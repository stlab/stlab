#include <stlab/concurrency/system_timer.hpp>
#include <stlab/concurrency/task.hpp>
#include <stlab/pre_exit.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <future>
#include <memory>
#include <new>
#include <utility>

namespace {
std::atomic<bool> fail_next{false};
}

/// Injects a single real allocation failure during timer preparation.
void* operator new(std::size_t size) {
    if (fail_next.exchange(false)) throw std::bad_alloc();
    if (auto* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}

/// Releases allocation storage.
void operator delete(void* address) noexcept { std::free(address); }

/// Releases sized allocation storage.
void operator delete(void* address, std::size_t) noexcept { std::free(address); }

/// Verifies cold and warm resource failures retain move-only targets and permit successful retry.
///
/// - Precondition: static core linkage on Windows, so the core uses this executable's operator new.
int main() {
    std::set_terminate([] { std::_Exit(2); });
    bool passed = true;
    for (int attempt = 0; attempt != 2; ++attempt) {
        bool reported = false;
        for (int pending = 0; pending != 256 && !reported; ++pending) {
            auto invoked = std::make_shared<bool>(false);
            stlab::task<void() noexcept> target = [owned = std::make_unique<int>(42),
                                                   invoked]() noexcept {
                *invoked = owned && *owned == 42;
            };
            fail_next = true;
            try {
                stlab::system_timer(std::chrono::hours(1), std::move(target));
            } catch (const std::bad_alloc&) {
                reported = true;
            }
            fail_next = false;
            if (reported) {
                target();
                passed = passed && *invoked;
            }
        }
        passed = passed && reported;

        auto completed = std::make_shared<std::promise<void>>();
        auto ready = completed->get_future();
        stlab::system_timer(std::chrono::nanoseconds(0),
                            [completed]() noexcept { completed->set_value(); });
        if (ready.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
            stlab::pre_exit();
            return 1;
        }
    }
    stlab::pre_exit();
    return passed ? 0 : 1;
}
