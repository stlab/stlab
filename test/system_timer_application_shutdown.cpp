#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/system_timer.hpp>
#include <stlab/pre_exit.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <future>
#include <memory>
#include <utility>

namespace {
std::array<int, 3> order{};
std::size_t position = 0;
std::promise<void>* application_signal = nullptr;

/// Records an application handler registered during pre-exit processing.
void nested_handler() noexcept { order[position++] = 3; }

/// Runs first and adds a new handler at the top of the public LIFO stack.
void last_handler() noexcept {
    order[position++] = 2;
    stlab::at_pre_exit(nested_handler);
}

/// Signals the active timer before the shared core cleanup handler runs.
void first_handler() noexcept {
    order[position++] = 1;
    application_signal->set_value();
}
} // namespace

/// Verifies public LIFO handlers precede core cleanup and callbacks can first-use all executors
/// while timer shutdown is joining them.
int main() {
    std::promise<void> application_release;
    auto application_result = application_release.get_future();
    application_signal = &application_release;
    std::promise<void> timer_closing;
    auto closing_result = timer_closing.get_future();
    auto deleter = [&](int* p) {
        delete p;
        timer_closing.set_value();
    };
    stlab::system_timer(std::chrono::hours(1), [owner = std::unique_ptr<int, decltype(deleter)>(
                                                    new int, deleter)]() noexcept {});
    std::promise<void> started;
    auto started_result = started.get_future();
    std::atomic<int> executor_calls{0};
    stlab::system_timer(
        std::chrono::nanoseconds::zero(),
        [application_result = std::move(application_result),
         closing_result = std::move(closing_result), &started, &executor_calls]() noexcept {
            started.set_value();
            application_result.wait();
            closing_result.wait();
            stlab::high_executor([&executor_calls]() noexcept { ++executor_calls; });
            stlab::default_executor([&executor_calls]() noexcept { ++executor_calls; });
            stlab::low_executor([&executor_calls]() noexcept { ++executor_calls; });
        });
    if (started_result.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        std::terminate();
    stlab::at_pre_exit(first_handler);
    stlab::at_pre_exit(last_handler);
    stlab::pre_exit();
    return order == std::array<int, 3>{2, 3, 1} && executor_calls == 3 ? 0 : 1;
}
