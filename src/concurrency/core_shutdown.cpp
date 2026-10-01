/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include "detail/core_shutdown.hpp"
#include "detail/system_timer_shutdown.hpp"

#include <stlab/config.hpp>

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <exception>
#include <mutex>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {
namespace {

/// One lazily registered handler and the fixed-size registry of initialized executor backends.
struct core_shutdown_state {
    std::once_flag registration;
    std::atomic<bool> registered{false};
    std::atomic<bool> closed{false};
    std::mutex mutex;
    std::array<core_executor_cleanup, 3> executors{};
    std::size_t count = 0;
};

/// Returns lifecycle state without constructing scheduler resources.
auto state() -> core_shutdown_state& {
    static core_shutdown_state result;
    return result;
}

/// Diagnoses core use after completed cleanup in every build configuration.
void check_open(const core_shutdown_state& value) noexcept {
    if (value.closed.load(std::memory_order_acquire)) {
        assert(false && "core service use after pre_exit() or core cleanup");
        std::terminate();
    }
}

/// Cancels timers and joins callbacks before draining initialized default executors.
///
/// - Precondition: if cleanup blocks the main thread, workers and timer callbacks do not
///   synchronously require main-queue progress.
/// - Postcondition: main-queue admission and servicing are unchanged.
/// - Complexity: linear in pending timers, accepted executor work, and initialized workers.
void shutdown_core() noexcept {
    shutdown_system_timer();
    auto& value = state();
    for (;;) {
        core_executor_cleanup cleanup = nullptr;
        {
            std::scoped_lock lock(value.mutex);
            if (value.count == 0) {
                value.closed.store(true, std::memory_order_release);
                return;
            }
            cleanup = value.executors[--value.count];
        }
        cleanup();
    }
}

} // namespace

void register_core_shutdown() {
    auto& value = state();
    check_open(value);
    if (!value.registered.load(std::memory_order_acquire)) {
        std::call_once(value.registration, [&] {
            // /EHsc assumes extern "C" calls cannot throw; preserve call_once's retry cleanup.
            register_core_shutdown_handler(shutdown_core);
            value.registered.store(true, std::memory_order_release);
        });
        check_open(value);
    }
}

void register_core_executor_cleanup(core_executor_cleanup cleanup) {
    auto& value = state();
    std::scoped_lock lock(value.mutex);
    check_open(value);
    if (!cleanup || value.count == value.executors.size()) {
        assert(false && "invalid or excess core executor cleanup registrations");
        std::terminate();
    }
    value.executors[value.count++] = cleanup;
}

void complete_core_shutdown() noexcept { state().closed.store(true, std::memory_order_release); }

} // namespace detail
STLAB_VERSION_NAMESPACE_END()
} // namespace stlab
