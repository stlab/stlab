/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/pre_exit.hpp>

#include <utility>

static_assert(STLAB_EXECUTION_SHARED() == STLAB_PACKAGE_EXPECT_EXECUTION_SHARED,
              "Execution's installed configuration must match its library type.");

/// Verifies that the STLab package supplies its execution dependency.
int main() {
    auto value = stlab::async(stlab::default_executor, [] { return 42; });
    const auto result = stlab::await(std::move(value));
    stlab::pre_exit();
    return result == 42 ? 0 : 1;
}
