/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_FUTURE_HPP
#define STLAB_CONCURRENCY_FUTURE_HPP

#include <stlab/config.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <variant> // for std::monostate
#include <vector>

#if STLAB_STD_COROUTINES()
#include <coroutine>
#endif

#include <stlab/concurrency/executor_base.hpp>
#include <stlab/concurrency/immediate_executor.hpp>
#include <stlab/concurrency/task.hpp>
#include <stlab/concurrency/traits.hpp>
#include <stlab/concurrency/tuple_algorithm.hpp>
#include <stlab/functional.hpp>
#include <stlab/memory.hpp>
#include <stlab/utility.hpp>

/**************************************************************************************************/

/**
Asynchronous one-shot results: futures and packaged tasks.

`future<T>` is the consumer side: it eventually holds a value or an exception.
`packaged_task<Args...>` is the producer side: an invocable that, when called,
runs a callable and completes its associated future with the result. You create
a task and its future together with `package<Sig>(executor, f)`. Return types
are auto-reduced: a `future<future<T>>` is flattened to `future<T>`, so
continuations and `package()` never expose nested futures.

Lifecycle and cancellation: if a `future` is destroyed before the result is
produced, the other side can observe cancellation
(`future_error_codes::broken_promise`). If a `packaged_task` is destroyed
without being invoked, its future is completed with broken_promise. Thus tasks
are effectively canceled when their future or packaged_task is destroyed. A
packaged_task can call `canceled()` to see if the future was already released.

Futures to copyable types are copyable; you can attach multiple continuations
via `then()`, `recover()`, `|`, or `^` from the same future. Futures to
non-copyable types are move-only and support a single continuation (use
`std::move(f).then(...)`). `then(f)` runs when the future has a value;
`recover(f)` runs when the future is ready (value or exception), so you can
handle errors. Continuations run on an executor (default or explicit). Use
`when_all` and `when_any` to combine futures; use `async(executor, f, args...)`
to run a function on an executor and get a future for its result. Obtain the
value with `get_ready()` (if the future is known to be ready) or `get_try()`
(returns immediately with value or empty); call `detach()` to drop the future
without cancelling the associated task.

Coroutines: a function returning `future<T>` can use `co_return` and `co_await`.
Use `co_await std::move(f)` to await a future (resumption happens in the thread
that completes the future). Use `co_await resume_on(executor, std::move(f))` to
resume the current coroutine on a specific executor when the future completes.
*/

/**************************************************************************************************/

namespace stlab {
inline namespace STLAB_VERSION_NAMESPACE() {

/**************************************************************************************************/

/// Invokes `f` with `args` and returns its result, or `std::monostate{}` if the result is void.
template <class F, class... Args>
auto invoke_void_to_monostate_result(F&& f, Args&&... args) {
    if constexpr (std::is_void_v<std::invoke_result_t<F, Args...>>) {
        std::forward<F>(f)(std::forward<Args>(args)...);
        return std::monostate{};
    } else {
        return std::forward<F>(f)(std::forward<Args>(args)...);
    }
}

/// Maps `void` to `std::monostate` for uniform future result storage; other types unchanged.
template <class T>
struct void_to_monostate {
    using type = std::conditional_t<std::is_void_v<T>, std::monostate, T>;
};

/// Alias for `void_to_monostate<T>::type`.
template <class T>
using void_to_monostate_t = typename void_to_monostate<T>::type;

/// True if T is `std::monostate`.
template <class T>
inline constexpr bool is_monostate_v = std::is_same_v<T, std::monostate>;

/// Returns `o.has_value()` when T is `std::monostate`, otherwise `std::move(o)`.
template <class T>
auto optional_monostate_to_bool(std::optional<T>&& o) {
    if constexpr (is_monostate_v<T>) {
        return o.has_value();
    } else {
        return std::move(o);
    }
}

/// Converts `std::monostate` to void (no return); forwards other types unchanged.
template <class T>
auto monostate_to_void(T&& a) {
    if constexpr (is_monostate_v<T>) {
        return;
    } else {
        return std::forward<T>(a);
    }
}

/// Converts `std::monostate` to `std::tuple{}`; wraps other types in `std::tuple` for uniform
/// application.
template <class T>
auto monostate_to_empty_tuple(T&& a) {
    if constexpr (is_monostate_v<T>) {
        return std::tuple{};
    } else {
        return std::forward_as_tuple(std::forward<T>(a));
    }
}

/// Invokes `f` with `args` after removing `std::monostate` values (for void future results).
template <class F, class... Args>
auto invoke_remove_monostate_arguments(F&& f, Args&&... args) {
    return std::apply(
        [&](auto&&... args) { return std::forward<F>(f)(std::forward<decltype(args)>(args)...); },
        std::apply(
            [&](auto&&... args) {
                return std::tuple_cat(monostate_to_empty_tuple(std::forward<Args>(args))...);
            },
            std::forward_as_tuple(std::forward<Args>(args)...)));
}

/**************************************************************************************************/

/// Error codes for future_error.
enum class future_error_codes : std::uint8_t {
    broken_promise = 1, ///< Promise was destroyed without setting a value or exception.
    no_state            ///< Operation required a valid shared state.
};

/**************************************************************************************************/

namespace detail {

inline auto Future_error_map(future_error_codes code) noexcept -> const
    char* {         // convert to name of future error
    switch (code) { // switch on error code value
        case future_error_codes::broken_promise:
            return "broken promise";

        case future_error_codes::no_state:
            return "no state";

        default:
            return nullptr;
    }
}

/**************************************************************************************************/

// This could be lifted into a common header if needed in other places
#if STLAB_CPP_VERSION_AT_LEAST(17)
template <class F, class... Args>
using result_t = std::invoke_result_t<F, Args...>;
#else
template <class F, class... Args>
using result_t = std::result_of_t<F(Args...)>;
#endif

/**************************************************************************************************/

} // namespace detail

/**************************************************************************************************/

/// Exception thrown when a future-related contract is violated (e.g. `broken_promise`, `no_state`).
class future_error : public std::logic_error {
public:
    explicit future_error(future_error_codes code) : logic_error(""), _code(code) {}

    /// The error code that caused this exception.
    [[nodiscard]] auto code() const noexcept -> const future_error_codes& { return _code; }

    /// Human-readable message for the stored error code.
    [[nodiscard]] auto what() const noexcept -> const char* override {
        return detail::Future_error_map(_code);
    }

private:
    const future_error_codes _code; // the stored error code
};

/**************************************************************************************************/

namespace detail {

/**************************************************************************************************/

template <class>
struct result_of_;

template <class R, class... Args>
struct result_of_<R(Args...)> {
    using type = R;
};

template <class F>
using result_of_t_ = typename result_of_<F>::type;

template <class F, class T>
struct result_of_when_all_t;

template <class F>
struct result_of_when_all_t<F, void> {
    using result_type = detail::result_t<F>;
};

template <class F, class T>
struct result_of_when_all_t {
    using result_type = detail::result_t<F, const std::vector<T>&>;
};

template <class F, class T>
struct result_of_when_any_t;

template <class F>
struct result_of_when_any_t<F, void> {
    using result_type = detail::result_t<F, size_t>;
};

template <class F, class R>
struct result_of_when_any_t {
    using result_type = detail::result_t<F, R, size_t>;
};

template <class T>
auto unique_usage(const std::shared_ptr<T>& p) -> bool {
    return p.use_count() == 1;
}

/**************************************************************************************************/

} // namespace detail

/**************************************************************************************************/

template <class...>
class packaged_task;

/// One-shot asynchronous result: holds a value or exception produced by a promise or packaged_task.
template <class, class = void>
class future;

/**************************************************************************************************/

namespace detail {

/**************************************************************************************************/

template <class>
struct packaged_task_from_signature;

template <class R, class... Args>
struct packaged_task_from_signature<R(Args...)> {
    using type = packaged_task<Args...>;
};

template <class T>
using packaged_task_from_signature_t = typename packaged_task_from_signature<T>::type;

/**************************************************************************************************/

template <class>
struct reduced_signature;

template <class R, class... Args>
struct reduced_signature<R(Args...)> {
    using type = R(Args...);
};

template <class R, class... Args>
struct reduced_signature<future<R>(Args...)> {
    using type = R(Args...);
};

template <class T>
using reduced_signature_t = typename reduced_signature<T>::type;

/**************************************************************************************************/

template <class T>
inline constexpr bool is_future_v = false;

template <class T>
inline constexpr bool is_future_v<future<T>> = true;

template <class T>
using reduced_t = std::conditional_t<is_future_v<T>, T, future<T>>;

template <class Sig>
using reduced_result_t = reduced_t<result_of_t_<Sig>>;

/**************************************************************************************************/

template <class T, class = void>
struct value_;

} // namespace detail

/**************************************************************************************************/

/// Creates a packaged task and its future for the callable `f`, run on `executor`.
/// @return A pair (packaged_task, future); invoke the task to run `f` and complete the future.
template <class Sig, class E, class F>
auto package(E, F&&)
    -> std::pair<detail::packaged_task_from_signature_t<Sig>, detail::reduced_result_t<Sig>>;

/**************************************************************************************************/

namespace detail {

/**************************************************************************************************/

template <class, class>
struct shared;
template <class, class = void>
struct shared_base;

/**************************************************************************************************/

template <class... Args>
struct shared_task {
    void* _co_handle{nullptr}; // storing as void* for ABI stability.

    virtual ~shared_task() {
#if STLAB_STD_COROUTINES()
        if (_co_handle) std::coroutine_handle<>::from_address(_co_handle).destroy();
#endif
    }

    virtual void operator()(Args...) = 0;
    virtual void set_exception(const std::exception_ptr&) noexcept = 0;
};

/**************************************************************************************************/

template <class T>
struct shared_base<T, enable_if_copyable<void_to_monostate_t<T>>>
    : std::enable_shared_from_this<shared_base<T>> {
    using then_t = std::vector<std::pair<executor_t, task<void() noexcept>>>;

    using type = void_to_monostate_t<T>;

    executor_t _executor;
    std::optional<type> _result;
    std::exception_ptr _exception;
    std::mutex _mutex;
    std::atomic_bool _ready{false};
    then_t _then;

    explicit shared_base(executor_t s) : _executor(std::move(s)) {}

    template <class F>
    auto recover(future<T>&& p, F&& f) {
        return recover(std::move(p), _executor, std::forward<F>(f));
    }

    template <class E, class F>
    auto recover(future<T>&& p, E executor, F&& f) {
        using result_type = detail::result_t<F, future<T>>;

        auto [pro, fut] = package<result_type()>(
            executor, [_f = std::forward<F>(f), _p = std::move(p)]() mutable {
                return std::move(_f)(std::move(_p));
            });

        bool ready{false};
        {
            std::unique_lock<std::mutex> lock(_mutex);
            ready = _ready;
            if (!ready) _then.emplace_back(std::move(executor), std::move(pro));
        }

        if (ready) executor(std::move(pro)); // cannot reference this after here

        return std::move(fut);
    }

    template <class F>
    void _detach(future<T>&& p, F&& f) {
        auto pro = [_f = std::forward<F>(f), _p = std::move(p)]() mutable noexcept {
            std::move(_f)(std::move(_p));
        };

        bool ready{false};
        {
            std::unique_lock<std::mutex> lock(_mutex);
            ready = _ready;
            if (!ready) _then.emplace_back(immediate_executor, std::move(pro));
        }

        if (ready) std::move(pro)(); // cannot reference this after here
    }

    void _detach() {
        std::unique_lock<std::mutex> lock(_mutex);
        if (!_ready)
            _then.emplace_back([](auto&&) {}, [_p = this->shared_from_this()]() noexcept {});
    }

    template <class F>
    void _on_completion(F&& f) {
        task<void() noexcept> t(std::forward<F>(f));
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (!_ready) {
                _then.emplace_back(immediate_executor, std::move(t));
                return;
            }
        }
        std::move(t)();
    }

    template <class E, class F>
    void _on_completion(E executor, F&& f) {
        task<void() noexcept> t(std::forward<F>(f));
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (!_ready) {
                _then.emplace_back(std::move(executor), std::move(t));
                return;
            }
        }
        executor(std::move(t));
    }

    void _set_exception(const std::exception_ptr& error) noexcept {
        _exception = error;
        then_t then;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            then = std::move(_then);
            _ready = true;
        }
        // propagate exception with scheduling
        for (auto& e : then) {
            e.first(std::move(e.second));
        }
    }

    template <class A>
    void set_value(A&& a);

    auto is_ready() const& -> bool { return _ready; }

    // get_ready() is called internally on continuations when we know _ready is true;
    auto get_ready() -> const type& {
        assert(is_ready() && "FATAL (sean.parent) : get_ready() called but not ready!");

        if (_exception) std::rethrow_exception(_exception);
        return *_result;
    }

    auto get_ready_r(bool unique) -> type {
        if (!unique) return get_ready();

        assert(is_ready() && "FATAL (sean.parent) : get_ready() called but not ready!");

        if (_exception) std::rethrow_exception(_exception);
        return std::move(*_result);
    }

    auto get_try() -> std::optional<type> {
        bool ready = false;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            ready = _ready;
        }
        if (ready) {
            if (_exception) std::rethrow_exception(_exception);
            return _result;
        }
        return std::nullopt;
    }

    auto get_try_r(bool unique) -> std::optional<type> {
        if (!unique) return get_try();

        bool ready = false;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            ready = _ready;
        }
        if (ready) {
            if (_exception) std::rethrow_exception(_exception);
            return std::move(_result);
        }
        return std::nullopt;
    }
};

/**************************************************************************************************/

template <class T>
struct shared_base<T, enable_if_not_copyable<void_to_monostate_t<T>>>
    : std::enable_shared_from_this<shared_base<T>> {
    using then_t = std::pair<executor_t, task<void() noexcept>>;

    executor_t _executor;
    std::optional<T> _result;
    std::exception_ptr _exception;
    std::mutex _mutex;
    std::atomic_bool _ready{false};
    then_t _then;

    explicit shared_base(executor_t s) : _executor(std::move(s)) {}

    template <class F>
    auto recover(future<T>&& p, F&& f) {
        return recover(std::move(p), _executor, std::forward<F>(f));
    }

    /*
        NOTE : executor cannot be a reference type here. When invoked it could
        cause _this_ to be deleted, and the executor passed in may be
        this->_executor
    */
    template <class E, class F>
    auto recover(future<T>&& p, E executor, F&& f) {
        using result_type = detail::result_t<F, future<T>>;

        auto [pro, fut] = package<result_type()>(
            executor, [_f = std::forward<F>(f), _p = std::move(p)]() mutable {
                return std::move(_f)(std::move(_p));
            });

        bool ready{false};
        {
            std::unique_lock<std::mutex> lock(_mutex);
            ready = _ready;
            if (!ready) {
                assert(!_then.second && "recover: _then slot already occupied");
                _then = {std::move(executor), std::move(pro)};
            }
        }
        if (ready) executor(std::move(pro)); // cannot reference this after here

        return std::move(fut);
    }

    template <class F>
    void _detach(future<T>&& p, F&& f) {
        auto pro = [_f = std::forward<F>(f), _p = std::move(p)]() mutable noexcept {
            std::move(_f)(std::move(_p));
        };

        bool ready{false};
        {
            std::unique_lock<std::mutex> lock(_mutex);
            ready = _ready;
            if (!ready) _then = {immediate_executor, std::move(pro)};
        }

        if (ready) std::move(pro)(); // cannot reference this after here
    }

    void _detach() {
        std::unique_lock<std::mutex> lock(_mutex);
        if (!_ready) _then = then_t([](auto&&) {}, [_p = this->shared_from_this()]() noexcept {});
    }

    template <class F>
    void _on_completion(F&& f) {
        task<void() noexcept> t(std::forward<F>(f));
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (!_ready) {
                assert(!_then.second && "on_completion: _then slot already occupied");
                _then = {immediate_executor, std::move(t)};
                return;
            }
        }
        std::move(t)();
    }

    template <class E, class F>
    void _on_completion(E executor, F&& f) {
        task<void() noexcept> t(std::forward<F>(f));
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (!_ready) {
                assert(!_then.second && "on_completion: _then slot already occupied");
                _then = {std::move(executor), std::move(t)};
                return;
            }
        }
        executor(std::move(t));
    }

    void _set_exception(const std::exception_ptr& error) noexcept {
        _exception = error;
        then_t then;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (_then.second) then = std::move(_then);
            _ready = true;
        }
        // propagate exception with scheduling
        if (then.second) then.first(std::move(then.second));
    }

    template <class A>
    void set_value(A&& a);

    auto is_ready() const -> bool {
        return _ready;
    } // get_ready() is called internally on continuations when we know _ready is true;

    auto get_ready() -> const T& { return get_ready_r(true); }

    auto get_ready_r(bool) -> T {
        assert(is_ready() && "FATAL (sean.parent) : get_ready() called but not ready!");

        if (_exception) std::rethrow_exception(_exception);
        return std::move(*_result);
    }

    auto get_try() -> std::optional<T> { return get_try_r(true); }

    auto get_try_r(bool) -> std::optional<T> {
        bool ready = false;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            ready = _ready;
        }
        if (ready) {
            if (_exception) std::rethrow_exception(_exception);
            return std::move(_result);
        }
        return {};
    }
};

/**************************************************************************************************/

/// Producer side of a one-shot result; setting a value or exception completes the associated
/// future.
template <class R>
class promise {
    using type = void_to_monostate_t<R>;
    std::weak_ptr<shared_base<R>> _p;

public:
    explicit promise(const std::shared_ptr<shared_base<R>>& p) : _p{p} {}

    ~promise() {
        if (auto p = _p.lock()) {
            p->_set_exception(
                std::make_exception_ptr(future_error(future_error_codes::broken_promise)));
        }
    }

    promise(promise&&) noexcept = default;
    auto operator=(promise&&) noexcept -> promise& = default;

    promise(const promise&) = delete;
    auto operator=(const promise&) -> promise& = delete;

    /// Completes the future with `value`. No effect if the future was already satisfied or
    /// canceled.
    void set_value(type&& value) && noexcept {
        if (auto p = _p.lock()) {
            _p.reset();
            p->set_value(std::move(value));
        }
    }

    /// Completes the future (void result). No effect if already satisfied or canceled.
    auto set_value() && noexcept { set_value(std::monostate{}); }

    /// Completes the future with `error`. No effect if already satisfied or canceled.
    void set_exception(const std::exception_ptr& error) && noexcept {
        if (auto p = _p.lock()) {
            _p.reset();
            p->_set_exception(error);
        }
    }

    /// True if the associated future was released (e.g. consumer no longer interested).
    [[nodiscard]] auto canceled() const -> bool { return _p.expired(); }
};

/// Deduction guide for promise from `shared_ptr<shared_base<R>>`.
template <class R>
promise(std::shared_ptr<shared_base<R>>) -> promise<R>;

template <class F, class R, class... Args>
struct shared<F, R(Args...)> final : shared_base<R>, shared_task<Args...> {
    std::optional<F> _f;

    shared(executor_t s, F&& f) : shared_base<R>(std::move(s)), _f(std::move(f)) {}

    void operator()(Args... args) noexcept override {
        std::move (*_f)(promise{this->shared_from_this()}, std::move(args)...);
        // After invoking `_f`, it is not destructed because it could be satisfying the promise
        // asynchronously. `_f` is responsible for any cleanup prior to the future being released
        // and must be aware that it may be destructed at any time after this point.
    }

    void set_exception(const std::exception_ptr& error) noexcept override {
        shared_base<R>::_set_exception(error);
        _f.reset(); // _f will not be invoked, release it and any resources it holds
    }
};

template <class Sig, class F>
auto make_shared_state(executor_t s, F&& f) -> std::shared_ptr<shared<F, Sig>> {
    return std::make_shared<shared<F, Sig>>(std::move(s), std::forward<F>(f));
}

template <class... Args>
[[nodiscard]] auto weak_state(const packaged_task<Args...>& p)
    -> std::weak_ptr<shared_task<Args...>>;

/**************************************************************************************************/

} // namespace detail

/**************************************************************************************************/

/// Invocable that completes a future when called with arguments of type `Args...`; created by
/// `package()`.
template <class... Args>
class packaged_task {
    using ptr_t = std::weak_ptr<detail::shared_task<Args...>>;
    ptr_t _p;

    explicit packaged_task(ptr_t&& p) : _p(std::move(p)) {}

    template <class Sig, class E, class F>
    friend auto package(E, F&&)
        -> std::pair<detail::packaged_task_from_signature_t<Sig>, detail::reduced_result_t<Sig>>;

    template <class T, class E>
    friend auto future_with_broken_promise(E) -> detail::reduced_t<T>;

    template <class... Brgs>
    friend auto detail::weak_state(const packaged_task<Brgs...>& p)
        -> std::weak_ptr<detail::shared_task<Brgs...>>;

public:
    packaged_task() = default;
    ~packaged_task() {
        if (auto p = _p.lock()) {
            p->set_exception(
                std::make_exception_ptr(future_error(future_error_codes::broken_promise)));
        }
    }

    packaged_task(const packaged_task&) = delete;
    auto operator=(const packaged_task&) -> packaged_task& = delete;

    packaged_task(packaged_task&&) noexcept = default;
    auto operator=(packaged_task&& x) noexcept -> packaged_task& = default;

    /// Invokes the packaged callable with `args` and completes the associated future.
    /// Clears _co_handle before reset so ~shared_task() will not destroy the coroutine; the
    /// coroutine is destroyed in final_awaiter::await_suspend (coroutine is suspended there).
    template <class... A>
    void operator()(A&&... args) noexcept {
        if (auto p = _p.lock()) {
            p->_co_handle = nullptr;
            _p.reset();
            (*p)(std::forward<A>(args)...);
        }
    }

    /// True if the associated future was released.
    [[nodiscard]] auto canceled() const -> bool { return _p.expired(); }

    /// Completes the future with `error` without invoking the callable.
    void set_exception(const std::exception_ptr& error) noexcept {
        if (auto p = _p.lock()) {
            p->_co_handle = nullptr;
            _p.reset();
            p->set_exception(error);
        }
    }
};

namespace detail {

template <class... Args>
[[nodiscard]] auto weak_state(const packaged_task<Args...>& p)
    -> std::weak_ptr<detail::shared_task<Args...>> {
    return p._p;
}

} // namespace detail

/**************************************************************************************************/

/// Consumer side of a one-shot result (copyable T). Use `get_ready()` or `get_try()` to obtain the
/// value.
template <class T>
class STLAB_NODISCARD() future<T, enable_if_copyable<void_to_monostate_t<T>>> {
    using type = void_to_monostate_t<T>;
    using ptr_t = std::shared_ptr<detail::shared_base<T>>;
    ptr_t _p;

    explicit future(ptr_t p) : _p(std::move(p)) {}

    template <class Sig, class E, class F>
    friend auto package(E, F&&)
        -> std::pair<detail::packaged_task_from_signature_t<Sig>, detail::reduced_result_t<Sig>>;

    template <class U, class E>
    friend auto future_with_broken_promise(E) -> detail::reduced_t<U>;

    friend struct detail::shared_base<type>;

    template <class, class>
    friend struct detail::value_;

public:
    /// The type of the value this future holds.
    using result_type = T;

    future() = default;
    future(const future&) = default;
    future(future&&) noexcept = default;
    auto operator=(const future&) -> future& = default;
    auto operator=(future&&) noexcept -> future& = default;

    /// Exchanges the shared state with `x`.
    void swap(future& x) noexcept { std::swap(_p, x._p); }

    /// Exchanges the shared states of `x` and `y`.
    inline friend void swap(future& x, future& y) noexcept { x.swap(y); }
    /// True if `x` and `y` share the same shared state.
    inline friend auto operator==(const future& x, const future& y) -> bool { return x._p == y._p; }
    /// True if `x` and `y` do not share the same shared state.
    inline friend auto operator!=(const future& x, const future& y) -> bool { return !(x == y); }

    /// True if this future has an associated shared state.
    [[nodiscard]] auto valid() const -> bool { return static_cast<bool>(_p); }

    /// Returns a future that completes with the result of `f` applied to this future's value
    /// (default executor).
    template <class F>
    auto then(F&& f) const& {
        return recover([_f = std::forward<F>(f)](future<result_type>&& p) mutable {
            return invoke_remove_monostate_arguments(
                std::move(_f),
                invoke_void_to_monostate_result([&] { return std::move(p).get_ready(); }));
        });
    }

    /// Pipe operator: same as `then(f)`.
    template <class F>
    auto operator|(F&& f) const& {
        return then(std::forward<F>(f));
    }

    /// Returns a future that completes with the result of `f` on `executor` applied to this
    /// future's value.
    template <class E, class F>
    auto then(E&& executor, F&& f) const& {
        return recover(
            std::forward<E>(executor), [_f = std::forward<F>(f)](future<result_type>&& p) mutable {
                return invoke_remove_monostate_arguments(
                    std::move(_f),
                    invoke_void_to_monostate_result([&] { return std::move(p).get_ready(); }));
            });
    }

    /// Pipe operator: same as `then(etp.executor(), etp.task())`.
    template <class F>
    auto operator|(executor_task_pair<F> etp) const& {
        return then(std::move(etp)._executor, std::move(etp)._f);
    }

    /// Returns a future that completes with the result of `f` applied to this future's value
    /// (default executor). Rvalue overload; consumes `*this`.
    template <class F>
    auto then(F&& f) && {
        return std::move(*this).recover([_f = std::forward<F>(f)](future<result_type>&& p) mutable {
            return invoke_remove_monostate_arguments(
                std::move(_f),
                invoke_void_to_monostate_result([&] { return std::move(p).get_ready(); }));
        });
    }

    /// Pipe operator: same as `then(f)`. Rvalue overload; consumes `*this`.
    template <class F>
    auto operator|(F&& f) && {
        return std::move(*this).then(std::forward<F>(f));
    }

    /// Returns a future that completes with the result of `f` on `executor` applied to this
    /// future's value. Rvalue overload; consumes `*this`.
    template <class E, class F>
    auto then(E&& executor, F&& f) && {
        return std::move(*this).recover(
            std::forward<E>(executor), [_f = std::forward<F>(f)](future<result_type>&& p) mutable {
                return invoke_remove_monostate_arguments(
                    std::move(_f),
                    invoke_void_to_monostate_result([&] { return std::move(p).get_ready(); }));
            });
    }

    /// Pipe operator: same as `then(etp.executor(), etp.task())`. Rvalue overload; consumes
    /// `*this`.
    template <class F>
    auto operator|(executor_task_pair<F> etp) && {
        return std::move(*this).then(std::move(etp)._executor, std::move(etp)._f);
    }

    /// Returns a future that completes with the result of `f` given this future (possibly in error
    /// state); default executor.
    template <class F>
    auto recover(F&& f) const& {
        return _p->recover(copy(*this), std::forward<F>(f));
    }

    /// Pipe operator: same as `recover(f)`.
    template <class F>
    auto operator^(F&& f) const& {
        return recover(std::forward<F>(f));
    }

    /// Returns a future that completes with the result of `f` given this future, run on `executor`.
    template <class E, class F>
    auto recover(E&& executor, F&& f) const& {
        return _p->recover(copy(*this), std::forward<E>(executor), std::forward<F>(f));
    }

    /// Pipe operator: same as `recover(etp.executor(), etp.task())`.
    template <class F>
    auto operator^(executor_task_pair<F> etp) const& {
        return recover(std::move(etp)._executor, std::move(etp)._f);
    }

    /// Returns a future that completes with the result of `f` given this future (possibly in error
    /// state); default executor. Rvalue overload; consumes `*this`.
    template <class F>
    auto recover(F&& f) && {
        auto& self = *_p.get();
        return self.recover(std::move(*this), std::forward<F>(f));
    }

    /// Pipe operator: same as `recover(f)`. Rvalue overload; consumes `*this`.
    template <class F>
    auto operator^(F&& f) && {
        return std::move(*this).recover(std::forward<F>(f));
    }

    /// Returns a future that completes with the result of `f` given this future, run on `executor`.
    /// Rvalue overload; consumes `*this`.
    template <class E, class F>
    auto recover(E&& executor, F&& f) && {
        auto& self = *_p.get();
        return self.recover(std::move(*this), std::forward<E>(executor), std::forward<F>(f));
    }

    /// Pipe operator: same as `recover(etp.executor(), etp.task())`. Rvalue overload; consumes
    /// `*this`.
    template <class F>
    auto operator^(executor_task_pair<F> etp) && {
        return std::move(*this).recover(std::move(etp)._executor, std::move(etp)._f);
    }

    /// Drops this future without requiring a value; the promise may see `broken_promise`.
    void detach() const { _p->_detach(); }

    /// When this future completes (value or exception), invokes `f` with it and does not propagate
    /// further.
    template <class F>
    void detach(F&& f) && {
        auto& self = *_p.get();
        self._detach(std::move(*this), std::forward<F>(f));
    }

    /// Invokes `f` immediately when this future completes (value or exception).
    /// Requires `f` is noexcept.
    template <class F>
    void on_completion(F&& f) {
        _p->_on_completion(std::forward<F>(f));
    }

    /// Invokes `f` when this future completes (value or exception), on `executor`.
    /// Requires `f` is noexcept.
    template <class E, class F>
    void on_completion(E&& executor, F&& f) {
        _p->_on_completion(std::forward<E>(executor), std::forward<F>(f));
    }

    /// Releases the shared state; `valid()` becomes false.
    void reset() noexcept { _p.reset(); }

    /// True if the result or exception is available.
    [[nodiscard]] auto is_ready() const& -> bool { return _p && _p->is_ready(); }

    /// Returns the value if ready, or `std::nullopt` / false (for void) if not; rethrows if
    /// completed with exception.
    [[nodiscard]] auto get_try() const& { return optional_monostate_to_bool(_p->get_try()); }

    /// Same as `get_try()` but may move the value when this is the only reference.
    auto get_try() && { return optional_monostate_to_bool(_p->get_try_r(unique_usage(_p))); }

    /// Returns the value. @pre `is_ready()`. Rethrows the stored exception if the future failed.
    [[nodiscard]] auto get_ready() const& { return monostate_to_void(_p->get_ready()); }

    /// Same as `get_ready()` but may move the value when this is the only reference.
    auto get_ready() && { return monostate_to_void(_p->get_ready_r(unique_usage(_p))); }

    [[deprecated("Use exception() instead")]] [[nodiscard]] auto error()
        const& -> std::optional<std::exception_ptr> {
        return _p->_exception ? std::optional<std::exception_ptr>{_p->_exception} : std::nullopt;
    }

    /// Returns the stored exception, or a null `exception_ptr` if the future completed with a
    /// value. @pre `is_ready()`
    [[nodiscard]] auto exception() const& -> std::exception_ptr {
        assert(is_ready());
        return _p->_exception;
    }
};

/**************************************************************************************************/

/// Consumer side of a one-shot result (non-copyable T). Use `get_ready()` or `get_try()`;
/// `then`/`recover` only on rvalue.
template <class T>
class STLAB_NODISCARD() future<T, enable_if_not_copyable<void_to_monostate_t<T>>> {
    using ptr_t = std::shared_ptr<detail::shared_base<T>>;
    ptr_t _p;

    explicit future(ptr_t p) : _p(std::move(p)) {}

    template <class Sig, class E, class F>
    friend auto package(E, F&&)
        -> std::pair<detail::packaged_task_from_signature_t<Sig>, detail::reduced_result_t<Sig>>;

    template <class U, class E>
    friend auto future_with_broken_promise(E) -> detail::reduced_t<U>;

    friend struct detail::shared_base<T>;

    template <class, class>
    friend struct detail::value_;

public:
    /// The type of the value this future holds.
    using result_type = T;

    future() = default;
    future(const future&) = delete;
    future(future&&) noexcept = default;
    auto operator=(const future&) -> future& = delete;
    auto operator=(future&&) noexcept -> future& = default;

    /// Exchanges the shared state with `x`.
    void swap(future& x) noexcept { std::swap(_p, x._p); }

    /// Exchanges the shared states of `x` and `y`.
    inline friend void swap(future& x, future& y) noexcept { x.swap(y); }
    /// True if `x` and `y` share the same shared state.
    inline friend auto operator==(const future& x, const future& y) -> bool { return x._p == y._p; }
    /// True if `x` and `y` do not share the same shared state.
    inline friend auto operator!=(const future& x, const future& y) -> bool { return !(x == y); }

    /// True if this future has an associated shared state.
    [[nodiscard]] auto valid() const -> bool { return static_cast<bool>(_p); }

    /// Returns a future that completes with the result of `f` applied to this future's value
    /// (rvalue only).
    template <class F>
    auto then(F&& f) && {
        return std::move(*this).recover([_f = std::forward<F>(f)](future<result_type>&& p) mutable {
            return std::move(_f)(*std::move(p).get_try());
        });
    }

    /// Pipe operator: same as `then(f)`.
    template <class F>
    auto operator|(F&& f) && {
        return std::move(*this).then(std::forward<F>(f));
    }

    /// Returns a future that completes with the result of `f` on `executor` applied to this
    /// future's value.
    template <class E, class F>
    auto then(E&& executor, F&& f) && {
        return std::move(*this).recover(std::forward<E>(executor),
                                        [_f = std::forward<F>(f)](future<result_type>&& p) mutable {
                                            return std::move(_f)(*std::move(p).get_try());
                                        });
    }

    /// Pipe operator: same as `then(etp.executor(), etp.task())`.
    template <class F>
    auto operator|(executor_task_pair<F> etp) && {
        return std::move(*this).then(std::move(etp)._executor, std::move(etp)._f);
    }

    /// Returns a future that completes with the result of `f` given this future (possibly in error
    /// state); default executor.
    template <class F>
    auto recover(F&& f) && {
        auto& self = *_p.get();
        return self.recover(std::move(*this), std::forward<F>(f));
    }

    /// Pipe operator: same as `recover(f)`.
    template <class F>
    auto operator^(F&& f) && {
        return std::move(*this).recover(std::forward<F>(f));
    }

    /// Returns a future that completes with the result of `f` given this future, run on `executor`.
    template <class E, class F>
    auto recover(E&& executor, F&& f) && {
        auto& self = *_p.get();
        return self.recover(std::move(*this), std::forward<E>(executor), std::forward<F>(f));
    }

    /// Pipe operator: same as `recover(etp.executor(), etp.task())`.
    template <class F>
    auto operator^(executor_task_pair<F> etp) && {
        return std::move(*this).recover(std::move(etp)._executor, std::move(etp)._f);
    }

    /// Drops this future without requiring a value; the promise may see `broken_promise`.
    void detach() const { _p->_detach(); }

    /// When this future completes (value or exception), invokes `f` with it and does not propagate
    /// further.
    template <class F>
    void detach(F&& f) && {
        auto& self = *_p.get();
        self._detach(std::move(*this), std::forward<F>(f));
    }

    /// Invokes `f` immediately when this future completes (value or exception).
    /// This consumes the continuation slot, the behavior of attaching a
    /// continuation after on_completion() is undefined.
    template <class F>
    void on_completion(F&& f) {
        _p->_on_completion(std::forward<F>(f));
    }

    /// Invokes `f` when this future completes (value or exception), on `executor`.
    /// This consumes the continuation slot, the behavior of attaching a
    /// continuation after on_completion() is undefined.
    template <class E, class F>
    void on_completion(E&& executor, F&& f) {
        _p->_on_completion(std::forward<E>(executor), std::forward<F>(f));
    }

    /// Releases the shared state; `valid()` becomes false.
    void reset() noexcept { _p.reset(); }

    /// True if the result or exception is available.
    [[nodiscard]] auto is_ready() const& -> bool { return _p && _p->is_ready(); }

    /// Returns the value if ready, or `std::nullopt` / false (for void) if not; rethrows if
    /// completed with exception.
    [[nodiscard]] auto get_try() const& { return optional_monostate_to_bool(_p->get_try()); }

    /// Same as `get_try()` but may move the value when this is the only reference.
    auto get_try() && { return optional_monostate_to_bool(_p->get_try_r(unique_usage(_p))); }

    /// Returns the value. @pre `is_ready()`. Rethrows the stored exception if the future failed.
    [[nodiscard]] auto get_ready() const& { return monostate_to_void(_p->get_ready()); }

    /// Same as `get_ready()` but may move the value when this is the only reference.
    auto get_ready() && { return monostate_to_void(_p->get_ready_r(unique_usage(_p))); }

    [[deprecated("Use exception() instead")]] [[nodiscard]] auto error()
        const& -> std::optional<std::exception_ptr> {
        return _p->_exception ? std::optional<std::exception_ptr>{_p->_exception} : std::nullopt;
    }

    /// Returns the stored exception, or a null `exception_ptr` if the future completed with a
    /// value. @pre `is_ready()`
    [[nodiscard]] auto exception() const& -> std::exception_ptr {
        assert(is_ready());
        return _p->_exception;
    }
};

template <class Sig, class E, class F>
auto package(E executor, F&& f)
    -> std::pair<detail::packaged_task_from_signature_t<Sig>, detail::reduced_result_t<Sig>> {
    if constexpr (std::is_same_v<E, executor_t>) {
        assert(executor && "FATAL (sean.parent) : executor is null!");
    }

    using result_t = detail::result_of_t_<Sig>;

    if constexpr (detail::is_future_v<result_t>) {
        auto p = detail::make_shared_state<detail::reduced_signature_t<Sig>>(
            std::move(executor),
            [_f = std::make_optional(std::forward<F>(f)),
             _hold = future<void>{}](auto&& promise, auto&&... args) mutable noexcept {
                assert(_f && "packaged task invoked twice");
                try {
                    auto r = std::move(*_f)(std::forward<decltype(args)>(args)...);
                    try {
                        _hold = std::move(r).recover(
                            immediate_executor,
                            [_p = std::move(promise)](result_t&& f) mutable noexcept {
                                if (auto e = f.exception()) {
                                    std::move(_p).set_exception(std::move(e));
                                } else {
                                    std::move(_p).set_value(invoke_void_to_monostate_result(
                                        [&] { return std::move(f).get_ready(); }));
                                }
                            });
                    } catch (...) {
                        /* NOTE: an exception here is reported as a broken promise. Ideally recover
                         * would be passed the initial promise (it flows through the chain), but the
                         * API isn't there yet. */
                    }
                } catch (...) {
                    std::move(promise).set_exception(std::current_exception());
                }
                _f.reset();
            });
        return {detail::packaged_task_from_signature_t<Sig>{p}, result_t{p}};
    } else {
        auto p = detail::make_shared_state<Sig>(
            std::move(executor), [_f = std::make_optional(std::forward<F>(f))](
                                     auto&& promise, auto&&... args) mutable noexcept {
                assert(_f && "packaged task invoked twice");
                try {
                    auto tmp = invoke_void_to_monostate_result(
                        std::move(*_f), std::forward<decltype(args)>(args)...);
                    std::move(promise).set_value(std::move(tmp)); // noexcept
                } catch (...) {
                    std::move(promise).set_exception(std::current_exception());
                }
                _f.reset();
            });
        return {detail::packaged_task_from_signature_t<Sig>{p}, future<result_t>{p}};
    }
}

/// Returns a future of type T that is already ready with a `broken_promise` error (e.g. for
/// canceled work).
template <class T, class E>
auto future_with_broken_promise(E executor) -> detail::reduced_t<T> {
    auto p = std::make_shared<detail::shared_base<typename detail::reduced_t<T>::result_type>>(
        std::move(executor));
    p->_exception = std::make_exception_ptr(future_error(future_error_codes::broken_promise));
    p->_ready = true;

    return detail::reduced_t<T>{p};
}

/**************************************************************************************************/

namespace detail {

template <class F>
struct assign_ready_future {
    template <class T>
    static void assign(T& x, F&& f) {
        x = std::move(*(std::move(f).get_try()));
    }
};

template <>
struct assign_ready_future<future<void>> {
    template <class T>
    static void assign(T& x, const future<void>&) {
        x = std::move(typename T::value_type()); // to set the optional
    }
};

template <class F, class Args>
struct when_all_shared {
    // decay
    Args _args;
    std::mutex _guard;
    std::array<future<void>, std::tuple_size_v<Args>> _holds;
    std::size_t _remaining{std::tuple_size_v<Args>};
    std::exception_ptr _exception;
    packaged_task<> _f;

    // require f is sink.
    template <std::size_t index, class FF>
    auto done(FF&& f) -> std::enable_if_t<!std::is_lvalue_reference_v<FF>> {
        auto run{false};
        {
            std::unique_lock<std::mutex> lock{_guard};
            if (!_exception) {
                assign_ready_future<FF>::assign(std::get<index>(_args), std::move(f));
                if (--_remaining == 0) run = true;
            }
        }
        if (run) _f();
    }

    void failure(const std::exception_ptr& error) {
        auto run{false};
        {
            std::unique_lock<std::mutex> lock{_guard};
            if (!_exception) {
                for (auto& h : _holds)
                    h.reset();
                _exception = error;
                run = true;
            }
        }
        if (run) _f();
    }
};

template <size_t S, class R>
struct when_any_shared {
    using result_type = R;
    // decay
    std::optional<R> _arg;
    std::mutex _guard;
    std::array<future<void>, S> _holds;
    std::size_t _remaining{S};
    std::exception_ptr _exception;
    std::size_t _index = std::numeric_limits<std::size_t>::max();
    packaged_task<> _f;

    void failure(const std::exception_ptr& error) {
        auto run{false};
        {
            std::unique_lock<std::mutex> lock{_guard};
            if (--_remaining == 0) {
                _exception = error;
                run = true;
            }
        }
        if (run) _f();
    }

    template <size_t index, class FF>
    void done(FF&& f) {
        auto run{false};
        {
            std::unique_lock<std::mutex> lock{_guard};
            if (_index == std::numeric_limits<std::size_t>::max()) {
                _arg = std::move(*std::forward<FF>(f).get_try());
                _index = index;
                run = true;
            }
        }
        if (run) _f();
    }

    template <class F>
    auto apply(F& f) {
        return f(std::move(*_arg), _index);
    }
};

template <size_t S>
struct when_any_shared<S, void> {
    using result_type = void;
    // decay
    std::mutex _guard;
    std::array<future<void>, S> _holds;
    std::size_t _remaining{S};
    std::exception_ptr _exception;
    std::size_t _index = std::numeric_limits<std::size_t>::max();
    packaged_task<> _f;

    void failure(const std::exception_ptr& error) {
        auto run{false};
        std::unique_lock<std::mutex> lock{_guard};
        {
            if (--_remaining == 0) {
                _exception = error;
                run = true;
            }
        }
        if (run) _f();
    }

    template <size_t index, class FF>
    void done(FF&&) {
        auto run{false};
        {
            std::unique_lock<std::mutex> lock{_guard};
            if (_index == std::numeric_limits<std::size_t>::max()) {
                _index = index;
                run = true;
            }
        }
        if (run) _f();
    }

    template <class F>
    auto apply(F& f) {
        return f(_index);
    }
};

inline void rethrow_if_false(bool x, const std::exception_ptr& p) {
    if (!x) std::rethrow_exception(p);
}

template <class F, class Args, class P, std::size_t... I>
auto apply_when_all_args_(F& f, Args& args, P& p, std::index_sequence<I...>) {
    (void)std::initializer_list<int>{
        (rethrow_if_false(static_cast<bool>(std::get<I>(args)), p->_exception), 0)...};
    return apply_optional_indexed<
        index_sequence_transform_t<std::make_index_sequence<std::tuple_size_v<Args>>,
                                   remove_placeholder<Args>::template function>>(f, args);
}

template <class F, class P>
auto apply_when_all_args(F& f, P& p) {
    return apply_when_all_args_(f, p->_args, p,
                                std::make_index_sequence<std::tuple_size_v<decltype(p->_args)>>());
}

template <class F, class P>
auto apply_when_any_arg(F& f, P& p) {
    if (p->_exception) {
        std::rethrow_exception(p->_exception);
    }

    return p->apply(f);
}

template <std::size_t i, class E, class P, class T>
void attach_when_arg_(E&& executor, std::shared_ptr<P>& shared, T a) {
    auto holds =
        std::move(a).recover(std::forward<E>(executor), [_w = std::weak_ptr<P>(shared)](auto&& x) {
            auto p = _w.lock();
            if (!p) return;

            if (auto ex = x.exception()) {
                p->failure(std::move(ex));
            } else {
                p->template done<i>(std::move(x));
            }
        });
    std::unique_lock<std::mutex> lock{shared->_guard};
    shared->_holds[i] = std::move(holds);
}

template <class E, class P, class... Ts, std::size_t... I>
void attach_when_args_(std::index_sequence<I...>, E&& executor, std::shared_ptr<P>& p, Ts... a) {
    (void)std::initializer_list<int>{
        (attach_when_arg_<I>(std::forward<E>(executor), p, std::move(a)), 0)...};
}

template <class E, class P, class... Ts>
void attach_when_args(E&& executor, std::shared_ptr<P>& p, Ts... a) {
    attach_when_args_(std::make_index_sequence<sizeof...(Ts)>(), std::forward<E>(executor), p,
                      std::move(a)...);
}

} // namespace detail

/**************************************************************************************************/

/// Returns a future that completes when all `args` are ready; `f` is invoked with their values (or
/// first exception).
template <class E, class F, class... Ts>
auto when_all(const E& executor, F f, future<Ts>... args) {
    using vt_t = voidless_tuple<Ts...>;
    using opt_t = optional_placeholder_tuple<Ts...>;
    using result_t = decltype(apply_ignore_placeholders(std::declval<F>(), std::declval<vt_t>()));

    auto shared = std::make_shared<detail::when_all_shared<F, opt_t>>();
    auto p = package<result_t()>(
        executor, [_f = std::move(f), _p = shared] { return detail::apply_when_all_args(_f, _p); });
    shared->_f = std::move(p.first);

    detail::attach_when_args(executor, shared, std::move(args)...);

    return std::move(p.second);
}

/**************************************************************************************************/

/// Helper to implement when_any for result type T.
template <class T>
struct make_when_any {
    template <class E, class F, class... Ts>
    static auto make(const E& executor, F f, future<T> arg, future<Ts>... args) {
        using result_t = detail::result_t<F, T, size_t>;

        auto shared = std::make_shared<detail::when_any_shared<sizeof...(Ts) + 1, T>>();
        auto p = package<result_t()>(executor, [_f = std::move(f), _p = shared]() mutable {
            return detail::apply_when_any_arg(_f, _p);
        });
        shared->_f = std::move(p.first);

        detail::attach_when_args(executor, shared, std::move(arg), std::move(args)...);

        return std::move(p.second);
    }
};

/**************************************************************************************************/

/// Helper to implement when_any for void results.
template <>
struct make_when_any<void> {
    template <class E, class F, class... Ts>
    static auto make(E&& executor, F&& f, future<Ts>... args) {
        using result_t = detail::result_t<F, size_t>;

        auto shared = std::make_shared<detail::when_any_shared<sizeof...(Ts), void>>();
        auto p = package<result_t()>(executor, [_f = std::forward<F>(f), _p = shared]() mutable {
            return detail::apply_when_any_arg(_f, _p);
        });
        shared->_f = std::move(p.first);

        detail::attach_when_args(std::forward<E>(executor), shared, std::move(args)...);

        return std::move(p.second);
    }
};

/**************************************************************************************************/

/// Returns a future that completes when any of the given futures is ready; `f` receives the value
/// and the index of the future that completed first (as a second argument of type `std::size_t`).
template <class E, class F, class T, class... Ts>
auto when_any(E&& executor, F&& f, future<T>&& arg, future<Ts>&&... args) {
    return make_when_any<T>::make(std::forward<E>(executor), std::forward<F>(f), std::move(arg),
                                  std::move(args)...);
}

/**************************************************************************************************/

namespace detail {
template <class T>
struct value_storer {
    template <class C, class F>
    static void store(C& context, F&& f, std::size_t index) {
        context._results = std::move(*std::forward<F>(f).get_try());
        context._index = index;
    }
};

template <class T>
struct value_storer<std::vector<T>> {
    template <class C, class F>
    static void store(C& context, F&& f, std::size_t index) {
        context._results[index] = std::move(*std::forward<F>(f).get_try());
    }
};

template <bool Indexed, class R>
struct result_creator;

template <>
struct result_creator<true, void> {
    template <class C>
    static auto go(C& context) {
        return context._f(context._index);
    }
};

template <>
struct result_creator<false, void> {
    template <class C>
    static auto go(C& context) {
        return context._f();
    }
};

template <class R>
struct result_creator<true, R> {
    template <class C>
    static auto go(C& context) {
        return context._f(std::move(context._results), context._index);
    }
};

template <class R>
struct result_creator<false, R> {
    template <class C>
    static auto go(C& context) {
        return context._f(std::move(context._results));
    }
};

template <class F, bool Indexed, class R>
struct context_result {
    using result_type = R;

    R _results;
    std::exception_ptr _exception;
    std::size_t _index{0};
    F _f;

    context_result(F f, std::size_t s) : _f(std::move(f)) { init(_results, s); }

    template <class T>
    void init(std::vector<T>& v, std::size_t s) {
        v.resize(s);
    }

    template <class T>
    void init(T&, std::size_t) {}

    template <class FF>
    void apply(FF&& f, std::size_t index) {
        value_storer<R>::store(*this, std::forward<FF>(f), index);
    }

    void apply(const std::exception_ptr& error, std::size_t) { _exception = error; }

    auto operator()() { return result_creator<Indexed, R>::go(*this); }
};

template <class F, bool Indexed>
struct context_result<F, Indexed, void> {
    std::exception_ptr _exception;
    std::size_t _index{0};
    F _f;

    context_result(F f, std::size_t) : _f(std::move(f)) {}

    template <class FF>
    void apply(FF&&, std::size_t index) {
        _index = index;
    }

    void apply(const std::exception_ptr& error, std::size_t) { _exception = error; }

    auto operator()() { return result_creator<Indexed, void>::go(*this); }
};

/**************************************************************************************************/

/*
 * This specialization is used for cases when only one ready future is enough to move forward.
 * In case of when_any, the first successful future triggers the continuation. All others are
 * cancelled. In case of when_all, after the first error, this future cannot be fulfilled
 * anymore and so we cancel the all the others.
 */
struct single_trigger {
    template <class C, class F>
    static auto go(C& context, F&& f, size_t index) -> bool {
        auto run{false};
        {
            std::unique_lock<std::mutex> lock{context._guard};
            if (!context._single_event) {
                for (auto i = 0u; i < context._holds.size(); ++i) {
                    if (i != index) context._holds[i].reset();
                }
                context._single_event = true;
                context.apply(std::forward<F>(f), index);
                run = true;
            }
        }
        return run;
    }
};

/*
 * This specialization is used for cases when all futures must be fulfilled before the
 * continuation is triggered. In case of when_any it means, that the error case handling is
 * started, because all futures failed. In case of when_all it means, that after all futures
 * were fulfilled, the continuation is started.
 */
struct all_trigger {
    template <class C, class F>
    static auto go(C& context, F&& f, size_t index) -> bool {
        auto run{false};
        {
            std::unique_lock<std::mutex> lock{context._guard};
            context.apply(std::forward<F>(f), index);
            if (--context._remaining == 0) run = true;
        }
        return run;
    }

    template <class C>
    static auto go(C& context, const std::exception_ptr& error, size_t index) -> bool {
        auto run{false};
        {
            std::unique_lock<std::mutex> lock{context._guard};
            if (--context._remaining == 0) {
                context.apply(error, index);
                run = true;
            }
        }
        return run;
    }
};

template <class CR, class F, class ResultCollector, class FailureCollector>
struct common_context : CR {
    std::mutex _guard;
    std::size_t _remaining;
    bool _single_event{false};
    std::vector<future<void>> _holds;
    packaged_task<> _f;

    common_context(F f, size_t s) : CR(std::move(f), s), _remaining(s), _holds(_remaining) {}

    auto execute() {
        if (this->_exception) {
            std::rethrow_exception(this->_exception);
        }
        return CR::operator()();
    }

    void failure(const std::exception_ptr& error, size_t index) {
        if (FailureCollector::go(*this, error, index)) _f();
    }

    template <class FF>
    void done(FF&& f, size_t index) {
        if (ResultCollector::go(*this, std::forward<FF>(f), index)) _f();
    }
};

/**************************************************************************************************/

template <class C, class E, class T>
void attach_tasks(size_t index, E&& executor, const std::shared_ptr<C>& context, T&& a) {
    auto&& hold = std::forward<T>(a).recover(
        std::forward<E>(executor), [_context = make_weak_ptr(context), _i = index](const auto& x) {
            auto p = _context.lock();
            if (!p) return;
            if (auto ex = x.exception()) {
                p->failure(std::move(ex), _i);
            } else {
                p->done(std::move(x), _i);
            }
        });

    std::unique_lock<std::mutex> guard(context->_guard);
    context->_holds[index] = std::move(hold);
}

template <class R, class T, class C, class Enabled = void>
struct create_range_of_futures;

template <class R, class T, class C>
struct create_range_of_futures<R, T, C, enable_if_copyable<T>> {
    template <class E, class F, class I>
    static auto do_it(const E& executor, F&& f, I first, I last) {
        assert(first != last);

        auto context = std::make_shared<C>(std::forward<F>(f), std::distance(first, last));
        auto p = package<R()>(executor, [_c = context]() mutable { return _c->execute(); });

        context->_f = std::move(p.first);

        size_t index(0);
        for (; first != last; ++first) {
            attach_tasks(index++, executor, context, *first);
        }

        return std::move(p.second);
    }
};

template <class R, class T, class C>
struct create_range_of_futures<R, T, C, enable_if_not_copyable<T>> {
    template <class E, class F, class I>
    static auto do_it(const E& executor, F&& f, I first, I last) {
        assert(first != last);

        auto context = std::make_shared<C>(std::forward<F>(f), std::distance(first, last));
        auto p = package<R()>(executor, [_c = context] { return _c->execute(); });

        context->_f = std::move(p.first);

        size_t index(0);
        for (; first != last; ++first) {
            attach_tasks(index++, executor, context, std::move(*first));
        }

        return std::move(p.second);
    }
};

/**************************************************************************************************/

} // namespace detail

/**************************************************************************************************/

/// Returns a future that completes when all futures in `[range.first, range.second)` are ready; `f`
/// receives their values.
template <class E, class F, class I>
auto when_all(const E& executor, F f, std::pair<I, I> range) {
    using param_t = typename std::iterator_traits<I>::value_type::result_type;
    using result_t = typename detail::result_of_when_all_t<F, param_t>::result_type;
    using context_result_t =
        std::conditional_t<std::is_same_v<void, param_t>, void, std::vector<param_t>>;
    using context_t = detail::common_context<detail::context_result<F, false, context_result_t>, F,
                                             detail::all_trigger, detail::single_trigger>;

    if (range.first == range.second) {
        auto p = package<result_t()>(
            executor, detail::context_result<F, false, context_result_t>(std::move(f), 0));
        executor(std::move(p.first));
        return std::move(p.second);
    }

    return detail::create_range_of_futures<result_t, param_t, context_t>::do_it(
        executor, std::move(f), range.first, range.second);
}

/**************************************************************************************************/

/// Returns a future that completes when any future in `[range.first, range.second)` is ready; `f`
/// receives the result and the index of the future that completed first (as a second argument of
/// type `std::size_t`).
template <class E, class F, class I>
auto when_any(const E& executor, F&& f, std::pair<I, I> range) {
    using param_t = typename std::iterator_traits<I>::value_type::result_type;
    using result_t = std::decay_t<typename detail::result_of_when_any_t<F, param_t>::result_type>;
    using context_result_t = std::conditional_t<std::is_same_v<void, param_t>, void, param_t>;
    using context_t = detail::common_context<detail::context_result<F, true, context_result_t>, F,
                                             detail::single_trigger, detail::all_trigger>;

    if (range.first == range.second) {
        return future_with_broken_promise<std::decay_t<result_t>>(executor);
    }

    return detail::create_range_of_futures<result_t, param_t, context_t>::do_it(
        std::move(executor), std::forward<F>(f), range.first, range.second);
}

/**************************************************************************************************/

/// Runs `f` with `args` on `executor` and returns a future for the result.
template <class E, class F, class... Args>
auto async(const E& executor, F&& f, Args&&... args)
    -> detail::reduced_t<detail::result_t<std::decay_t<F>, std::decay_t<Args>...>> {
    using result_type = detail::result_t<std::decay_t<F>, std::decay_t<Args>...>;

    auto [pro, fut] = package<result_type()>(
        executor,
        [f = std::forward<F>(f), args = std::make_tuple(std::forward<Args>(args)...)]() mutable
            -> result_type { return std::apply(std::move(f), std::move(args)); });

    executor(std::move(pro));

    return std::move(fut);
}

/**************************************************************************************************/

namespace detail {

/**************************************************************************************************/

template <class T>
struct value_<T, enable_if_copyable<void_to_monostate_t<T>>> {
    template <class C>
    static void proceed(C& sb) {
        typename C::then_t then;
        {
            std::unique_lock<std::mutex> lock(sb._mutex);
            sb._ready = true;
            then = std::move(sb._then);
        }
        for (auto& e : then)
            e.first(std::move(e.second));
    }

    template <class R, class A>
    static void set(shared_base<R>& sb, A&& a) {
        sb._result = std::forward<A>(a);
        proceed(sb);
    }
};

template <class T>
struct value_<T, enable_if_not_copyable<void_to_monostate_t<T>>> {
    template <class C>
    static void proceed(C& sb) {
        typename C::then_t then;
        {
            std::unique_lock<std::mutex> lock(sb._mutex);
            sb._ready = true;
            then = std::move(sb._then);
        }
        if (then.first) then.first(std::move(then.second));
    }

    template <class R, class A>
    static void set(shared_base<R>& sb, A&& a) {
        sb._result = std::forward<A>(a);
        proceed(sb);
    }
};

/**************************************************************************************************/

template <class T>
template <class A>
void shared_base<T, enable_if_copyable<void_to_monostate_t<T>>>::set_value(A&& a) {
    value_<T>::set(*this, std::forward<A>(a));
}

template <class T>
template <class A>
void shared_base<T, enable_if_not_copyable<void_to_monostate_t<T>>>::set_value(A&& a) {
    value_<T>::set(*this, std::forward<A>(a));
}

/**************************************************************************************************/

} // namespace detail

/**************************************************************************************************/

} // namespace STLAB_VERSION_NAMESPACE()
} // namespace stlab

/**************************************************************************************************/

#if STLAB_STD_COROUTINES()

/**************************************************************************************************/

namespace stlab {
inline namespace STLAB_VERSION_NAMESPACE() {
namespace detail {

template <class... Args>
struct final_awaiter {
    bool await_ready() noexcept { return false; }
    void await_resume() noexcept {}
    void await_suspend(std::coroutine_handle<> h) noexcept {
        h.destroy();
    }
};

/// Awaitable that suspends and resumes the coroutine on the given executor when the future
/// completes.
template <class R>
struct resume_on_awaiter {
    executor_t _executor;
    future<R> _input;

    bool await_ready() const noexcept { return false; }

    auto await_resume() { return std::move(_input).get_ready(); }

    void await_suspend(std::coroutine_handle<> ch) {
        _input.on_completion(std::move(_executor), [ch]() noexcept { ch.resume(); });
    }
};

/// resume_on_awaiter with weak_ptr check for use from coroutines (via await_transform).
/// When AllowSkipSuspend is true (plain co_await), await_ready returns is_ready() to avoid
/// unnecessary suspension. When false (resume_on), always suspend so we resume on the given
/// executor.
template <class R, class WeakPtr, bool AllowSkipSuspend>
struct resume_on_awaiter_with_control {
    executor_t _executor;
    future<R> _input;
    WeakPtr _weak;

    bool await_ready() const noexcept {
        if constexpr (AllowSkipSuspend) {
            return _input.is_ready();
        } else {
            return false;
        }
    }
    auto await_resume() { return std::move(_input).get_ready(); }
    void await_suspend(std::coroutine_handle<> ch) {
        assert(_weak.lock() && "await_suspend: weak_state is gone");
        _weak.lock()->_co_handle = ch.address();
        _input.on_completion(std::move(_executor), [weak = std::move(_weak)]() noexcept {
            if (auto state = weak.lock())
                std::coroutine_handle<>::from_address(state->_co_handle).resume();
        });
    }
};

} // namespace detail

/// When the future completes, the current coroutine is resumed on `executor`; result/exception
/// semantics are the same as `co_await std::move(f)`.
template <class E, class R>
auto resume_on(E&& executor, future<R>&& f) -> detail::resume_on_awaiter<R> {
    return detail::resume_on_awaiter<R>{executor_t(std::forward<E>(executor)), std::move(f)};
}

} // namespace STLAB_VERSION_NAMESPACE()
} // namespace stlab

/**************************************************************************************************/
// Coroutine ownership and destruction rules (implementation notes):
//
// We destroy the coroutine only while it is suspended, in final_awaiter::await_suspend (h.destroy()).
// Before invoking the packaged_task (in return_value/return_void/unhandled_exception), we clear
// _co_handle in the shared state so ~shared_task() will not try to destroy the handle when the
// future is later destroyed. When suspended on a future we store the handle in _co_handle so we
// can resume via weak_state. For non-future awaitables we clear _co_handle before suspending
// (give up ownership for that suspend). initial_suspend is suspend_never so we never own before
// the first suspend.
/**************************************************************************************************/

template <class T, class... Args>
struct std::coroutine_traits<stlab::future<T>, Args...> {
    struct promise_type {
        stlab::packaged_task<std::variant<T, std::exception_ptr>> _promise;

        stlab::future<T> get_return_object() {
            auto [pro, fut] = stlab::package<T(std::variant<T, std::exception_ptr>)>(
                stlab::immediate_executor,
                [](std::variant<T, std::exception_ptr>&& v) mutable -> T {
                    // Use index (1 = exception) to avoid ambiguity when T is std::exception_ptr.
                    if (auto* ep = std::get_if<1>(&v)) {
                        std::rethrow_exception(*ep);
                    }
                    return std::get<0>(std::move(v));
                });
            _promise = std::move(pro);
            return std::move(fut);
        }

        auto initial_suspend() const noexcept { return std::suspend_never{}; }

        auto final_suspend() noexcept {
            assert(_promise.canceled() && "final_suspend: promise not fulfilled");
            return stlab::detail::final_awaiter<std::variant<T, std::exception_ptr>>{};
        }

        template <class U>
        void return_value(U&& val) {
            _promise(
                std::variant<T, std::exception_ptr>{std::in_place_type<T>, std::forward<U>(val)});
        }

        void unhandled_exception() {
            _promise(std::variant<T, std::exception_ptr>{std::in_place_type<std::exception_ptr>,
                                                         std::current_exception()});
        }

        template <class R>
        auto await_transform(stlab::future<R>&& f) {
            return stlab::detail::resume_on_awaiter_with_control<
                R, decltype(stlab::detail::weak_state(_promise)), true>{
                stlab::immediate_executor, std::move(f), stlab::detail::weak_state(_promise)};
        }
        template <class R>
        auto await_transform(stlab::detail::resume_on_awaiter<R> a) {
            return stlab::detail::resume_on_awaiter_with_control<
                R, decltype(stlab::detail::weak_state(_promise)), false>{
                std::move(a._executor), std::move(a._input), stlab::detail::weak_state(_promise)};
        }
        template <class U>
        U&& await_transform(U&& u) {
            if (auto state = stlab::detail::weak_state(_promise).lock())
                state->_co_handle = nullptr;
            return std::forward<U>(u);
        }
    };
};

template <class... Args>
struct std::coroutine_traits<stlab::future<void>, Args...> {
    struct promise_type {
        stlab::packaged_task<std::exception_ptr> _promise;

        stlab::future<void> get_return_object() {
            auto [pro, fut] = stlab::package<void(std::exception_ptr)>(
                stlab::immediate_executor, [](const std::exception_ptr& ep) mutable {
                    if (ep) std::rethrow_exception(ep);
                });
            _promise = std::move(pro);
            return std::move(fut);
        }

        auto initial_suspend() const noexcept { return std::suspend_never{}; }

        auto final_suspend() noexcept {
            assert(_promise.canceled() && "final_suspend: promise not fulfilled");
            return stlab::detail::final_awaiter<std::exception_ptr>{};
        }

        void return_void() { _promise(std::exception_ptr{}); }

        void unhandled_exception() { _promise(std::current_exception()); }

        template <class R>
        auto await_transform(stlab::future<R>&& f) {
            return stlab::detail::resume_on_awaiter_with_control<
                R, decltype(stlab::detail::weak_state(_promise)), true>{
                stlab::immediate_executor, std::move(f), stlab::detail::weak_state(_promise)};
        }
        template <class R>
        auto await_transform(stlab::detail::resume_on_awaiter<R> a) {
            return stlab::detail::resume_on_awaiter_with_control<
                R, decltype(stlab::detail::weak_state(_promise)), false>{
                std::move(a._executor), std::move(a._input), stlab::detail::weak_state(_promise)};
        }
        template <class U>
        U&& await_transform(U&& u) {
            if (auto state = stlab::detail::weak_state(_promise).lock())
                state->_co_handle = nullptr;
            return std::forward<U>(u);
        }
    };
};

/**************************************************************************************************/

template <class R>
auto operator co_await(stlab::future<R>&& f) {
    return stlab::detail::resume_on_awaiter<R>{stlab::immediate_executor, std::move(f)};
}

// co_await on an lvalue future is deleted — use std::move(f) to make cancellation semantics
// explicit
template <class R>
auto operator co_await(stlab::future<R>& f) = delete;

#endif // STLAB_STD_COROUTINES()

#endif
