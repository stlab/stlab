/*
    Copyright 2013 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/
/**************************************************************************************************/

// #include <adobe/config.hpp>

#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

#include <doctest/doctest.h>
// #include <adobe/test/check_regular.hpp>
// #include <adobe/test/check_less_than_comparable.hpp>
// #include <adobe/test/check_null.hpp>

#include <stlab/copy_on_write.hpp>
#include <stlab/utility.hpp>
// #include <adobe/memory.hpp>

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

template <typename R, typename T>
auto make_value(const T& x) -> R {
    return R(x);
}

/**************************************************************************************************/

template <>
auto make_value(const long& x) -> std::string {
    std::stringstream s;
    s << x;
    return std::string(s.str());
}

/**************************************************************************************************/

template <typename CowType>
void test_copy_on_write() {
    typename CowType::value_type (*mv)(const long&) =
        &make_value<typename CowType::value_type, long>;

    // Test default constructor
    {
        CowType value_0;
    }

    // Test basic concept requirements
    {
        CowType value_1(mv(1)); // allocation
        CowType value_2(mv(2)); // allocation
        CowType value_3(mv(3)); // allocation

        // regular
        // adobe::check_regular(value_1);

        // operator<
        // adobe::check_less_than_comparable(value_1, value_2, value_3, std::less<CowType>());

        // operator>
        // adobe::check_less_than_comparable(value_3, value_2, value_1, std::greater<CowType>());

        CowType value_test(mv(1)); // allocation

        REQUIRE(value_1 == value_test);
        REQUIRE(value_2 != value_test);

        REQUIRE(value_test.unique());

        value_test = value_2; // deallocation

        REQUIRE(!value_test.unique());
        REQUIRE(value_test.identity(value_2));
    }

    // Test basic move semantics
    {
        CowType value_1(mv(42)); // allocation
        CowType value_2(mv(21)); // allocation
        CowType value_move(std::move(value_1));

        // REQUIRE(value_move != value_1, "move failure");

        value_move = std::move(value_2); // deallocation

        // REQUIRE(value_move != value_2, "move failure");
        // REQUIRE(value_1 == value_2, "move failure"); // both should be object_m == 0
    }

    // Test copy-assignment using null object_m
    {
        CowType foo(mv(1)); // allocation
        CowType bar(std::move(foo));

        foo = mv(2); // allocation
    }

    // Test copy-assignment using non-null object_m
    {
        CowType foo(mv(5)); // allocation
        CowType bar(foo);

        REQUIRE(bar.identity(foo));

        bar = mv(6); // allocation

        REQUIRE(bar.unique());
        REQUIRE(foo.unique());
    }

    // Test move-assignment using null object_m
    {
        CowType foo(mv(1)); // allocation
        CowType bar(std::move(foo));
        typename CowType::value_type value(mv(2));

        foo = std::move(value); // allocation
    }

    // Test move-assignment using unique instance
    {
        CowType foo(mv(1)); // allocation
        typename CowType::value_type value(mv(2));

        foo = std::move(value);
    }

    // Test move-assignment using new allocation
    {
        CowType foo(mv(1)); // allocation
        CowType bar(foo);
        typename CowType::value_type value(mv(2));

        foo = std::move(value); // allocation
    }

    // Test write() using unique instance
    {
        CowType foo(mv(1)); // allocation

        foo.write() = typename CowType::value_type(mv(2));
    }

    // Test write() using new allocation
    {
        CowType foo(mv(1)); // allocation
        CowType bar(foo);

        foo.write() = typename CowType::value_type(mv(2)); // allocation
    }

    // Test read()
    {
        CowType foo(mv(1)); // allocation

        REQUIRE(foo.read() == typename CowType::value_type(mv(1)));
        REQUIRE(static_cast<typename CowType::value_type>(foo) ==
                typename CowType::value_type(mv(1)));
        REQUIRE(*foo == typename CowType::value_type(mv(1)));
        REQUIRE(*(foo.operator->()) == typename CowType::value_type(mv(1)));
    }

    // Test swap
    {
        CowType foo(mv(1)); // allocation
        CowType bar(mv(2)); // allocation

        swap(foo, bar);

        REQUIRE(foo.read() == typename CowType::value_type(mv(2)));
        REQUIRE(bar.read() == typename CowType::value_type(mv(1)));
    }
}

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

TEST_CASE("copy_on_write_interface") {
    using namespace stlab;

    {
        // noexcept-correct default construction:
        static_assert(noexcept(int()), "int() had better be noexcept!");
        static_assert(noexcept(copy_on_write<int>()),
                      "Default c'tor should be noexcept if the type's c'tor is.");
        struct not_noexcept_ctor {
            not_noexcept_ctor() { throw std::exception(); }
        };
        static_assert(!noexcept(not_noexcept_ctor()),
                      "Default c'tor should be noexcept if the type is noexcept.");
        static_assert(!noexcept(copy_on_write<not_noexcept_ctor>()),
                      "Default c'tor shouldn't be noexcept if the type's c'tor isn't.");
        REQUIRE_THROWS_AS(([] { copy_on_write<not_noexcept_ctor> ctor_will_throw; }()),
                          std::exception);
    }

    {
        // default construction
        copy_on_write<int> a;
        copy_on_write<int> b;

        REQUIRE(a.identity(b));
        REQUIRE(a == 0);
    }

    {
        // emplace construction
        copy_on_write<int> a(10);
        copy_on_write<std::pair<int, int>> b(10, 20);
        copy_on_write<std::tuple<int, int, int>> c(10, 20, 30);

        REQUIRE(a == 10);
        REQUIRE((b == std::make_pair(10, 20)));
        REQUIRE((c == std::make_tuple(10, 20, 30)));
    }

    {
        // copy construction
        copy_on_write<int> a = 3;
        auto b = copy(a);
        REQUIRE(a.identity(b));
        REQUIRE(b == 3);
    }

    {
        // move construction
        copy_on_write<int> a = 3;
        copy_on_write<int> b = std::move(a);
        a = 0;
        REQUIRE(!a.identity(b));
        REQUIRE(b == 3);
        REQUIRE(a == 0);
    }

    {
        // copy assignment
        copy_on_write<int> a = 3;
        copy_on_write<int> b;
        b = a;
        REQUIRE(a.identity(b));
        REQUIRE(b == 3);
    }

    {
        // move assignment
        copy_on_write<int> a = 3;
        copy_on_write<int> b;
        b = std::move(a);
        a = 0;
        REQUIRE(!a.identity(b));
        REQUIRE(b == 3);
        REQUIRE(a == 0);
    }

    {
        // value assignment
        copy_on_write<int> a;
        a = 3;
        REQUIRE(a == 3);
    }

    {
        // write
        copy_on_write<std::pair<int, int>> a(1, 2);
        copy_on_write<std::pair<int, int>> b = a;
        REQUIRE(a.identity(b));
        a.write().first = 3;
        REQUIRE(!a.identity(b));
        REQUIRE(a == std::make_pair(3, 2));
        REQUIRE(b == std::make_pair(1, 2));
    }

    {
        // read
        copy_on_write<std::pair<int, int>> a(1, 2);
        REQUIRE((a.read().first == 1 && a.read().second == 2));
    }

    {
        // implicit conversion
        copy_on_write<std::pair<int, int>> a(1, 2);
        std::pair<int, int> b = a;
        REQUIRE(b == std::make_pair(1, 2));
    }

    {
        // operator * and ->
        copy_on_write<std::pair<int, int>> a(1, 2);
        REQUIRE((a->first == 1 && a->second == 2));
        REQUIRE(((*a).first == 1 && (*a).second == 2));
    }

    {
        // unique
        copy_on_write<std::pair<int, int>> a(1, 2);
        REQUIRE(a.unique());
        {
            auto b = copy(a);
            REQUIRE((!a.unique() && !b.unique()));
        }
        REQUIRE(a.unique());
    }

    // identity (tested above)

    // swap
    {
        copy_on_write<int> a(1);
        copy_on_write<int> b(2);
        swap(a, b);
        REQUIRE(((a == 2) && (b == 1)));
    }

    // comparisons
    {
        copy_on_write<int> a(1);
        copy_on_write<int> b(1);
        copy_on_write<int> c(2);

        REQUIRE(((a == b) && (a != c) && !(a == c) && !(a != b)));
        REQUIRE(((a == 1) && (a != 2) && !(a == 2) && !(a != 1)));
        REQUIRE(((1 == b) && (1 != c) && !(1 == c) && !(1 != b)));

        REQUIRE((!(a < b) && (a < c)));
        REQUIRE((!(a < 1) && (a < 2)));
        REQUIRE((!(1 < 1) && (1 < 2)));

        REQUIRE((!(a > b) && (c > a)));
        REQUIRE((!(a > 1) && (c > 1)));
        REQUIRE((!(1 > b) && (2 > a)));

        REQUIRE(((a <= b) && !(c <= a)));
        REQUIRE(((a <= 1) && !(c <= 1)));
        REQUIRE(((1 <= b) && !(2 <= a)));

        REQUIRE(((a >= b) && !(a >= c)));
        REQUIRE(((a >= 1) && !(a >= 2)));
        REQUIRE(((1 >= b) && !(1 >= c)));
    }
}

/**************************************************************************************************/

TEST_CASE("copy_on_write_test") {
    // test nonmovable type with capture_allocator
    test_copy_on_write<stlab::copy_on_write<int>>();

    // test movable type with capture_allocator
    test_copy_on_write<stlab::copy_on_write<std::string>>();
}

/**************************************************************************************************/
