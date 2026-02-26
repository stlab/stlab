/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_CHANNEL_HPP
#define STLAB_CONCURRENCY_CHANNEL_HPP

#include <stlab/config.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include <stlab/concurrency/executor_base.hpp>
#include <stlab/concurrency/traits.hpp>
#include <stlab/concurrency/tuple_algorithm.hpp>
#include <stlab/functional.hpp>
#include <stlab/memory.hpp>

/**************************************************************************************************/

namespace stlab {
inline namespace STLAB_VERSION_NAMESPACE() {

/**************************************************************************************************/

template <typename, typename = void>
class sender;
template <typename>
class receiver;

/**************************************************************************************************/
/*
 * close on a process is called when a process is in an await state to signal that no more data is
 * coming. In response to a close, a process can switch to a yield state to yield values, otherwise
 * it is destructed. await_try is await if a value is available, otherwise yield (allowing for an
 * interruptable task).
 */
enum class process_state : std::uint8_t { await, yield };

enum class message_t : std::uint8_t { argument, error };

/**************************************************************************************************/

using process_state_scheduled = std::pair<process_state, std::chrono::nanoseconds>;

constexpr process_state_scheduled await_forever{process_state::await,
                                                std::chrono::nanoseconds::max()};

constexpr process_state_scheduled yield_immediate{process_state::yield,
                                                  std::chrono::nanoseconds::min()};

/**************************************************************************************************/

enum class channel_error_codes : std::uint8_t { // names for channel errors
    broken_channel = 1,
    process_already_running = 2,
    no_state = 3
};

/**************************************************************************************************/

namespace detail {

inline auto channel_error_map(channel_error_codes code) noexcept
    -> char const* { // convert to name of channel error
    switch (code) {  // switch on error code value
        case channel_error_codes::broken_channel:
            return "broken channel";

        case channel_error_codes::process_already_running:
            return "process already running";

        case channel_error_codes::no_state:
            return "no state";

        default:
            return nullptr;
    }
}

/**************************************************************************************************/

} // namespace detail

/**************************************************************************************************/

// channel exception

class channel_error : public std::logic_error {
public:
    explicit channel_error(channel_error_codes code) : logic_error(""), _code(code) {}

    [[nodiscard]] auto code() const noexcept -> const channel_error_codes& { return _code; }

    [[nodiscard]] auto what() const noexcept -> const char* override {
        return detail::channel_error_map(_code);
    }

private:
    const channel_error_codes _code;
};

/**************************************************************************************************/

template <typename I, // I models ForwardIterator
          typename N, // N models PositiveInteger
          typename F> // F models UnaryFunction
auto for_each_n(I p, N n, F f) -> I {
    for (N i = 0; i != n; ++i, ++p) {
        f(*p);
    }
    return p;
}

struct identity {
    template <typename T>
    auto operator()(T&& x) const -> T {
        return std::forward<T>(x);
    }
};

/**************************************************************************************************/

template <typename>
struct result_of_;

template <typename R, typename... Args>
struct result_of_<R(Args...)> {
    using type = R;
};

template <typename F>
using result_of_t_ = typename result_of_<F>::type;

template <class T1, class... T>
struct first_ {
    using type = T1;
};

template <typename... T>
using first_t = typename first_<T...>::type;

/**************************************************************************************************/

template <typename>
struct argument_of;
template <typename R, typename Arg>
struct argument_of<R(Arg)> {
    using type = Arg;
};

template <typename T>
using argument_of_t = typename argument_of<T>::type;

/**************************************************************************************************/

namespace detail {

/**************************************************************************************************/

template <typename T, typename...>
auto yield_type_(decltype(&T::yield)) -> decltype(std::declval<T>().yield());

template <typename T, typename... Arg>
auto yield_type_(...) -> decltype(std::declval<T>()(std::declval<Arg>()...));

template <typename T, typename... Arg>
using yield_type = decltype(yield_type_<T, Arg...>(0));

/**************************************************************************************************/

class avoid_ {};

template <typename T>
using avoid = std::conditional_t<std::is_same_v<void, T>, avoid_, T>;

/**************************************************************************************************/

template <typename F, std::size_t... I, typename... T>
auto invoke_(F&& f,
             std::tuple<std::variant<T, std::exception_ptr>...>& t,
             std::index_sequence<I...>) {
    return std::forward<F>(f)(std::move(std::get<I>(t))...);
}

template <typename F, typename... Args>
auto avoid_invoke(F&& f, std::tuple<std::variant<Args, std::exception_ptr>...>& t)
    -> std::enable_if_t<!std::is_same_v<void, yield_type<unwrap_reference_t<F>, Args...>>,
                        yield_type<unwrap_reference_t<F>, Args...>> {
    return invoke_(std::forward<F>(f), t, std::make_index_sequence<sizeof...(Args)>());
}

template <typename F, typename... Args>
auto avoid_invoke(F&& f, std::tuple<std::variant<Args, std::exception_ptr>...>& t)
    -> std::enable_if_t<std::is_same_v<void, yield_type<unwrap_reference_t<F>, Args...>>, avoid_> {
    invoke_(std::forward<F>(f), t, std::make_index_sequence<sizeof...(Args)>());
    return avoid_();
}

/**************************************************************************************************/

template <std::size_t S>
struct invoke_variant_dispatcher {
    template <typename F, typename T, typename... Args, std::size_t... I>
    static auto invoke_(F&& f, T& t, std::index_sequence<I...>) {
        return std::forward<F>(f)(std::move(std::get<Args>(std::get<I>(t)))...);
    }

    template <typename F, typename T, typename... Args>
    static auto invoke(F&& f, T& t) {
        return invoke_<F, T, Args...>(std::forward<F>(f), t,
                                      std::make_index_sequence<sizeof...(Args)>());
    }
};

template <>
struct invoke_variant_dispatcher<1> {
    template <typename F, typename T, typename... Args>
    static auto invoke(F&& f, [[maybe_unused]] T& t) {
        using arg1_t = first_t<Args...>;
        if constexpr (std::is_same_v<arg1_t, void>) {
            return;
        } else if constexpr (std::is_same_v<arg1_t, detail::avoid_>) {
            return std::forward<F>(f)();
        } else {
            return std::forward<F>(f)(std::move(std::get<arg1_t>(std::get<0>(t))));
        }
    }
};

template <typename F, typename T, typename R, std::size_t S, typename... Args>
auto avoid_invoke_variant(F&& f, T& t) -> std::enable_if_t<!std::is_same_v<void, R>, R> {
    return invoke_variant_dispatcher<S>::template invoke<F, T, Args...>(std::forward<F>(f), t);
}

template <typename F, typename T, typename R, std::size_t S, typename... Args>
auto avoid_invoke_variant(F&& f, T& t) -> std::enable_if_t<std::is_same_v<void, R>, avoid_> {
    invoke_variant_dispatcher<S>::template invoke<F, T, Args...>(std::forward<F>(f), t);
    return avoid_();
}

/**************************************************************************************************/

template <typename T>
using receiver_t = typename std::remove_reference_t<T>::result_type;

/**************************************************************************************************/

template <typename T>
struct shared_process_receiver {
    virtual ~shared_process_receiver() = default;

    virtual void map(sender<T>) = 0;
    virtual void clear_to_send() = 0;
    virtual void add_receiver() = 0;
    virtual void remove_receiver() = 0;
    [[nodiscard]] virtual auto executor() const -> executor_t = 0;
    virtual void set_buffer_size(size_t) = 0;
    [[nodiscard]] virtual auto buffer_size() const -> size_t = 0;
};

/**************************************************************************************************/

template <typename T>
struct shared_process_sender {
    virtual ~shared_process_sender() = default;

    virtual void send(avoid<T> x) = 0;
    virtual void send(std::exception_ptr) = 0;
    virtual void add_sender() = 0;
    virtual void remove_sender() = 0;
    [[nodiscard]] virtual auto free_buffer() const -> std::size_t = 0;
};

/**************************************************************************************************/

template <typename T>
using process_close_t = decltype(std::declval<T&>().close());

template <typename T>
constexpr bool has_process_close_v = is_detected_v<process_close_t, T>;

template <typename T>
auto process_close(std::optional<T>& x)
    -> std::enable_if_t<has_process_close_v<unwrap_reference_t<T>>> {
    if (x) unwrap(*x).close();
}

template <typename T>
auto process_close(std::optional<T>&)
    -> std::enable_if_t<!has_process_close_v<unwrap_reference_t<T>>> {}

/**************************************************************************************************/

template <typename T>
using process_state_t = decltype(std::declval<const T&>().state());

template <typename T>
constexpr bool has_process_state_v = is_detected_v<process_state_t, T>;

template <typename T>
auto get_process_state(const std::optional<T>& x)
    -> std::enable_if_t<has_process_state_v<unwrap_reference_t<T>>, process_state_scheduled> {
    return unwrap(*x).state();
}

template <typename T>
auto get_process_state(const std::optional<T>&)
    -> std::enable_if_t<!has_process_state_v<unwrap_reference_t<T>>, process_state_scheduled> {
    return await_forever;
}

/**************************************************************************************************/

template <typename P>
using process_set_error_t =
    decltype(std::declval<P&>().set_error(std::declval<std::exception_ptr>()));

template <typename P>
constexpr bool has_set_process_error_v = is_detected_v<process_set_error_t, P>;

template <typename P>
auto set_process_error(P& process, std::exception_ptr&& error)
    -> std::enable_if_t<has_set_process_error_v<unwrap_reference_t<P>>, void> {
    unwrap(process).set_error(std::move(error));
}

template <typename P>
auto set_process_error(P&, std::exception_ptr&&)
    -> std::enable_if_t<!has_set_process_error_v<unwrap_reference_t<P>>, void> {}

/**************************************************************************************************/

template <typename T>
using process_yield_t = decltype(std::declval<T&>().yield());

template <typename T>
constexpr bool has_process_yield_v = is_detected_v<process_yield_t, T>;

/**************************************************************************************************/

template <typename T, typename... Args>
using process_await_t = decltype(std::declval<T&>().await(std::declval<Args>()...));

template <typename T, typename... Args>
constexpr bool has_process_await_v = is_detected_v<process_await_t, T, Args...>;

/**************************************************************************************************/

template <typename P, typename... T, std::size_t... I>
void await_variant_args_(P& process,
                         std::tuple<std::variant<detail::avoid<T>, std::exception_ptr>...>& args,
                         std::index_sequence<I...>) {
    unwrap(process).await(std::move(std::get<T>(std::get<I>(args)))...);
}

template <typename P, typename... T>
void await_variant_args(P& process,
                        std::tuple<std::variant<detail::avoid<T>, std::exception_ptr>...>& args) {
    await_variant_args_<P, T...>(process, args, std::make_index_sequence<sizeof...(T)>());
}

/**************************************************************************************************/

template <typename T>
auto find_argument_error(T& argument) -> std::optional<std::exception_ptr> {
    std::optional<std::exception_ptr> result;

    auto error_index = tuple_find(argument, [](const auto& c) {
        return static_cast<message_t>(c.index()) == message_t::error;
    });

    if (error_index != std::tuple_size_v<T>) {
        result = get_i(
            argument, error_index, [](auto& elem) { return std::get<std::exception_ptr>(elem); },
            std::exception_ptr{});
    }

    return result;
}

/**************************************************************************************************/

template <typename T>
struct default_queue_strategy {
    static const std::size_t arguments_size = 1;
    using value_type = std::tuple<std::variant<avoid<T>, std::exception_ptr>>;

    std::deque<std::variant<avoid<T>, std::exception_ptr>> _queue;

    [[nodiscard]] auto empty() const -> bool { return _queue.empty(); }

    auto front() { return std::make_tuple(std::move(_queue.front())); }

    void pop_front() { _queue.pop_front(); }

    [[nodiscard]] auto size() const { return std::array<std::size_t, 1>{{_queue.size()}}; }

    template <std::size_t>
    [[nodiscard]] auto queue_size() const {
        return _queue.size();
    }

    template <std::size_t, typename U>
    void append(U&& u) {
        _queue.emplace_back(std::forward<U>(u));
    }
};

/**************************************************************************************************/

template <typename... T>
struct zip_with_queue_strategy {
    static const std::size_t Size = sizeof...(T);
    static const std::size_t arguments_size = Size;
    using value_type = std::tuple<std::variant<T, std::exception_ptr>...>;
    using queue_size_t = std::array<std::size_t, Size>;
    using queue_t = std::tuple<std::deque<std::variant<T, std::exception_ptr>>...>;

    queue_t _queue;

    [[nodiscard]] auto empty() const -> bool {
        return tuple_find(_queue, [](const auto& c) { return c.empty(); }) != Size;
    }

    template <std::size_t... I, typename U>
    auto front_impl(U& u, std::index_sequence<I...>) {
        return std::make_tuple(std::move(std::get<I>(u).front())...);
    }

    auto front() {
        assert(!empty() && "front on an empty container is a very bad idea!");
        return front_impl(_queue, std::make_index_sequence<Size>());
    }

    void pop_front() {
        tuple_for_each(_queue, [](auto& c) { c.pop_front(); });
    }

    [[nodiscard]] auto size() const {
        queue_size_t result;
        std::size_t i = 0;
        tuple_for_each(_queue, [&i, &result](const auto& c) { result[i++] = c.size(); });
        return result;
    }

    template <std::size_t I>
    [[nodiscard]] auto queue_size() const {
        return std::get<I>(_queue).size();
    }

    template <std::size_t I, typename U>
    void append(U&& u) {
        std::get<I>(_queue).emplace_back(std::forward<U>(u));
    }
};

/**************************************************************************************************/

template <typename... T>
struct round_robin_queue_strategy {
    static const std::size_t Size = sizeof...(T);
    static const std::size_t arguments_size = 1;
    using item_t = std::variant<first_t<T...>, std::exception_ptr>;
    using value_type = std::tuple<item_t>;
    using queue_size_t = std::array<std::size_t, Size>;
    using queue_t = std::tuple<std::deque<std::variant<T, std::exception_ptr>>...>;
    std::size_t _index{0};
    std::size_t _popped_index{0};
    queue_t _queue;

    [[nodiscard]] auto empty() const -> bool {
        return get_i(_queue, _index, [](const auto& c) { return c.empty(); }, true);
    }

    auto front() {
        assert(!empty() && "front on an empty container is a very bad idea!");
        return std::make_tuple(get_i(_queue, _index, [](auto& c) { return c.front(); }, item_t{}));
    }

    void pop_front() {
        void_i(_queue, _index, [](auto& c) { c.pop_front(); });
        _popped_index = _index;
        ++_index;
        if (_index == Size) _index = 0; // restart from the first sender
    }

    [[nodiscard]] auto size() const {
        queue_size_t result;
        std::size_t i = 0;
        tuple_for_each(_queue, [&i, &result, this](const auto& c) {
            if (i == _popped_index)
                result[i] = c.size();
            else
                result[i] = std::numeric_limits<std::size_t>::max();
            ++i;
        });
        return result;
    }

    template <std::size_t I>
    [[nodiscard]] auto queue_size() const {
        return std::get<I>(_queue).size();
    }

    template <std::size_t I, typename U>
    void append(U&& u) {
        std::get<I>(_queue).emplace_back(std::forward<U>(u));
    }
};

/**************************************************************************************************/

template <typename... T>
struct unordered_queue_strategy {
    static const std::size_t Size = sizeof...(T);
    static const std::size_t arguments_size = 1;
    using item_t = std::variant<first_t<T...>, std::exception_ptr>;
    using value_type = std::tuple<item_t>;
    using queue_size_t = std::array<std::size_t, Size>;
    using queue_t = std::tuple<std::deque<std::variant<T, std::exception_ptr>>...>;
    std::size_t _index{0};
    std::size_t _popped_index{0};
    queue_t _queue;

    [[nodiscard]] auto empty() const -> bool {
        return tuple_find(_queue, [](const auto& c) { return !c.empty(); }) == Size;
    }

    auto front() {
        assert(!empty() && "front on an empty container is a very bad idea!");
        _index = tuple_find(_queue, [](const auto& c) { return !c.empty(); });
        return std::make_tuple(
            get_i(_queue, _index, [](auto& c) { return std::move(c.front()); }, item_t{}));
    }

    void pop_front() {
        void_i(_queue, _index, [](auto& c) { c.pop_front(); });
        _popped_index = _index;
    }

    [[nodiscard]] auto size() const {
        queue_size_t result;
        std::size_t i = 0;
        tuple_for_each(_queue, [&i, &result, this](const auto& c) {
            if (i == _popped_index)
                result[i] = c.size();
            else
                result[i] = std::numeric_limits<std::size_t>::max();
            ++i;
        });

        return result;
    }

    template <std::size_t I>
    [[nodiscard]] auto queue_size() const {
        return std::get<I>(_queue).size();
    }

    template <std::size_t I, typename U>
    void append(U&& u) {
        std::get<I>(_queue).emplace_back(std::forward<U>(u));
    }
};

/**************************************************************************************************/

template <typename Q, typename T, typename R, typename... Args>
struct shared_process;

template <typename Q, typename T, typename R, typename Arg, std::size_t I, typename... Args>
struct shared_process_sender_indexed : public shared_process_sender<Arg> {
    shared_process<Q, T, R, Args...>& _shared_process;

    explicit shared_process_sender_indexed(shared_process<Q, T, R, Args...>& sp) :
        _shared_process(sp) {}

    void add_sender() override { ++_shared_process._sender_count; }

    void remove_sender() override {
        assert(_shared_process._sender_count > 0);
        if (--_shared_process._sender_count == 0) {
            bool do_run;
            {
                std::unique_lock<std::mutex> lock(_shared_process._process_mutex);
                _shared_process._process_close_queue = true;
                do_run = !_shared_process._receiver_count && !_shared_process._process_running;
                _shared_process._process_running = _shared_process._process_running || do_run;
            }
            if (do_run) _shared_process.run();
        }
    }

    template <typename U>
    void enqueue(U&& u) {
        bool do_run;
        {
            std::unique_lock<std::mutex> lock(_shared_process._process_mutex);
            _shared_process._queue.template append<I>(
                std::forward<U>(u)); // TODO (sparent) : overwrite here.
            do_run = !_shared_process._receiver_count && (!_shared_process._process_running ||
                                                          _shared_process._timeout_function_active);

            _shared_process._process_running = _shared_process._process_running || do_run;
        }
        if (do_run) _shared_process.run();
    }

    void send(std::exception_ptr error) override { enqueue(std::move(error)); }

    void send(avoid<Arg> arg) override { enqueue(std::move(arg)); }

    [[nodiscard]] auto free_buffer() const -> std::size_t override {
        std::unique_lock<std::mutex> lock(_shared_process._process_mutex);
        return _shared_process._process_buffer_size == 0 ?
                   std::numeric_limits<std::size_t>::max() :
                   (_shared_process._process_buffer_size -
                    _shared_process._queue.template queue_size<I>());
    }
};

/**************************************************************************************************/

template <typename Q, typename T, typename R, typename U, typename... Args>
struct shared_process_sender_helper;

template <typename Q, typename T, typename R, std::size_t... I, typename... Args>
struct shared_process_sender_helper<Q, T, R, std::index_sequence<I...>, Args...>
    : shared_process_sender_indexed<Q, T, R, Args, I, Args...>... {
    explicit shared_process_sender_helper(shared_process<Q, T, R, Args...>& sp) :
        shared_process_sender_indexed<Q, T, R, Args, I, Args...>(sp)... {}
};

/**************************************************************************************************/

template <typename R, typename Enabled = void>
struct downstream;

template <typename R>
struct downstream<R, std::enable_if_t<std::is_copy_constructible_v<R> || std::is_same_v<R, void>>> {
    std::deque<sender<R>> _data;

    template <typename F>
    void append_receiver(F&& f) {
        _data.emplace_back(std::forward<F>(f));
    }

    void clear() { _data.clear(); }

    [[nodiscard]] auto size() const -> std::size_t { return _data.size(); }

    template <typename... Args>
    void send(std::size_t n, Args... args) {
        stlab::for_each_n(begin(_data), n, [&](const auto& e) { e(args...); });
    }

    [[nodiscard]] auto minimum_free_buffer() const -> std::size_t {
        if (size() == 0) return 0;
        // std::reduce with C++17
        return std::accumulate(_data.cbegin(), _data.cend(),
                               std::numeric_limits<std::size_t>::max(),
                               [](auto val, const auto& e) {
                                   return std::min(val, e.free_buffer() ? *e.free_buffer() : val);
                               });
    }
};

template <typename R>
struct downstream<R,
                  std::enable_if_t<!std::is_copy_constructible_v<R> && !std::is_same_v<R, void>>> {
    std::optional<sender<R>> _data;

    template <typename F>
    void append_receiver(F&& f) {
        _data = std::forward<F>(f);
    }

    void clear() { _data = std::nullopt; }

    [[nodiscard]] auto size() const -> std::size_t { return 1; }

    template <typename... Args>
    void send(std::size_t, Args&&... args) {
        if (_data) (*_data)(std::forward<Args>(args)...);
    }

    [[nodiscard]] auto minimum_free_buffer() const -> std::size_t {
        if (_data && (*_data).free_buffer()) return *(*_data).free_buffer();
        return 0;
    }
};

/**************************************************************************************************/

template <typename Q, typename T, typename R, typename... Args>
struct shared_process
    : shared_process_receiver<R>,
      shared_process_sender_helper<Q, T, R, std::make_index_sequence<sizeof...(Args)>, Args...>,
      std::enable_shared_from_this<shared_process<Q, T, R, Args...>> {
    static_assert((has_process_yield_v<unwrap_reference_t<T>> &&
                   has_process_state_v<unwrap_reference_t<T>>) ||
                      (!has_process_yield_v<unwrap_reference_t<T>> &&
                       !has_process_state_v<unwrap_reference_t<T>>),
                  "Processes that use .yield() must have .state() const");

    /*
        the downstream continuations are stored in a deque so we don't get reallocations
        on push back - this allows us to make calls while additional inserts happen.
    */

    using result_t = R;
    using queue_strategy_t = Q;
    using process_t = T;
    using lock_t = std::unique_lock<std::mutex>;

    std::mutex _downstream_mutex;
    downstream<result_t> _downstream;
    queue_strategy_t _queue;

    executor_t _executor;
    std::optional<process_t> _process;

    std::mutex _process_mutex;

    bool _process_running = false;
    std::atomic_size_t _process_suspend_count{0};
    bool _process_close_queue = false;
    // REVISIT (sparent) : I'm not certain final needs to be under the mutex
    bool _process_final = false;

    std::mutex _timeout_function_control;
    std::atomic_bool _timeout_function_active{false};

    std::atomic_size_t _sender_count{0};
    std::atomic_size_t _receiver_count;

    std::atomic_size_t _process_buffer_size{1};

    const std::tuple<std::shared_ptr<shared_process_receiver<Args>>...> _upstream;

    template <typename E, typename F>
    shared_process(E&& e, F&& f) :
        shared_process_sender_helper<Q, T, R, std::make_index_sequence<sizeof...(Args)>, Args...>(
            *this),
        _executor(std::forward<E>(e)), _process(std::forward<F>(f)) {
        _sender_count = std::is_same_v<result_t, void> ? 0 : 1;
        _receiver_count = !std::is_same_v<result_t, void>;
    }

    template <typename E, typename F, typename... U>
    shared_process(E&& e, F&& f, U&&... u) :
        shared_process_sender_helper<Q, T, R, std::make_index_sequence<sizeof...(Args)>, Args...>(
            *this),
        _executor(std::forward<E>(e)), _process(std::forward<F>(f)),
        _upstream(std::forward<U>(u)...) {
        _sender_count = sizeof...(Args);
        _receiver_count = !std::is_same_v<result_t, void>;
    }

    void add_receiver() override {
        if (std::is_same_v<result_t, void>) return;
        ++_receiver_count;
    }

    void remove_receiver() override {
        if (std::is_same_v<result_t, void>) return;
        /*
            NOTE (sparent) : Decrementing the receiver count can allow this to start running on a
            send before we can get to the check - so we need to see if we are already running
            before starting again.
        */
        assert(_receiver_count > 0);
        if (--_receiver_count == 0) {
            bool do_run;
            {
                std::unique_lock<std::mutex> lock(_process_mutex);
                do_run = ((!_queue.empty() || std::is_same_v<first_t<Args...>, void>) ||
                          _process_close_queue) &&
                         !_process_running;
                _process_running = _process_running || do_run;
            }
            if (do_run) run();
        }
    }

    auto executor() const -> executor_t override { return _executor; }

    void task_done() {
        bool do_run;
        bool do_final;
        {
            std::unique_lock<std::mutex> lock(_process_mutex);
            do_run = !_queue.empty() || _process_close_queue;
            _process_running = do_run;
            do_final = _process_final;
        }
        // The mutual exclusiveness of this assert implies too many variables. Should have a single
        // "get state" call.
        assert(!(do_run && do_final) && "ERROR (sparent) : can't run and close at the same time.");
        // I met him on a Monday and my heart stood still
        if (do_run) run();
        if (do_final) {
            std::unique_lock<std::mutex> lock(_downstream_mutex);
            _downstream.clear(); // This will propagate the close to anything downstream
            _process = std::nullopt;
        }
    }

    void clear_to_send() override {
        {
            std::unique_lock<std::mutex> lock(_process_mutex);
            if (_process_final) {
                return;
            }
        }

        bool do_run = false;
        {
            const auto ps = get_process_state(_process);
            std::unique_lock<std::mutex> lock(_process_mutex);
            if (_process_suspend_count > 0) --_process_suspend_count; // could be atomic?

            if (!_process_suspend_count) {
                if (ps.first == process_state::yield || !_queue.empty() || _process_close_queue) {
                    do_run = true;
                } else {
                    _process_running = false;
                    do_run = false;
                }
            }
        }
        // Somebody told me that his name was Bill
        if (do_run) run();
    }

    auto pop_from_queue() {
        std::optional<typename Q::value_type> message;
        std::array<bool, sizeof...(Args)> do_cts = {{false}};
        bool do_close = false;

        std::unique_lock<std::mutex> lock(_process_mutex);
        if (_queue.empty()) {
            std::swap(do_close, _process_close_queue);
            _process_final = do_close; // unravel after any yield
        } else {
            message = std::move(_queue.front());
            _queue.pop_front();
            auto queue_size = _queue.size();
            for (auto index = 0u; index < queue_size.size(); ++index)
                do_cts[index] = queue_size[index] <= (_process_buffer_size - 1);
        }
        return std::make_tuple(std::move(message), do_cts, do_close);
    }

    auto dequeue() -> bool {
        using queue_t = typename Q::value_type;
        std::optional<queue_t> message;
        std::array<bool, sizeof...(Args)> do_cts;
        bool do_close = false;

        std::tie(message, do_cts, do_close) = pop_from_queue();

        std::size_t i = 0;
        tuple_for_each(_upstream, [do_cts, &i](auto& u) {
            if (do_cts[i] && u) u->clear_to_send();
            ++i;
        });

        if (message) {
            auto error = find_argument_error(*message);
            if (error) {
                if (has_set_process_error_v<T>)
                    set_process_error(*_process, std::move(*error));
                else
                    do_close = true;
            } else
                await_variant_args<process_t, Args...>(*_process, *message);
        } else {
            do_close = true;
        }

        if (do_close) process_close(_process);

        return bool(message);
    }

    /*
        REVISIT (sparent) : Next two cases are nearly identical, complicated by the need to
        remove constexpr if to support C++14.
    */

    template <typename U>
    auto step() -> std::enable_if_t<has_process_yield_v<unwrap_reference_t<U>> &&
                                    !has_process_await_v<unwrap_reference_t<T>, Args...>> {
        // in case that the timeout function is just been executed then we have to re-schedule
        // the current run
        lock_t lock(_timeout_function_control, std::try_to_lock);
        if (!lock) {
            run();
            return;
        }
        _timeout_function_active = false;

        /*
            While we are waiting we will flush the queue. The assumption here is that work
            is done on yield()
        */
        try {
            if (get_process_state(_process).first == process_state::await) return;

            // Workaround until we can use structured bindings
            auto tmp = get_process_state(_process);
            const auto& state = tmp.first;
            const auto& duration = tmp.second;

            /*
                Once we hit yield, go ahead and call it. If the yield is delayed then schedule it.
               This process will be considered running until it executes.
            */
            if (state == process_state::yield) {
                if (std::chrono::duration_cast<std::chrono::nanoseconds>(duration) <=
                    std::chrono::nanoseconds::min())
                    broadcast(unwrap(*_process).yield());
                else
                    execute_at(duration, _executor)(
                        [_weak_this = make_weak_ptr(this->shared_from_this())]() noexcept {
                            auto _this = _weak_this.lock();
                            if (!_this) return;
                            _this->try_broadcast();
                        });
            }

            /*
                We are in an await state and the queue is empty.

                If we await forever then task_done() leaving us in an await state.
                else if we await with an expired timeout then go ahead and yield now.
                else schedule a timeout when we will yield if not canceled by intervening await.
            */
            else if (std::chrono::duration_cast<std::chrono::nanoseconds>(duration) ==
                     std::chrono::nanoseconds::max()) {
                task_done();
            } else if (std::chrono::duration_cast<std::chrono::nanoseconds>(duration) <=
                       std::chrono::nanoseconds::min()) {
                broadcast(unwrap(*_process).yield());
            } else {
                /* Schedule a timeout. */
                _timeout_function_active = true;
                execute_at(duration, _executor)(
                    [_weak_this = make_weak_ptr(this->shared_from_this())]() noexcept {
                        auto _this = _weak_this.lock();
                        // It may be that the complete channel is gone in the meanwhile
                        if (!_this) return;

                        // try_lock can fail spuriously
                        while (true) {
                            lock_t lock(_this->_timeout_function_control, std::try_to_lock);
                            if (!lock) continue;

                            // we were cancelled
                            if (get_process_state(_this->_process).first != process_state::yield) {
                                _this->try_broadcast();
                                _this->_timeout_function_active = false;
                            }
                            return;
                        }
                    });
            }
        } catch (...) { // this catches exceptions during _process.await() and _process.yield()
            broadcast(std::move(std::current_exception()));
        }
    }

    template <typename U>
    auto step() -> std::enable_if_t<has_process_yield_v<unwrap_reference_t<U>> &&
                                    has_process_await_v<unwrap_reference_t<T>, Args...>> {
        // in case that the timeout function is just been executed then we have to re-schedule
        // the current run
        lock_t lock(_timeout_function_control, std::try_to_lock);
        if (!lock) {
            run();
            return;
        }
        _timeout_function_active = false;

        /*
            While we are waiting we will flush the queue. The assumption here is that work
            is done on yield()
        */
        try {
            while (get_process_state(_process).first == process_state::await) {
                if (!dequeue()) break;
            }

            // Workaround until we can use structured bindings
            auto tmp = get_process_state(_process);
            const auto& state = tmp.first;
            const auto& duration = tmp.second;

            /*
                Once we hit yield, go ahead and call it. If the yield is delayed then schedule it.
               This process will be considered running until it executes.
            */
            if (state == process_state::yield) {
                if (std::chrono::duration_cast<std::chrono::nanoseconds>(duration) <=
                    std::chrono::nanoseconds::min())
                    broadcast(unwrap(*_process).yield());
                else
                    execute_at(duration, _executor)(
                        [_weak_this = make_weak_ptr(this->shared_from_this())]() noexcept {
                            auto _this = _weak_this.lock();
                            if (!_this) return;
                            _this->try_broadcast();
                        });
            }

            /*
                We are in an await state and the queue is empty.

                If we await forever then task_done() leaving us in an await state.
                else if we await with an expired timeout then go ahead and yield now.
                else schedule a timeout when we will yield if not canceled by intervening await.
            */
            else if (std::chrono::duration_cast<std::chrono::nanoseconds>(duration) ==
                     std::chrono::nanoseconds::max()) {
                task_done();
            } else if (std::chrono::duration_cast<std::chrono::nanoseconds>(duration) <=
                       std::chrono::nanoseconds::min()) {
                broadcast(unwrap(*_process).yield());
            } else {
                /* Schedule a timeout. */
                _timeout_function_active = true;
                execute_at(duration, _executor)(
                    [_weak_this = make_weak_ptr(this->shared_from_this())]() noexcept {
                        auto _this = _weak_this.lock();
                        // It may be that the complete channel is gone in the meanwhile
                        if (!_this) return;

                        // try_lock can fail spuriously
                        while (true) {
                            lock_t lock(_this->_timeout_function_control, std::try_to_lock);
                            if (!lock) continue;

                            // we were cancelled
                            if (get_process_state(_this->_process).first != process_state::yield) {
                                _this->try_broadcast();
                                _this->_timeout_function_active = false;
                            }
                            return;
                        }
                    });
            }
        } catch (...) { // this catches exceptions during _process.await() and _process.yield()
            broadcast(std::move(std::current_exception()));
        }
    }

    void try_broadcast() {
        try {
            if (_process) broadcast(unwrap(*_process).yield());
        } catch (...) {
            broadcast(std::move(std::current_exception()));
        }
    }
    /*
        REVISIT (sparent) : See above comments on step() and ensure consistency.

        What is this code doing, if we don't have a yield then it also assumes no await?

        This seems to be doing a lot for a (required) noexcept operation - are we sure?
    */

    template <typename U>
    auto step() noexcept -> std::enable_if_t<!has_process_yield_v<unwrap_reference_t<U>>> {
        using queue_t = typename Q::value_type;
        std::optional<queue_t> message;
        std::array<bool, sizeof...(Args)> do_cts;
        bool do_close = false;

        std::tie(message, do_cts, do_close) = pop_from_queue();

        std::size_t i = 0;
        tuple_for_each(_upstream, [do_cts, &i](auto& u) {
            if (do_cts[i] && u) u->clear_to_send();
            ++i;
        });

        if (message) {
            auto error = find_argument_error(*message);
            if (error) {
                do_close = true;
            } else {
                try {
                    broadcast(
                        avoid_invoke_variant<process_t, queue_t, R, Q::arguments_size, Args...>(
                            std::move(*_process), *message));
                } catch (...) {
                    broadcast(std::move(std::current_exception()));
                }
            }
        } else
            task_done();
    }

    void run() {
        _executor([_p = make_weak_ptr(this->shared_from_this())]() noexcept {
            auto p = _p.lock();
            if (p) p->template step<T>();
        });
    }

    template <typename... A>
    void broadcast(A&&... args) {
        /*
            We broadcast the result to any processes currently attached to receiver
        */

        std::size_t n;
        bool suspend_process;
        {
            std::unique_lock<std::mutex> lock(_downstream_mutex);
            n = _downstream.size();
            suspend_process = _downstream.minimum_free_buffer() <= 1;
        }

        {
            std::unique_lock<std::mutex> lock(_process_mutex);
            if (suspend_process) {
                /*
                    Suspend for however many downstream processes we have + 1 for this process -
                   that ensures that we are not kicked to run while still broadcasting.
                */
                _process_suspend_count = n + 1;
            } else
                _process_suspend_count = 1;
        }
        /*
            There is no lock on _downstream here. We only ever append to this deque so the first
            n elements are good.
        */

        _downstream.send(n, std::forward<A>(args)...);

        clear_to_send(); // unsuspend this process and decrement the _process_suspend_count
                         // immediately by 1
    }

    void map(sender<result_t> f) override {
        /*
            REVISIT (sparent) : If we are in a final state then we should destruct the sender
            and not add it here.
        */
        {
            std::unique_lock<std::mutex> lock(_downstream_mutex);
            _downstream.append_receiver(std::move(f));
        }
    }

    void set_buffer_size(size_t buffer_size) override { _process_buffer_size = buffer_size; }

    auto buffer_size() const -> size_t override { return _process_buffer_size; }
};

/**************************************************************************************************/

} // namespace detail

struct unordered_t {
    template <typename... R>
    using strategy_type = detail::unordered_queue_strategy<detail::receiver_t<R>...>;
};

struct round_robin_t {
    template <typename... R>
    using strategy_type = detail::round_robin_queue_strategy<detail::receiver_t<R>...>;
};

struct zip_with_t {
    template <typename... R>
    using strategy_type = detail::zip_with_queue_strategy<detail::receiver_t<R>...>;
};

/**************************************************************************************************/

namespace detail {

// This helper class is necessary to encapsulate the following functions, because Clang
// currently has a bug in accepting friend functions with auto return type
struct channel_combiner {
    template <typename P, typename URP, typename... R, std::size_t... I>
    static void map_as_sender_(P& p, URP& upstream_receiver_processes, std::index_sequence<I...>) {
        using shared_process_t = typename P::element_type;
        using queue_t = typename shared_process_t::queue_strategy_t;
        using process_t = typename shared_process_t::process_t;
        using result_t = typename shared_process_t::result_t;

        (void)std::initializer_list<int>{
            (std::get<I>(upstream_receiver_processes)
                 ->map(sender<R>(std::dynamic_pointer_cast<shared_process_sender<R>>(
                     std::dynamic_pointer_cast<
                         shared_process_sender_indexed<queue_t, process_t, result_t, R, I, R...>>(
                         p)))),
             0)...};
    }

    template <typename P, typename URP, typename... R>
    static void map_as_sender(P& p, URP& upstream_receiver_processes) {
        map_as_sender_<P, URP, R...>(p, upstream_receiver_processes,
                                     std::make_index_sequence<sizeof...(R)>());
    }

    template <typename M, typename F, typename... R>
    struct merge_result {
        using type = yield_type<unwrap_reference_t<F>, receiver_t<first_t<R...>>>;
    };

    template <typename F, typename... R>
    struct merge_result<zip_with_t, F, R...> {
        using type = yield_type<unwrap_reference_t<F>, receiver_t<R>...>;
    };

    template <typename M, typename S, typename F, typename... R>
    static auto merge_helper(S&& s, F&& f, R&&... upstream_receiver) {
        using result_t = typename merge_result<M, F, R...>::type;

        auto upstream_receiver_processes = std::make_tuple(upstream_receiver._p...);
        auto merge_process =
            std::make_shared<shared_process<typename M::template strategy_type<R...>, F, result_t,
                                            receiver_t<R>...>>(
                std::forward<S>(s), std::forward<F>(f), upstream_receiver._p...);

        map_as_sender<decltype(merge_process), decltype(upstream_receiver_processes),
                      receiver_t<R>...>(merge_process, upstream_receiver_processes);

        return receiver<result_t>(std::move(merge_process));
    }
};

struct zip_helper {
    template <typename... T>
    auto operator()(T&&... t) const {
        return std::make_tuple(std::forward<T>(t)...);
    }
};

template <typename E, typename T>
struct channel_ {
    static auto create(E executor) {
        auto p = std::make_shared<
            detail::shared_process<detail::default_queue_strategy<T>, identity, T, T>>(
            std::move(executor), identity());

        return std::make_pair(sender<T>(p), receiver<T>(p));
    }
};

template <typename E>
struct channel_<E, void> {
    static auto create(E executor) {
        auto p = std::make_shared<
            detail::shared_process<detail::default_queue_strategy<void>, identity, void, void>>(
            std::move(executor), identity());

        return receiver<void>(p);
    }
};

/**************************************************************************************************/

} // namespace detail

/**************************************************************************************************/

template <typename T, typename E>
auto channel(E executor) {
    return detail::channel_<E, T>::create(std::move(executor));
}

/**************************************************************************************************/

template <typename S, typename F, typename... R>
[[deprecated("Use zip_with")]] auto join(S s, F f, R... upstream_receiver) {
    return detail::channel_combiner::merge_helper<zip_with_t, S, F, R...>(
        std::move(s), std::move(f), std::forward<R>(upstream_receiver)...);
}

/**************************************************************************************************/

template <typename S, typename F, typename... R>
[[deprecated("Use merge_channel<unordered_t>")]] auto merge(S s, F f, R... upstream_receiver) {
    return detail::channel_combiner::merge_helper<unordered_t, S, F, R...>(
        std::move(s), std::move(f), std::move(upstream_receiver)...);
}

/**************************************************************************************************/

template <typename M, typename S, typename F, typename... R>
auto merge_channel(S s, F f, R&&... upstream_receiver) {
    return detail::channel_combiner::merge_helper<M>(std::move(s), std::move(f),
                                                     std::forward<R>(upstream_receiver)...);
}

/**************************************************************************************************/

template <typename S, typename F, typename... R>
auto zip_with(S s, F f, const R&... upstream_receiver) {
    return detail::channel_combiner::merge_helper<zip_with_t>(std::move(s), std::move(f),
                                                              upstream_receiver...);
}

/**************************************************************************************************/

template <typename S, typename... R>
auto zip(S s, const R&... r) {
    return zip_with(std::move(s), detail::zip_helper{}, r...);
}

// template <typename S, typename F, typename... R>
// [[deprecated("Use merge_channel<round_robin_t>")]] auto zip(S s, F f, R... upstream_receiver);

/**************************************************************************************************/

struct buffer_size {
    std::size_t _value;
    buffer_size(std::size_t b) : _value(b) {}
};

/**************************************************************************************************/

namespace detail {

struct annotations {
    std::optional<executor_t> _executor;
    std::optional<std::size_t> _buffer_size;

    annotations(executor_t e, std::size_t bs) : _executor(std::move(e)), _buffer_size(bs) {}
    explicit annotations(executor_t e) : _executor(std::move(e)) {}
    explicit annotations(std::size_t bs) : _buffer_size(bs) {}
};

template <typename F>
struct annotated_process {
    using process_type = F;

    F _f;
    annotations _annotations;

    explicit annotated_process(executor_task_pair<F>&& etp) :
        _f(std::move(etp._f)), _annotations(std::move(etp._executor)) {}

    annotated_process(F f, const executor& e) : _f(std::move(f)), _annotations(e._executor) {}
    annotated_process(F f, buffer_size bs) : _f(std::move(f)), _annotations(bs._value) {}

    annotated_process(F f, executor&& e) : _f(std::move(f)), _annotations(std::move(e._executor)) {}
    annotated_process(F f, annotations&& a) : _f(std::move(f)), _annotations(std::move(a)) {}
    annotated_process(executor_task_pair<F>&& etp, buffer_size bs) :
        _f(std::move(etp._f)), _annotations(std::move(etp._executor), bs) {}
};

template <typename B, typename E>
auto combine_bs_executor(B&& b, E&& e) -> detail::annotations {
    detail::annotations result{b._value};
    result._executor = std::forward<E>(e)._executor;
    return result;
}

} // namespace detail

// !std::is_enum_v<std::remove_reference_t<F>>, int> = 0 is used to exclude enum types from
// operator& so built-in & is used (e.g. doctest assertType).

inline auto operator&(buffer_size bs, const executor& e) -> detail::annotations {
    return detail::combine_bs_executor(bs, e);
}

inline auto operator&(buffer_size bs, executor&& e) -> detail::annotations {
    return detail::combine_bs_executor(bs, std::move(e));
}

inline auto operator&(const executor& e, buffer_size bs) -> detail::annotations {
    return detail::combine_bs_executor(bs, e);
}

inline auto operator&(executor&& e, buffer_size bs) -> detail::annotations {
    return detail::combine_bs_executor(bs, std::move(e));
}

template <typename F, std::enable_if_t<!std::is_enum_v<std::remove_reference_t<F>>, int> = 0>
auto operator&(buffer_size bs, F&& f) -> detail::annotated_process<F> {
    return detail::annotated_process<F>(std::forward<F>(f), bs);
}

template <typename F, std::enable_if_t<!std::is_enum_v<std::remove_reference_t<F>>, int> = 0>
auto operator&(F&& f, buffer_size bs) -> detail::annotated_process<F> {
    return detail::annotated_process<F>(std::forward<F>(f), bs);
}

template <typename F>
auto operator&(executor_task_pair<F>&& etp, buffer_size bs) -> detail::annotated_process<F> {
    return detail::annotated_process<F>{std::move(etp), bs};
}

template <typename F>
auto operator&(buffer_size bs, executor_task_pair<F>&& etp) -> detail::annotated_process<F> {
    return detail::annotated_process<F>{std::move(etp), bs};
}

template <typename F, std::enable_if_t<!std::is_enum_v<std::remove_reference_t<F>>, int> = 0>
auto operator&(detail::annotations&& a, F&& f) -> detail::annotated_process<F> {
    return detail::annotated_process<F>{std::forward<F>(f), std::move(a)};
}

template <typename F, std::enable_if_t<!std::is_enum_v<std::remove_reference_t<F>>, int> = 0>
auto operator&(F&& f, detail::annotations&& a) -> detail::annotated_process<F> {
    return detail::annotated_process<F>{std::forward<F>(f), std::move(a)};
}

template <typename F>
auto operator&(detail::annotated_process<F>&& a, executor&& e) -> detail::annotated_process<F> {
    auto result{std::move(a)};
    a._annotations._executor = std::move(e._executor);
    return result;
}

template <typename F>
auto operator&(detail::annotated_process<F>&& a, buffer_size bs) -> detail::annotated_process<F> {
    auto result{std::move(a)};
    a._annotations._buffer_size = bs._value;
    return result;
}

/**************************************************************************************************/

template <typename T>
class STLAB_NODISCARD() receiver {
    using ptr_t = std::shared_ptr<detail::shared_process_receiver<T>>;

    ptr_t _p;
    bool _ready = false;

    template <typename U, typename>
    friend class sender;

    template <typename U>
    friend class receiver;

    template <typename U, typename V>
    friend struct detail::channel_;

    friend struct detail::channel_combiner;

    explicit receiver(ptr_t p) : _p(std::move(p)) {}

public:
    using result_type = T;

    receiver() = default;

    ~receiver() {
        if (!_ready && _p) _p->remove_receiver();
    }

    receiver(const receiver& x) : _p(x._p), _ready(x._ready) {
        if (_p) _p->add_receiver();
    }

    receiver(receiver&&) noexcept = default;

    auto operator=(const receiver& x) -> receiver& {
        // self-assignment is not allowed to disable cert-oop54-cpp warning (and is likely a bug)
        assert(this != &x && "self-assignment is not allowed");
        return *this = receiver(x);
    }

    auto operator=(receiver&& x) noexcept -> receiver& = default;

    void set_ready() {
        if (!_ready && _p) _p->remove_receiver();
        _ready = true;
    }

    void swap(receiver& x) noexcept { std::swap(*this, x); }

    inline friend void swap(receiver& x, receiver& y) noexcept { x.swap(y); }
    inline friend auto operator==(const receiver& x, const receiver& y) -> bool {
        return x._p == y._p;
    };
    inline friend auto operator!=(const receiver& x, const receiver& y) -> bool {
        return !(x == y);
    };

    [[nodiscard]] auto ready() const -> bool { return _ready; }

    template <typename F>
    auto operator|(F&& f) const {
        if (!_p) throw channel_error(channel_error_codes::broken_channel);

        if (_ready) throw channel_error(channel_error_codes::process_already_running);

        auto p = std::make_shared<detail::shared_process<
            detail::default_queue_strategy<T>, F, detail::yield_type<unwrap_reference_t<F>, T>, T>>(
            _p->executor(), std::forward<F>(f), _p);
        _p->map(sender<T>(p));
        return receiver<detail::yield_type<unwrap_reference_t<F>, T>>(std::move(p));
    }

    template <typename F>
    auto operator|(detail::annotated_process<F> ap) {
        if (!_p) throw channel_error(channel_error_codes::broken_channel);

        if (_ready) throw channel_error(channel_error_codes::process_already_running);

        auto executor = ap._annotations._executor.value_or(_p->executor());
        auto p = std::make_shared<detail::shared_process<
            detail::default_queue_strategy<T>, F, detail::yield_type<unwrap_reference_t<F>, T>, T>>(
            executor, std::move(ap._f), _p);

        _p->map(sender<T>(p));

        if (ap._annotations._buffer_size) p->set_buffer_size(*ap._annotations._buffer_size);

        return receiver<detail::yield_type<unwrap_reference_t<F>, T>>(std::move(p));
    }

    template <typename F>
    auto operator|(executor_task_pair<F> etp) {
        return operator|(detail::annotated_process<F>(std::move(etp)));
    }

    auto operator|(sender<T> send) {
        return operator|(
            [_send = std::move(send)](auto&& x) { _send(std::forward<decltype(x)>(x)); });
    }
};

/**************************************************************************************************/

template <typename T>
class sender<T, enable_if_copyable<T>> {
    using ptr_t = std::weak_ptr<detail::shared_process_sender<T>>;
    ptr_t _p;

    template <typename U>
    friend class receiver;

    template <typename U, typename V>
    friend struct detail::channel_;

    friend struct detail::channel_combiner;

    sender(ptr_t p) : _p(std::move(p)) {}

public:
    sender() = default;

    ~sender() {
        if (auto p = _p.lock()) p->remove_sender();
    }

    sender(const sender& x) : _p(x._p) {
        if (auto p = _p.lock()) p->add_sender();
    }

    sender(sender&&) noexcept = default;

    // copy-assign uses copy-ctor to keep sender count correct.
    auto operator=(const sender& x) -> sender& {
        // self-assignment is not allowed to disable cert-oop54-cpp warning (and is likely a bug)
        assert(this != &x && "self-assignment is not allowed");
        return *this = sender(x);
    }

    auto operator=(sender&&) noexcept -> sender& = default;

    void swap(sender& x) noexcept { std::swap(*this, x); }

    inline friend void swap(sender& x, sender& y) noexcept { x.swap(y); }

    inline friend auto operator==(const sender& x, const sender& y) -> bool {
        return x._p.lock() == y._p.lock();
    };

    inline friend auto operator!=(const sender& x, const sender& y) -> bool { return !(x == y); };

    void close() {
        auto p = _p.lock();
        if (p) p->remove_sender();
        _p.reset();
    }

    template <typename... A>
    void operator()(A&&... args) const {
        auto p = _p.lock();
        if (p) p->send(std::forward<A>(args)...);
    }

    [[nodiscard]] auto free_buffer() const -> std::optional<std::size_t> {
        std::optional<std::size_t> result;
        auto p = _p.lock();
        if (p) result = p->free_buffer();
        return result;
    }
};

template <typename T>
class sender<T, enable_if_not_copyable<T>> {
    using ptr_t = std::weak_ptr<detail::shared_process_sender<T>>;
    ptr_t _p;

    template <typename U>
    friend class receiver;

    template <typename U, typename V>
    friend struct detail::channel_;

    friend struct detail::channel_combiner;

    sender(ptr_t p) : _p(std::move(p)) {}

public:
    sender() = default;

    ~sender() {
        auto p = _p.lock();
        if (p) p->remove_sender();
    }

    sender(const sender& x) = delete;
    sender(sender&&) noexcept = default;
    auto operator=(const sender& x) -> sender& = delete;
    auto operator=(sender&&) noexcept -> sender& = default;

    void swap(sender& x) noexcept { std::swap(*this, x); }

    inline friend void swap(sender& x, sender& y) noexcept { x.swap(y); }

    inline friend auto operator==(const sender& x, const sender& y) -> bool {
        return x._p.lock() == y._p.lock();
    };

    inline friend auto operator!=(const sender& x, const sender& y) -> bool { return !(x == y); };

    void close() {
        auto p = _p.lock();
        if (p) p->remove_sender();
        _p.reset();
    }

    template <typename... A>
    void operator()(A&&... args) const {
        auto p = _p.lock();
        if (p) p->send(std::forward<A>(args)...);
    }

    [[nodiscard]] auto free_buffer() const -> std::optional<std::size_t> {
        std::optional<std::size_t> result;
        auto p = _p.lock();
        if (p) result = p->free_buffer();
        return result;
    }
};

/**************************************************************************************************/

template <typename F>
struct function_process;

template <typename R, typename... Args>
struct function_process<R(Args...)> {
    std::function<R(Args...)> _f;
    std::function<R()> _bound;
    bool _done = true;

    using signature = R(Args...);

    template <typename F>
    function_process(F&& f) : _f(std::forward<F>(f)) {}

    template <typename... A>
    void await(A&&... args) {
        _bound = std::bind(_f, std::forward<A>(args)...);
        _done = false;
    }

    auto yield() -> R {
        _done = true;
        return _bound();
    }
    [[nodiscard]] auto state() const -> process_state_scheduled {
        return _done ? await_forever : yield_immediate;
    }
};

/**************************************************************************************************/

} // namespace STLAB_VERSION_NAMESPACE()
} // namespace stlab

/**************************************************************************************************/

#endif
