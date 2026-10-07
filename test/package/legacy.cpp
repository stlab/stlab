/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/task.hpp>
#include <stlab/pre_exit.hpp>

#include <chrono>
#include <future>
#include <utility>

static_assert(STLAB_EXECUTION_SHARED() == STLAB_PACKAGE_EXPECT_EXECUTION_SHARED,
              "Execution's installed configuration must match its library type.");

/// Verifies that the legacy core link target supplies only execution APIs.
int main() {
    std::promise<int> completion;
    auto result = completion.get_future();
    stlab::task<void() noexcept> task(
        [completion = std::move(completion)]() mutable noexcept { completion.set_value(42); });
    stlab::default_executor(std::move(task));
    const bool ready = result.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    const bool correct = ready && result.get() == 42;
    stlab::pre_exit();
    return correct ? 0 : 1;
}
