#include <stlab/concurrency/default_executor.hpp>
#include <stlab/pre_exit.hpp>

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <future>

#if defined(_MSC_VER)
#include <crtdbg.h>
#include <stdlib.h> // NOLINT(modernize-deprecated-headers): Windows CRT extensions.
#endif

namespace {
std::promise<void> release;

/// Releases the accepted low-priority task before shared core cleanup begins.
void stop() noexcept { release.set_value(); }

/// Chains accepted work across every priority while shared core cleanup drains the executor.
struct continuation_chain {
    std::atomic<std::size_t> completed{0};

    /// Posts one continuation, which posts its successor before returning.
    void post(std::size_t index) noexcept {
        auto next = [this, index]() noexcept {
            completed.fetch_add(1, std::memory_order_relaxed);
            if (index + 1 != 4096) post(index + 1);
        };
        switch (index % 3) {
            case 0:
                stlab::high_executor(next);
                break;
            case 1:
                stlab::default_executor(next);
                break;
            case 2:
                stlab::low_executor(next);
                break;
        }
    }
};
} // namespace

/// Verifies initialized and first-used priorities remain available to accepted continuations.
int main() {
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    if (std::signal(SIGABRT, [](int) { std::_Exit(2); }) == SIG_ERR) std::_Exit(1);
    std::set_terminate([] { std::_Exit(2); });

    continuation_chain chain;
    auto stopped = release.get_future();
    std::promise<void> low_started;
    auto low_ready = low_started.get_future();
    stlab::low_executor([&]() noexcept {
        low_started.set_value();
        stopped.wait();
        chain.post(0);
    });
    low_ready.wait();

    stlab::high_executor([]() noexcept {});

    stlab::at_pre_exit(stop);
    stlab::pre_exit();
    return chain.completed.load(std::memory_order_relaxed) == 4096 ? 0 : 1;
}
