/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_IMMEDIATE_EXECUTOR_HPP
#define STLAB_CONCURRENCY_IMMEDIATE_EXECUTOR_HPP

/*! @file immediate_executor.hpp
 *  @brief Synchronous inline executor.
 */

#include <stlab/config.hpp>

#include <type_traits>
#include <utility>

/**************************************************************************************************/

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/** @defgroup stlab_concurrency_immediate_executor immediate_executor
 *  @ingroup stlab_concurrency
 *  @brief Synchronous inline executor.
 *  @{
 */

/**************************************************************************************************/

namespace detail {

/**************************************************************************************************/

struct immediate_executor_type {
    template <typename F>
    auto operator()(F&& f) const -> std::enable_if_t<std::is_nothrow_invocable_v<F>> {
        std::forward<F>(f)();
    }
};

/**************************************************************************************************/

} // namespace detail

/**************************************************************************************************/

/// Invokes work inline on the calling thread (synchronous executor).
inline constexpr auto immediate_executor = detail::immediate_executor_type{};

/**************************************************************************************************/

/** @} */

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

#endif

/**************************************************************************************************/
