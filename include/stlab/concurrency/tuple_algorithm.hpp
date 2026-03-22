/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
    Author: Felix Petriconi
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_TUPLE_ALGORITHM_HPP
#define STLAB_CONCURRENCY_TUPLE_ALGORITHM_HPP

/*! @file tuple_algorithm.hpp
 *  @brief Tuple algorithms and utilities (including for future combiners).
 */

#include <stlab/config.hpp> // for STLAB_VERSION_NAMESPACE_*

// stdc++
#include <cstddef>     // for size_t
#include <optional>    // for optional
#include <tuple>       // for get, tuple, tuple_size, tuple_cat, tuple...
#include <type_traits> // for is_same_v, conditional_t, conditional
#include <utility>     // for forward, index_sequence, declval, move

/**************************************************************************************************/

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/** @defgroup stlab_concurrency_tuple_algorithm tuple_algorithm
 *  @ingroup stlab_concurrency
 *  @brief Tuple algorithms and utilities (including for future combiners).
 *  @{
 */

/**************************************************************************************************/

class placeholder {};

/**************************************************************************************************/

namespace detail {

/**************************************************************************************************/

template <std::size_t I, std::size_t L, typename T, typename Op>
struct tuple_find_impl {
    static auto find(const T& t, Op op) {
        if (op(std::get<I>(t))) return I;
        return tuple_find_impl<I + 1, L, T, Op>::find(t, op);
    }
};

template <std::size_t L, typename T, typename Op>
struct tuple_find_impl<L, L, T, Op> {
    static auto find(const T&, Op) { return L; }
};

template <std::size_t I, std::size_t L, typename T, typename Op>
struct tuple_for_each_impl {
    static void for_each(T& t, Op& op) {
        op(std::get<I>(t));
        tuple_for_each_impl<I + 1, L, T, Op>::for_each(t, op);
    }
};

template <std::size_t L, typename T, typename Op>
struct tuple_for_each_impl<L, L, T, Op> {
    static void for_each(T&, Op&) {}
};

template <std::size_t I, std::size_t L, typename T, typename Op>
struct tuple_min_element_impl {
    static void min_element(T& t, Op& op) {
        op(std::get<I>(t));
        tuple_min_element_impl<I + 1, L, T, Op>::min_element(t, op);
    }
};

template <std::size_t I, std::size_t L>
struct get_i_impl {
    template <typename T, typename F, typename D>
    static auto go(T& t, std::size_t index, F&& f, D&& default_v) {
        if (index == I) return std::forward<F>(f)(std::get<I>(t));
        return get_i_impl<I + 1, L>::go(t, index, std::forward<F>(f), std::forward<D>(default_v));
    }
};

template <std::size_t L>
struct get_i_impl<L, L> {
    template <typename T, typename F, typename D>
    static auto go(T&, std::size_t, F&&, D&& default_v) {
        return std::forward<D>(default_v);
    }
};

template <std::size_t I, std::size_t L>
struct void_i_impl {
    template <typename T, typename F>
    static void go(T& t, std::size_t index, F&& f) {
        if (index == I)
            std::forward<F>(f)(std::get<I>(t));
        else
            void_i_impl<I + 1, L>::go(t, index, std::forward<F>(f));
    }
};

template <std::size_t L>
struct void_i_impl<L, L> {
    template <typename T, typename F>
    static void go(T&, std::size_t, F&&) {}
};

/**************************************************************************************************/

} // namespace detail

/**************************************************************************************************/
/*
 * Finds in a tuple an element that satisfies the given predicate and returns the tuple index.
 * It returns an index beyond the last element if no element satisfies the predicate.
 */
template <typename T, typename Op>
auto tuple_find(const T& t, Op op) -> std::size_t {
    if constexpr (std::tuple_size_v<T> == 0)
        return 1;
    else
        return detail::tuple_find_impl<0, std::tuple_size_v<T>, T, Op>::find(t, op);
}

/*
 * Applies the given predicate on all tuple elements
 */
template <typename T, typename Op>
void tuple_for_each(T& t, Op op) {
    detail::tuple_for_each_impl<0, std::tuple_size_v<T>, T, Op>::for_each(t, op);
}

/*
 * Gets from the tuple element at position index the value by applying the given predicate
 * The default value is returned, if the index is equal or greater to tuple_size
 */
template <typename T, typename F, typename D>
auto get_i(T& t, std::size_t index, F f, D&& default_v) {
    return detail::get_i_impl<0, std::tuple_size_v<T>>::go(t, index, std::move(f),
                                                           std::forward<D>(default_v));
}

/*
 * Applies at the tuple element at position index the the given predicate
 */
template <typename T, typename F>
auto void_i(T& t, std::size_t index, F&& f) {
    return detail::void_i_impl<0, std::tuple_size_v<T>>::go(t, index, std::forward<F>(f));
}

/**************************************************************************************************/

namespace detail {

/**************************************************************************************************/

template <class F, class Tuple, std::size_t... I>
constexpr auto apply_impl(F&& f, Tuple&& t, std::index_sequence<I...>) -> decltype(auto) {
    return std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))...);
}

template <class F, class Tuple, std::size_t... I>
constexpr auto apply_optional_indexed_impl(F&& f, Tuple&& t, std::index_sequence<I...>)
    -> decltype(auto) {
    return std::forward<F>(f)(std::move(*std::get<I>(std::forward<Tuple>(t)))...);
}

/**************************************************************************************************/

template <class Seq, class F, class Tuple>
constexpr auto apply_optional_indexed(F&& f, Tuple&& t) -> decltype(auto) {
    return detail::apply_optional_indexed_impl(std::forward<F>(f), std::forward<Tuple>(t), Seq());
}

/**************************************************************************************************/

template <class T, std::size_t N>
struct map_placeholder {
    using type = std::index_sequence<N>;
};

template <std::size_t N>
struct map_placeholder<placeholder, N> {
    using type = std::index_sequence<>;
};

template <std::size_t N>
struct map_placeholder<std::optional<placeholder>, N> {
    using type = std::index_sequence<>;
};

/**************************************************************************************************/

} // namespace detail

/**************************************************************************************************/
// type-function variant of std::tuple_cat
template <typename... Ts>
using tuple_cat_t = decltype(std::tuple_cat(std::declval<Ts>()...));

/**************************************************************************************************/
// type-function that takes a parameter pack and returns a std::tuple<Ts...>
// where all T[i] == void have been removed.
template <typename... Ts>
using voidless_tuple =
    tuple_cat_t<std::conditional_t<std::is_same_v<void, Ts>, std::tuple<>, std::tuple<Ts>>...>;

/**************************************************************************************************/
// type-function that takes a parameter pack and returns a std::tuple<Ts...>
// where all T[i] == void have been replaced with stlab::placeholder.
template <typename... Ts>
using placeholder_tuple =
    std::tuple<std::conditional_t<std::is_same_v<void, Ts>, placeholder, Ts>...>;

/**************************************************************************************************/
// type-function that takes a parameter pack and returns a std::tuple<std::optional<Ts>...>
// where all T[i] == void have been replaced with stlab::placeholder.
template <typename... Ts>
using optional_placeholder_tuple =
    std::tuple<std::optional<std::conditional_t<std::is_same_v<void, Ts>, placeholder, Ts>>...>;

/**************************************************************************************************/
// apply the tuple `t`as arguments to the function `f`. Placeholders are ignored.
template <class F, class Tuple>
constexpr auto apply_ignore_placeholders(F&& f, Tuple&& t) -> decltype(auto) {
    return detail::apply_impl(std::forward<F>(f), std::forward<Tuple>(t),
                              std::make_index_sequence<std::tuple_size_v<Tuple>>());
}

/**************************************************************************************************/
// remove_placeholder::function<Index>::type returns a std::index_sequence<Index> if the type at
// Index is not a placeholder, otherwise it returns an empty std::index_sequence.
template <class Tuple>
struct remove_placeholder {
    template <std::size_t Index>
    struct function {
        using type =
            typename detail::map_placeholder<std::tuple_element_t<Index, Tuple>, Index>::type;
    };
};

/**************************************************************************************************/
// apply_indexed applies the tuple `t` as arguments to the function `f` using the index sequence
// `Seq` to select the arguments.
template <class Seq, class F, class Tuple>
constexpr auto apply_indexed(F&& f, Tuple&& t) -> decltype(auto) {
    return detail::apply_impl(std::forward<F>(f), std::forward<Tuple>(t), Seq());
}

/**************************************************************************************************/

/** @} */

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

#endif
