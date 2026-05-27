/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_SCOPE_HPP
#define STLAB_SCOPE_HPP

/*! @file scope.hpp
 *  @brief Bind an object’s lifetime to a callable’s execution (`scope`).
 *
 *  @par Motivation
 *  A plain extra `{ }` block does not document intent. `scope` constructs `T` from the leading
 *  arguments, runs the last argument as a nullary function while `T` is alive, then destroys `T`.
 *
 *  @par Example
 *  @code
 *  void pop_and_run_task() {
 *      std::function<void()> task = scope<std::lock_guard<std::mutex>>(m, [&]() {
 *          return pop_front_unsafe(task_queue);
 *      });
 *      task();
 *  }
 *  @endcode
 *  The lock’s lifetime is bound to the lambda that pops the queue—clearer than an anonymous scope.
 */

/**************************************************************************************************/

#include <stlab/config.hpp>

#include <mutex>
#include <tuple>
#include <utility>

/**************************************************************************************************/

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/** @defgroup stlab_scope scope
 *  @brief RAII scope helpers (`scope`, mutex guard).
 *  @{
 */

/**************************************************************************************************/

namespace detail {

template <typename T, typename Tuple, size_t... S>
auto scope_call(Tuple&& t, std::index_sequence<S...>) {
    T scoped(std::forward<std::tuple_element_t<S, Tuple>>(std::get<S>(t))...);
    (void)scoped;

    // call the function
    constexpr size_t last_index = std::tuple_size_v<Tuple> - 1;
    return std::forward<std::tuple_element_t<last_index, Tuple>>(std::get<last_index>(t))();
}

} // namespace detail

/**************************************************************************************************/

/// Scopes the lifetime of an instance of `T`. All but the last arguments construct `T`; the last
/// argument is a nullary function invoked while `T` is alive. `T` is destroyed after that function
/// returns.
template <typename T, typename... Args>
inline auto scope(Args&&... args) {
    return detail::scope_call<T>(std::forward_as_tuple(std::forward<Args>(args)...),
                                 std::make_index_sequence<sizeof...(args) - 1>());
}

/// @overload
/// @brief Workaround until VS2017 bug is fixed; prefer the variadic `scope` when possible.
template <typename T, typename F>
inline auto scope(std::mutex& m, F&& f) {
    T scoped(m);
    return std::forward<F>(f)();
}

/**************************************************************************************************/

/** @} */

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

#endif

/**************************************************************************************************/
