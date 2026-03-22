/*
    Copyright 2017 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_MEMORY_HPP
#define STLAB_MEMORY_HPP

/*! @file memory.hpp
 *  @brief Small memory-related utilities (`make_weak_ptr`).
 */

/**************************************************************************************************/

#include <memory>
#include <stlab/config.hpp>

/**************************************************************************************************/

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/** @defgroup stlab_memory memory
 *  @ingroup stlab
 *  @brief Small memory-related utilities (`make_weak_ptr`).
 *  @{
 */

/**************************************************************************************************/

/// Returns a `std::weak_ptr<T>` sharing ownership with `x`.
template <typename T>
auto make_weak_ptr(const std::shared_ptr<T>& x) {
    return std::weak_ptr<T>(x);
}

/**************************************************************************************************/

/** @} */

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

#endif
