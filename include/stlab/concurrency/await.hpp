/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_AWAIT_HPP
#define STLAB_CONCURRENCY_AWAIT_HPP

/*! @file await.hpp
 *  @brief Blocking wait helpers for futures (`await`, `blocking_get`, etc.).
 */

#include <stlab/config.hpp>

#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <utility>

#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/immediate_executor.hpp>
#include <stlab/concurrency/ready_future.hpp>
#include <stlab/memory.hpp>

#if STLAB_TASK_SYSTEM(PORTABLE)
#include <stlab/concurrency/default_executor.hpp>
#endif

/**************************************************************************************************/

/** @defgroup stlab_concurrency_await await
 *  @ingroup stlab_concurrency
 *  @brief Blocking wait helpers for futures (`await`, `blocking_get`, etc.).
 *  @{
 */

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/**************************************************************************************************/

/**
    Assumes `f` _will wait_ and wakes or adds a thread to the thread pool (to the limit) before
    invoking `f`.
*/
template <class F>
auto invoke_waiting(F&& f) {
#if STLAB_TASK_SYSTEM(PORTABLE)
    if (!detail::pts().wake()) detail::pts().add_thread();
#endif

    return std::forward<F>(f)();
}

/**************************************************************************************************/

/// Synchronously wait for the result `x`. If `x` resolves as an exception, the exception is
/// rethrown. When using the portable task system, an additional thread is added to the pool if no
/// threads are available and the maximum number of threads has not been reached.

template <class T>
auto await(future<T>&& x) -> T {
    if (x.is_ready()) return std::move(x).get_ready(); // if ready, done

    std::mutex m;
    std::condition_variable condition;
    future<T> result;

    auto hold = std::move(x).recover(immediate_executor, [&](future<T>&& r) {
        {
            std::unique_lock<std::mutex> lock{m};
            result = std::move(r);
            condition.notify_one(); // must notify under lock
        }
    });
    std::unique_lock<std::mutex> lock{m};
    invoke_waiting([&] { condition.wait(lock, [&] { return result.is_ready(); }); });
    return std::move(result).get_ready();
}

/// Equivalent to `await(copy(x))`.

template <class T>
[[deprecated("implicit copy deprecated, use `await(std::move(f))` or `await(stlab::copy(f))`"
             " instead.")]]
auto await(const future<T>& x) -> T {
    return await(future<T>{x});
}

namespace detail {

template <class T>
struct blocking_get_guarded {
    bool _timed_out{false};
    future<T> _result;
    std::condition_variable _condition;
    std::mutex _mutex;

    template <class F>
    auto set(F&& r) -> future<T> {
        {
            std::unique_lock<std::mutex> lock{_mutex};
            if (_timed_out) return std::forward<F>(r);
            _result = std::forward<F>(r);
        }
        _condition.notify_one();
        /*
            A valid future<> result is required because the following reduction in
            recover() will attach a continuation - which will crash on an invalid
            future<>.
         */
        return make_exceptional_future<T>(
            std::make_exception_ptr(std::runtime_error("not an error")), immediate_executor);
    }

    auto wait_for(const std::chrono::nanoseconds& timeout) -> future<T> {
        std::unique_lock<std::mutex> lock{_mutex};
        _timed_out = !invoke_waiting(
            [&] { return _condition.wait_for(lock, timeout, [&] { return _result.valid(); }); });
        return _timed_out ? future<T>{} : std::move(_result);
    }
};

} // namespace detail

template <class T>
auto await_for(future<T>&& x, const std::chrono::nanoseconds& timeout) -> future<T> {
    if (x.is_ready()) return std::move(x);

    auto p = std::make_shared<detail::blocking_get_guarded<T>>();

    auto hold = std::move(x).recover(immediate_executor, [_p = stlab::make_weak_ptr(p)](auto&& r) {
        if (auto p = _p.lock()) return p->set(std::forward<decltype(r)>(r));
        return std::forward<decltype(r)>(r);
    });

    auto result = p->wait_for(timeout);
    return result.valid() ? std::move(result) : std::move(hold);
}

/// Equivalent to `await_for(copy(x), timeout)`.

template <class T>
[[deprecated("implicit copy deprecated, use `await_for(std::move(f), t)` or"
             " `await_for(stlab::copy(f), t)` instead.")]]
auto await_for(const future<T>& x, const std::chrono::nanoseconds& timeout) -> future<T> {
    return await_for(future<T>{x}, timeout);
}

/**************************************************************************************************/

template <class T>
[[deprecated("Use await instead.")]]
auto blocking_get(future<T> x) -> T {
    return await(std::move(x));
}

template <class T>
[[deprecated("Use await_for instead.")]]
auto blocking_get_for(future<T> x, const std::chrono::nanoseconds& timeout) -> future<T> {
    await_for(std::move(x), timeout);
}

template <class T>
[[deprecated("Use await_for instead.")]]
auto blocking_get(future<T> x, const std::chrono::nanoseconds& timeout) -> decltype(x.get_try()) {
    return blocking_get_for(std::move(x), timeout).get_try();
}

/**************************************************************************************************/

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

/** @} */

#endif // STLAB_CONCURRENCY_AWAIT_HPP
