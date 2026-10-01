#include <stlab/concurrency/system_timer.hpp>
#include <stlab/pre_exit.hpp>

#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <memory>

/// Verifies shutdown cancels pending captures and waits for a committed callback.
int main() {
    std::atomic<int> destroyed{0};
    std::atomic<int> invoked{0};
    auto deleter = [&](int* p) {
        ++destroyed;
        delete p;
    };
    stlab::system_timer(std::chrono::nanoseconds::max(),
                        [owned = std::unique_ptr<int, decltype(deleter)>(new int, deleter),
                         &invoked]() noexcept { ++invoked; });
    std::promise<void> started;
    auto started_result = started.get_future();
    std::promise<void> release;
    auto release_result = release.get_future();
    std::atomic<bool> completed{false};
    stlab::system_timer(std::chrono::nanoseconds::zero(), [&]() noexcept {
        started.set_value();
        release_result.wait();
        completed = true;
    });
    if (started_result.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        std::terminate();
    auto closing = std::async(std::launch::async, [] { stlab::pre_exit(); });
    const bool waited =
        closing.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout;
    release.set_value();
    if (closing.wait_for(std::chrono::seconds(5)) != std::future_status::ready) std::terminate();
    closing.get();
    return waited && completed && destroyed == 1 && invoked == 0 ? 0 : 1;
}
