/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_TRAITS_HPP
#define STLAB_CONCURRENCY_TRAITS_HPP

#include <stlab/config.hpp>

#include <type_traits>

/**************************************************************************************************/

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/**************************************************************************************************/

template <bool...>
struct bool_pack;
template <bool... v>
using all_true = std::is_same<bool_pack<true, v...>, bool_pack<v..., true>>;

/**************************************************************************************************/

template <template <typename> class test, typename T>
struct smart_test : test<T> {};

template <typename T>
using smart_is_copy_constructible = smart_test<std::is_copy_constructible, T>;

template <typename T>
constexpr bool smart_is_copy_constructible_v = smart_is_copy_constructible<T>::value;

/**************************************************************************************************/

template <typename T>
using enable_if_copyable = std::enable_if_t<smart_is_copy_constructible_v<T>>;

template <typename T>
using enable_if_not_copyable = std::enable_if_t<!smart_is_copy_constructible_v<T>>;

/**************************************************************************************************/

// the following implements the C++ standard 17 proposal N4502
#if __GNUC__ < 5 && !defined __clang__
// http://stackoverflow.com/a/28967049/1353549
template <typename...>
struct voider {
    using type = void;
};
template <typename... Ts>
using void_t = typename voider<Ts...>::type;
#else
template <typename...>
using void_t = void;
#endif

struct nonesuch {
    nonesuch() = delete;
    ~nonesuch() = delete;
    nonesuch(nonesuch const&) = delete;
    void operator=(nonesuch const&) = delete;
};

// primary template handles all types not supporting the archetypal Op:
template <class Default, class, template <class...> class Op, class... Args>
struct detector {
    using value_t = std::false_type;
    using type = Default;
};

// the specialization recognizes and handles only types supporting Op:
template <class Default, template <class...> class Op, class... Args>
struct detector<Default, void_t<Op<Args...>>, Op, Args...> {
    using value_t = std::true_type;
    using type = Op<Args...>;
};

template <template <class...> class Op, class... Args>
using is_detected = typename detector<nonesuch, void, Op, Args...>::value_t;

template <template <class...> class Op, class... Args>
constexpr bool is_detected_v = is_detected<Op, Args...>::value;

template <template <class...> class Op, class... Args>
using detected_t = typename detector<nonesuch, void, Op, Args...>::type;

/**************************************************************************************************/

STLAB_VERSION_NAMESPACE_END()

/**************************************************************************************************/

} // namespace stlab

/**************************************************************************************************/

#endif

/**************************************************************************************************/
