/*
    Copyright 2017 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/
/**************************************************************************************************/

// stdc++
#include <array>
#include <cstddef>
#include <functional>
#include <iostream>
#include <string>
#include <utility>

// boost
#include <doctest/doctest.h>

// stlab
#include <stlab/concurrency/task.hpp>
#include <stlab/test/model.hpp>

/**************************************************************************************************/

using namespace stlab;

/**************************************************************************************************/

TEST_CASE("task_argument_test") {
    {
        task<void(const regular&)> t([](const regular& a) { REQUIRE(a._x == 42); });
        t(regular{42}); // rvalue->const &
        regular a{42};
        t(a); // lvalue->const &
        const regular b{42};
        t(b); // const lvalue->const &
    }

    {
        task<void(regular&)> t([](regular& a) { REQUIRE(a._x == 42); });
        regular a{42};
        t(a); // lvalue->&
    }

    {
        task<void(move_only&&)> t([](move_only&& a) { REQUIRE(a.member() == 42); });
        t(move_only{42}); // rvalue->&&
    }

    {
        task<void(move_only)> t([](move_only a) { REQUIRE(a.member() == 42); });
        t(move_only{42}); // rvalue->value
    }

    {
        task<void(regular)> t([](const regular& a) { REQUIRE(a._x == 42); });
        t(regular{42}); // rvalue->value
        regular a{42};
        t(a); // lvalue->value
        const regular b{42};
        t(b); // const lvalue->value
    }

    // These test mismatched task signature to lambda signature
    {
        task<void(const regular&)> t([](const regular& a) { REQUIRE(a._x == 42); });
        t(regular{42}); // rvalue->const &
        regular a{42};
        t(a); // lvalue->const &
        const regular b{42};
        t(b); // const lvalue->const &
    }

    {
        task<void(regular&)> t([](const regular& a) { REQUIRE(a._x == 42); });
        regular a{42};
        t(a); // lvalue->&
    }

    {
        task<void(regular&)> t([](const regular& a) { REQUIRE(a._x == 42); });
        regular a{42};
        t(a); // lvalue->&
    }

    {
        task<void(move_only&&)> t([](const move_only& a) { REQUIRE(a.member() == 42); });
        t(move_only{42}); // rvalue->&&
    }

    {
        task<void(move_only&&)> t([](move_only a) { REQUIRE(a.member() == 42); });
        t(move_only{42}); // rvalue->&&
    }

    {
        task<void(move_only)> t([](const move_only& a) { REQUIRE(a.member() == 42); });
        t(move_only{42}); // rvalue->value
    }

    {
        task<void(move_only)> t([](move_only&& a) { REQUIRE(a.member() == 42); });
        t(move_only{42}); // rvalue->value
    }

    {
        task<void(regular)> t([](const regular& a) { REQUIRE(a._x == 42); });
        t(regular{42}); // rvalue->value
        regular a{42};
        t(a); // lvalue->value
        const regular b{42};
        t(b); // const lvalue->value
    }

    {
        task<void(regular)> t([](regular&& a) { REQUIRE(a._x == 42); });
        t(regular{42}); // rvalue->value
        regular a{42};
        t(a); // lvalue->value
        const regular b{42};
        t(b); // const lvalue->value
    }
}

/**************************************************************************************************/

TEST_CASE("task_nullary_tests") {
    {
        task<regular()> t([] { return regular(42); });
        REQUIRE(t()._x == 42);
    }

    {
        task<regular()> x([] { return regular(42); });
        task<regular()> y = std::move(x);
        REQUIRE(y()._x == 42);
    }

    {
        task<regular()> x([] { return regular(42); });
        task<regular()> y([] { return regular(99); });
        swap(x, y);
        REQUIRE(x()._x == 99);
        REQUIRE(y()._x == 42);
    }

    {
        task<move_only()> t([] { return move_only(42); });
        REQUIRE(t().member() == 42);
    }

    {
        task<move_only()> x([] { return move_only(42); });
        task<move_only()> y = std::move(x);
        REQUIRE(y().member() == 42);
    }
}

/**************************************************************************************************/

TEST_CASE("task_copyable_nullary_tests") {
    int value(42);

    {
        task<int()> t([_value = value] { return _value; });
        REQUIRE(t() == value);
    }

    {
        task<int()> x([_value = value] { return _value; });
        task<int()> y = std::move(x);
        REQUIRE(y() == value);
    }

    {
        task<regular()> x([_value = value] { return regular(_value); });
        task<regular()> y([_value = 99] { return regular(_value); });
        swap(x, y);
        REQUIRE(x()._x == 99);
        REQUIRE(y()._x == 42);
    }

    {
        task<move_only()> t([_value = value] { return move_only(_value); });
        REQUIRE(t().member() == value);
    }

    {
        task<move_only()> x([_value = value] { return move_only(_value); });
        task<move_only()> y = std::move(x);
        REQUIRE(y().member() == value);
    }
}

/**************************************************************************************************/

TEST_CASE("task_moveable_nullary_tests") {
    {
        move_only value(42);
        task<int()> t([_value = std::move(value)] { return _value.member(); });
        REQUIRE(t() == 42);
    }

    {
        move_only value(42);
        task<int()> x([_value = std::move(value)] { return _value.member(); });
        task<int()> y = std::move(x);
        REQUIRE(y() == 42);
    }

    {
        move_only value0(42);
        move_only value1(99);
        task<move_only()> x([_value = std::move(value0)]() mutable { return std::move(_value); });
        task<move_only()> y([_value = std::move(value1)]() mutable { return std::move(_value); });
        swap(x, y);
        REQUIRE(x().member() == 99);
        REQUIRE(y().member() == 42);
    }

    {
        move_only value(42);
        task<move_only()> t([_value = std::move(value)]() mutable { return std::move(_value); });
        REQUIRE(t().member() == 42);
    }

    {
        move_only value(42);
        task<move_only()> x([_value = std::move(value)]() mutable { return std::move(_value); });
        task<move_only()> y = std::move(x);
        REQUIRE(y().member() == 42);
    }
}

/**************************************************************************************************/

TEST_CASE("task_n_ary_tests") {
    {
        task<int(int)> t([](int x) { return x; });
        REQUIRE(t(42) == 42);
    }

    {
        task<int(int, float)> t([](int x, float y) { return x + static_cast<int>(y); });
        REQUIRE(t(21, 21.f) == 42);
    }

    {
        task<int(int, float)> x([](int x, float y) { return x + static_cast<int>(y); });
        task<int(int, float)> y([](int x, float y) { return x * static_cast<int>(y); });
        swap(x, y);
        REQUIRE(x(10, 10.f) == 100);
        REQUIRE(y(10, 10.f) == 20);
    }

    {
        task<int(int, float, std::string)> t([](int x, float y, const std::string& z) {
            return x + static_cast<int>(y) + static_cast<int>(z.size());
        });
        REQUIRE(t(20, 20.f, "00") == 42);
    }

    {
        task<int(move_only, int)> x([](move_only m, int i) { return m.member() + i; });
        REQUIRE(x(move_only(40), 2) == 42);
    }
}

/**************************************************************************************************/

struct large_model {
    std::array<char, 512> buffer{42};
    auto operator()() const { return buffer[0]; }
};

TEST_CASE("task_type_tests") {
    {
        // empty model
        task<void()> t;
        REQUIRE(!t);
        REQUIRE_THROWS_AS(t(), std::bad_function_call);
        std::cout << t.target_type().name() << '\n';
        REQUIRE(t.target<void>() == nullptr);
    }

    {
        // small model
        auto small_model = [] { return 42; };
        task<int()> t = small_model;
        REQUIRE(t);
        REQUIRE(t() == 42);
        std::cout << t.target_type().name() << '\n';
        REQUIRE(t.target<decltype(small_model)>() != nullptr);

        // null assignment
        t = nullptr;
        REQUIRE(!t);
    }

    {
        task<int()> t(nullptr);
        REQUIRE(!t);
    }

    {
        // large model
        task<int()> t = large_model();
        REQUIRE(t);
        REQUIRE(t() == 42);
        std::cout << t.target_type().name() << '\n';
        REQUIRE(t.target<decltype(large_model())>() != nullptr);
    }
}

/**************************************************************************************************/

TEST_CASE("task_equality_tests") {
    {
        task<void()> a;
        REQUIRE(a == std::nullptr_t());
    }
    {
        task<void()> a;
        REQUIRE(std::nullptr_t() == a);
    }

    {
        task<void()> a([] {});
        REQUIRE(a != std::nullptr_t());
    }
    {
        task<void()> a([] {});
        REQUIRE(std::nullptr_t() != a);
    }
}

/**************************************************************************************************/

// These tests should fail to compile.
#if 0
TEST_CASE("task_fail") {
    const task<void()> c{[] {}}; // const task
    task<void()> t{std::move(c)};
}
#endif

/**************************************************************************************************/
