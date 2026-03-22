/*
    Copyright 2017 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_UTILITY_HPP
#define STLAB_UTILITY_HPP

/*! @file utility.hpp
 *  @brief Type utilities (`move`, `copy`, `static_max`, etc.).
 */

/**************************************************************************************************/

#include <stlab/config.hpp>

#include <array>
#include <cstddef>          // for size_t
#include <initializer_list> // for initializer_list
#include <type_traits>      // for remove_reference, is_same, is_const
#include <utility>          // for index_sequence, forward

/**************************************************************************************************/

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/** @defgroup stlab_utility utility
 *  @ingroup stlab
 *  @brief Type utilities (`move`, `copy`, `static_max`, etc.).
 *  @{
 */

/**************************************************************************************************/

namespace detail {

/**************************************************************************************************/

template <bool, class T>
struct move_if_helper;

template <class T>
struct move_if_helper<true, T> {
    using type = std::remove_reference_t<T>&&;
};

template <class T>
struct move_if_helper<false, T> {
    using type = std::remove_reference_t<T>&;
};

template <bool P, class T>
using move_if_helper_t = typename move_if_helper<P, T>::type;

/**************************************************************************************************/

} // namespace detail

/**************************************************************************************************/

template <class Seq1, class Seq2>
struct index_sequence_cat;

template <std::size_t... N1, std::size_t... N2>
struct index_sequence_cat<std::index_sequence<N1...>, std::index_sequence<N2...>> {
    using type = std::index_sequence<N1..., N2...>;
};

template <class Seq1, class Seq2>
using index_sequence_cat_t = typename index_sequence_cat<Seq1, Seq2>::type;

/**************************************************************************************************/

template <class Seq>
struct index_sequence_to_array;

template <std::size_t... N>
struct index_sequence_to_array<std::index_sequence<N...>> {
    static constexpr std::array<std::size_t, sizeof...(N)> value{{N...}};
};

/**************************************************************************************************/

template <class Seq, template <std::size_t> class F, std::size_t Index, std::size_t Count>
struct index_sequence_transform;

template <class Seq,
          template <std::size_t> class F,
          std::size_t Index = 0,
          std::size_t Count = Seq::size()>
using index_sequence_transform_t = typename index_sequence_transform<Seq, F, Index, Count>::type;

template <class Seq, template <std::size_t> class F, std::size_t Index, std::size_t Count>
struct index_sequence_transform {
    using type = index_sequence_cat_t<
        index_sequence_transform_t<Seq, F, Index, Count / 2>,
        index_sequence_transform_t<Seq, F, Index + Count / 2, Count - Count / 2>>;
};

template <class Seq, template <std::size_t> class F, std::size_t Index>
struct index_sequence_transform<Seq, F, Index, 0> {
    using type = std::index_sequence<>;
};

template <class Seq, template <std::size_t> class F, std::size_t Index>
struct index_sequence_transform<Seq, F, Index, 1> {
    using type = typename F<index_sequence_to_array<Seq>::value[Index]>::type;
};

/**************************************************************************************************/

/// @brief If `P` is true, casts `t` to an rvalue; otherwise keeps an lvalue reference.
template <bool P, class T>
constexpr auto move_if(T&& t) noexcept -> detail::move_if_helper_t<P, T> {
    return static_cast<detail::move_if_helper_t<P, T>>(t);
}

/**************************************************************************************************/

/// Invokes `f(arg0)`, `f(arg1)`, … `f(argN)`.
/// @param f Unary function invoked once per argument.
/// @param args Arguments passed one-by-one to `f`.
template <class F, class... Args>
void for_each_argument(F&& f, Args&&... args) {
    return (void)std::initializer_list<int>{(std::forward<F>(f)(args), 0)...};
}

/**************************************************************************************************/

/// Returns a copy of the argument. Used to pass an lvalue to a function taking an rvalue, or to
/// copy a type with an `explicit` copy constructor.
///
/// @par Example
/// @code
/// void f(std::vector<int>&&);
/// std::vector<int> c = ...;
/// f(copy(c)); // pass a copy of c
/// f(move(c)); // sink c
/// @endcode
template <typename T>
constexpr auto copy(T&& value) noexcept(noexcept(std::decay_t<T>{static_cast<T&&>(value)}))
    -> std::decay_t<T> {
    static_assert(!std::is_same_v<std::decay_t<T>, T>, "explicit copy of rvalue.");
    return std::decay_t<T>{static_cast<T&&>(value)};
}

/**************************************************************************************************/

/// A standard move implementation but with a compile-time check for const types.
template <class T>
constexpr auto move(T&& t) noexcept -> std::remove_reference_t<T>&& {
    static_assert(!std::is_const_v<std::remove_reference_t<T>>,
                  "move of const type will unintentionally decay to copy");
    return static_cast<std::remove_reference_t<T>&&>(t);
}

/**************************************************************************************************/

/** @} */

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

#endif

/**************************************************************************************************/
