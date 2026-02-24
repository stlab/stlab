
#include <stlab/concurrency/system_timer.hpp>

#include <chrono>
#include <utility>

#include <doctest/doctest.h>

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/immediate_executor.hpp>


/**************************************************************************************************/

using namespace stlab;

/**************************************************************************************************/

TEST_CASE("system_timer_cancellation") {
    // Test to ensure that a task scheduled after pre_exit() is not executed
    system_timer(std::chrono::hours(1), []() noexcept { FAIL(""); });

    auto [task, future] = package<int()>(immediate_executor, [] { return 42; });
    system_timer(std::chrono::seconds(0), std::move(task));

    REQUIRE(await(std::move(future)) == 42);
}

/**************************************************************************************************/
