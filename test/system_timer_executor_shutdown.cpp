#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/system_timer.hpp>
#include <stlab/pre_exit.hpp>

#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <utility>

/// Verifies pending timer captures are released before executor shutdown waits for active tasks.
int main() {
    auto promise = std::make_shared<std::promise<void>>();
    auto result = promise->get_future();
    stlab::system_timer(std::chrono::hours(1), [owner = std::move(promise)]() noexcept {});
    std::promise<void> started;
    auto started_result = started.get_future();
    std::promise<void> finished;
    auto finished_result = finished.get_future();
    std::atomic<bool> released{false};
    stlab::default_executor(
        [result = std::move(result), &started, &finished, &released]() mutable noexcept {
            started.set_value();
            try {
                result.get();
            } catch (const std::future_error& error) {
                released = error.code() == std::make_error_code(std::future_errc::broken_promise);
            }
            finished.set_value();
        });
    if (started_result.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        std::terminate();
    std::promise<void> timer_started;
    auto timer_started_result = timer_started.get_future();
    stlab::system_timer(std::chrono::nanoseconds::zero(),
                        [finished_result = std::move(finished_result), &timer_started]() noexcept {
                            timer_started.set_value();
                            finished_result.wait();
                        });
    if (timer_started_result.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        std::terminate();
    stlab::pre_exit();
    return released ? 0 : 1;
}
