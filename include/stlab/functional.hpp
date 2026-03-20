/*
    Copyright 2017 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_FUNCTIONAL_HPP
#define STLAB_FUNCTIONAL_HPP

/*! @file functional.hpp
 *  @brief Reference unwrapping and related functional helpers.
 */

/**************************************************************************************************/

#include <stlab/config.hpp>

#include <functional>
#include <type_traits>

/**************************************************************************************************/

/** @defgroup stlab_functional functional
 *  @ingroup stlab
 *  @brief Reference unwrapping and related functional helpers.
 *  @{
 */

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/**************************************************************************************************/

template <class T>
struct unwrap_reference {
    using type = T;
};

template <class T>
struct unwrap_reference<std::reference_wrapper<T>> {
    using type = T;
};

template <class T>
using unwrap_reference_t = typename unwrap_reference<T>::type;

/**************************************************************************************************/

template <class T>
struct is_reference_wrapper : std::false_type {};
template <class T>
struct is_reference_wrapper<std::reference_wrapper<T>> : std::true_type {};

template <class T>
constexpr bool is_reference_wrapper_v = is_reference_wrapper<T>::value;

/**************************************************************************************************/

template <typename T>
auto unwrap(T& val) -> T& {
    return val;
}

template <typename T>
auto unwrap(const T& val) -> const T& {
    return val;
}

template <typename T>
auto unwrap(std::reference_wrapper<T>& val) -> T& {
    return val.get();
}

template <typename T>
auto unwrap(const std::reference_wrapper<T>& val) -> const T& {
    return val.get();
}

/**************************************************************************************************/

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

/** @} */

#endif

/**************************************************************************************************/
