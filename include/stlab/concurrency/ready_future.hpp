/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_READY_FUTURE_HPP
#define STLAB_CONCURRENCY_READY_FUTURE_HPP

/*! @file ready_future.hpp
 *  @brief Factory functions for already-resolved futures.
 *
 *  @details
 *  `make_ready_future` completes a `package` immediately with a value (or `void`).
 *  `make_exceptional_future` completes with an error. The `executor` argument schedules any
 *  attached continuation.
 */

#include <stlab/config.hpp>

#include <exception>
#include <type_traits>
#include <utility>

#include <stlab/concurrency/future.hpp>

/**************************************************************************************************/

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/** @defgroup stlab_concurrency_ready_future ready_future
 *  @ingroup stlab_concurrency
 *  @brief Factory functions for already-resolved futures.
 *  @{
 */

/**************************************************************************************************/

/// Creates a `future` already completed with `x` (`executor` runs attached continuations).
template <typename T, typename E>
auto make_ready_future(T&& x, E executor) -> future<std::decay_t<T>> {
    auto p = package<std::decay_t<T>(std::decay_t<T>)>(
        std::move(executor), [](auto&& x) { return std::forward<decltype(x)>(x); });
    p.first(std::forward<T>(x));
    return std::move(p.second);
}

/// Creates a `future<void>` already completed (`executor` runs attached continuations).
template <typename E>
auto make_ready_future(E executor) -> future<void> {
    auto p = package<void()>(std::move(executor), []() {});
    p.first();
    return p.second;
}

namespace detail {

template <class T>
struct _make_exceptional_future {
    template <typename E>
    auto operator()(const std::exception_ptr& error, E executor) const -> future<T> {
        auto p = package<T(T)>(std::move(executor),
                               [](auto&& a) { return std::forward<decltype(a)>(a); });
        p.first.set_exception(error);
        return std::move(p.second);
    }
};

template <>
struct _make_exceptional_future<void> {
    template <typename E>
    auto operator()(const std::exception_ptr& error, E executor) const -> future<void> {
        auto p = package<void()>(std::move(executor), []() {});
        p.first.set_exception(error);
        return std::move(p.second);
    }
};

} // namespace detail

/// Creates a `future` already failed with `error` (`executor` runs attached continuations).
template <typename T, typename E>
auto make_exceptional_future(const std::exception_ptr& error, E executor) -> future<T> {
    return detail::_make_exceptional_future<T>{}(error, std::move(executor));
}

/**************************************************************************************************/

/** @} */

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

#endif // STLAB_CONCURRENCY_READY_FUTURE_HPP
