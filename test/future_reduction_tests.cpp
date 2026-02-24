/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <doctest/doctest.h>

#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/immediate_executor.hpp>
#include <stlab/concurrency/ready_future.hpp>

using namespace stlab;

TEST_CASE("void_void_reduction") {
    auto f = make_ready_future(immediate_executor) |
             [] { return make_ready_future(immediate_executor); };
    REQUIRE(!f.exception());
}
