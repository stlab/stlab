/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <stlab/concurrency/tuple_algorithm.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <list>
#include <tuple>
#include <vector>

using namespace stlab;

TEST_CASE("tuple_find_empty_tuple") {
    std::tuple<> t;
    REQUIRE(stlab::tuple_find(t, [](const auto&) { return false; }) == std::size_t(1));
}

TEST_CASE("tuple_find_one_element_tuple_that_fails") {
    std::tuple<std::vector<int>> t;
    REQUIRE(stlab::tuple_find(t, [](const auto& t) { return !t.empty(); }) == std::size_t(1));
}

TEST_CASE("tuple_find_one_element_tuple_that_succeeds") {
    std::tuple<std::vector<int>> t;
    REQUIRE(stlab::tuple_find(t, [](const auto& t) { return t.empty(); }) == std::size_t(0));
}

TEST_CASE("tuple_find_two_element_tuple_that_fails") {
    std::tuple<std::vector<int>, std::list<double>> t;
    REQUIRE(stlab::tuple_find(t, [](const auto& t) { return !t.empty(); }) == std::size_t(2));
}

TEST_CASE("tuple_find_two_element_tuple_first_succeeds") {
    std::tuple<std::vector<int>, std::list<double>> t;
    REQUIRE(stlab::tuple_find(t, [](const auto& t) { return t.empty(); }) == std::size_t(0));
}

TEST_CASE("tuple_find_two_element_tuple_second_succeeds") {
    std::tuple<std::vector<int>, std::list<double>> t;
    std::get<0>(t).push_back(0);
    REQUIRE(stlab::tuple_find(t, [](const auto& t) { return t.empty(); }) == std::size_t(1));
}

TEST_CASE("tuple_for_each_empty_tuple") {
    std::tuple<> t;
    std::size_t count = 0;
    stlab::tuple_for_each(t, [&count](auto&) { ++count; });
    REQUIRE(count == std::size_t(0));
}

TEST_CASE("tuple_for_each_one_element_tuple") {
    std::tuple<std::vector<int>> t;
    std::get<0>(t) = {
        1, 2}; // workaround for gcc 5 missing capability to support initializer lists for tuples
    std::size_t count = 0;
    stlab::tuple_for_each(t, [&count](auto& c) {
        ++count;
        c.pop_back();
    });
    REQUIRE(std::get<0>(t).size() == std::size_t(1));
    REQUIRE(count == std::size_t(1));
}

TEST_CASE("tuple_for_each_two_element_tuple") {
    std::tuple<std::vector<int>, std::list<double>> t;
    std::get<0>(t) = {
        1, 2}; // workaround for gcc 5 missing capability to support initializer lists for tuples
    std::get<1>(t) = {2.4, 3.5};

    std::size_t count = 0;
    stlab::tuple_for_each(t, [&count](auto& c) {
        ++count;
        c.pop_back();
    });
    REQUIRE(std::get<0>(t).size() == std::size_t(1));
    REQUIRE(std::get<1>(t).size() == std::size_t(1));
    REQUIRE(count == std::size_t(2));
}
