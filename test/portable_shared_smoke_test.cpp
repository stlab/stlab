/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/default_executor.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>

#include <doctest/doctest.h>

TEST_CASE("portable_shared_core_public_api_submits_and_waits") {
    std::condition_variable ready;
    std::mutex mutex;
    bool done = false;

    stlab::default_executor([&]() noexcept {
        {
            std::lock_guard<std::mutex> lock{mutex};
            done = true;
        }
        ready.notify_one();
    });

    std::unique_lock<std::mutex> lock{mutex};
    const auto completed = stlab::invoke_waiting(
        [&] { return ready.wait_for(lock, std::chrono::seconds(5), [&] { return done; }); });

    REQUIRE(completed);
    REQUIRE(done);
}
