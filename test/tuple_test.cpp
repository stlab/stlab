/*
    Copyright 2017 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/
/**************************************************************************************************/

// stdc++

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>

// boost
#include <doctest/doctest.h>
#include <tuple>
#include <utility>

// stlab
#ifndef STLAB_DISABLE_FUTURE_COROUTINES
#define STLAB_DISABLE_FUTURE_COROUTINES
#endif

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/ready_future.hpp>
#include <stlab/concurrency/tuple_algorithm.hpp>
#include <stlab/utility.hpp>


// cxxabi.h (demangling) is only available with libstdc++ (e.g. Linux/macOS Clang), not on Windows.
#if defined(__clang__) && defined(__GLIBCXX__)
#include <cxxabi.h>
#endif

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

template <typename T>
auto demangle() -> std::string {
#if defined(__clang__) && defined(__GLIBCXX__)
    struct freer_t {
        void operator()(void* x) const { std::free(x); }
    };
    std::unique_ptr<char, freer_t> c_str(
        abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, nullptr));
    return c_str.get();
#else
    return {};
#endif
}

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

using namespace std;
using namespace stlab;

/**************************************************************************************************/

TEST_CASE("remove_placeholder_test") {
    auto x = std::make_tuple(10, placeholder(), 25.0, placeholder());

    apply_indexed<index_sequence_transform_t<make_index_sequence<tuple_size_v<decltype(x)>>,
                                             remove_placeholder<decltype(x)>::function>>(
        [](auto... args) { for_each_argument([](auto x) { cout << x << endl; }, args...); }, x);
}

/**************************************************************************************************/

TEST_CASE("add_placeholder_test") {
    using interim_t = placeholder_tuple<int, void, int, void>;

    auto x = interim_t(10, placeholder(), 25, placeholder());

    apply_indexed<index_sequence_transform_t<make_index_sequence<tuple_size_v<decltype(x)>>,
                                             remove_placeholder<decltype(x)>::function>>(
        [](auto... args) { for_each_argument([](auto x) { cout << x << endl; }, args...); }, x);
}

/**************************************************************************************************/

template <typename F, typename... Ts>
void when_all_typecheck(
    F f, const future<Ts>&... futures) { // NOLINT(cert-dcl50-cpp) parameter pack, not C variadic
    (void)f;
    (..., (void)futures);
    using pt_t = placeholder_tuple<Ts...>;
    using opt_t = optional_placeholder_tuple<Ts...>;
    using vt_t = voidless_tuple<Ts...>;
    using result_t = decltype(apply_ignore_placeholders(std::declval<F>(), std::declval<vt_t>()));

    cout << "pt: " << demangle<pt_t>() << "\n";
    cout << "opt_t: " << demangle<opt_t>() << "\n";
    cout << "vt_t: " << demangle<vt_t>() << "\n";
    cout << "result_t: " << demangle<result_t>() << "\n";
}

/**************************************************************************************************/

TEST_CASE("future_typecheck_test") {
    auto fv = [] { return stlab::make_ready_future(stlab::default_executor); };
    auto fi = [] {
        static int count_s{0};
        return stlab::make_ready_future<int>(count_s++, stlab::default_executor);
    };

    when_all_typecheck(
        [](auto... args) { for_each_argument([](auto x) { cout << x << endl; }, args...); }, fv(),
        fi(), fv(), fi());
}

/**************************************************************************************************/

TEST_CASE("future_when_all_int_int") {
    auto fi = [] {
        static int count_s{0};
        return stlab::make_ready_future<int>(count_s++, stlab::default_executor);
    };

    auto f = when_all(
        stlab::default_executor,
        [](auto... args) { for_each_argument([](auto x) { cout << x << "\n"; }, args...); }, fi(),
        fi());

    await(std::move(f));
}

/**************************************************************************************************/

TEST_CASE("future_when_all_void") {
    auto fv = [] { return stlab::make_ready_future(stlab::default_executor); };

    auto f = when_all(stlab::default_executor, []() { cout << "done!\n"; }, fv());

    await(std::move(f));
}

/**************************************************************************************************/

TEST_CASE("future_when_all_void_int") {
    auto fv = [] { return stlab::make_ready_future(stlab::default_executor); };
    auto fi = [] {
        static int count_s{0};
        return stlab::make_ready_future<int>(count_s++, stlab::default_executor);
    };

    auto f = when_all(
        stlab::default_executor,
        [](auto... args) { for_each_argument([](auto x) { cout << x << "\n"; }, args...); }, fv(),
        fi());

    await(std::move(f));
}

/**************************************************************************************************/

TEST_CASE("future_when_all_int_void") {
    auto fv = [] { return stlab::make_ready_future(stlab::default_executor); };
    auto fi = [] {
        static int count_s{0};
        return stlab::make_ready_future<int>(count_s++, stlab::default_executor);
    };

    auto f = when_all(
        stlab::default_executor,
        [](auto... args) { for_each_argument([](auto x) { cout << x << "\n"; }, args...); }, fi(),
        fv());

    await(std::move(f));
}

/**************************************************************************************************/

TEST_CASE("future_when_all_int_void_string_void_bool_void") {
    auto fv = [] { return stlab::make_ready_future(stlab::default_executor); };
    auto fi = [] { return stlab::make_ready_future<int>(42, stlab::default_executor); };
    auto fs = [] {
        return stlab::make_ready_future<std::string>("Hello, world!", stlab::default_executor);
    };
    auto fb = [] { return stlab::make_ready_future<bool>(true, stlab::default_executor); };

    auto f = when_all(
        stlab::default_executor,
        [](const auto&... args) {
            for_each_argument([](const auto& x) { cout << x << "\n"; }, args...);
        },
        fi(), fv(), fs(), fv(), fb(), fv());

    await(std::move(f));
}

/**************************************************************************************************/

TEST_CASE("future_when_any_void") {
    auto fv = [] { return stlab::make_ready_future(stlab::default_executor); };

    auto f = when_any(
        stlab::default_executor, [](size_t index) { std::cout << "f: " << index << '\n'; }, fv(),
        fv(), fv(), fv(), fv(), fv());

    await(std::move(f));
}

/**************************************************************************************************/
