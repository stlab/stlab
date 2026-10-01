/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include "detail/core_shutdown.hpp"
#include "detail/system_timer_shutdown.hpp"
#include "detail/timer_common.hpp"

#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/system_timer.hpp>
#include <stlab/concurrency/task.hpp>
#include <stlab/config.hpp>

#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/threading.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

#if defined(__EMSCRIPTEN_PTHREADS__)
#include <condition_variable>
#endif

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {
namespace {

using clock_type = std::chrono::steady_clock;

/// Owns an accepted task until registration, invocation, or cancellation finishes.
struct timer_record {
    std::atomic<unsigned> references{2}; // Pending list and registration/timeout callback.
    timer_record* previous = nullptr;
    timer_record* next = nullptr;
    clock_type::time_point accepted;
    std::int64_t delay_ns;
    long timeout_id = 0;
    bool armed = false;
    task<void() noexcept> target;

    /// Captures the acceptance instant before registration is proxied.
    explicit timer_record(std::int64_t delay) : accepted(clock_type::now()), delay_ns(delay) {}

    /// Releases one scheduling owner.
    void release() noexcept {
        if (references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
    }

    /// Returns the remaining delay without overflowing an absolute deadline.
    auto remaining() const noexcept -> std::int64_t {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now() - accepted)
                .count();
        return elapsed >= delay_ns ? 0 : delay_ns - std::max<std::int64_t>(0, elapsed);
    }
};

/// Serializes timer admission and tracks accepted registration/timeout callbacks.
struct timer_state {
    std::mutex mutex;
    timer_record* head = nullptr;
    bool closed = false;

    /// Links a prepared record into the accepted list.
    void insert(timer_record& record) noexcept {
        record.next = head;
        if (head) head->previous = &record;
        head = &record;
    }

    /// Removes a record from the accepted list.
    void remove(timer_record& record) noexcept {
        if (record.previous)
            record.previous->next = record.next;
        else
            head = record.next;
        if (record.next) record.next->previous = record.previous;
        record.previous = record.next = nullptr;
    }
};

/// Returns the process-wide timer state.
auto state() -> timer_state& {
    static timer_state result;
    return result;
}

/// Invokes a due timer, or re-arms it when a bounded timeout expires early.
void fire(void* context) noexcept;

/// Arms one bounded timeout on the main runtime thread.
void arm(timer_record& record) noexcept {
    constexpr std::int64_t maximum_ms = 2147483647;
    const auto remaining = record.remaining();
    const auto rounded_ms = remaining / 1000000 + (remaining % 1000000 != 0);
    record.timeout_id = emscripten_set_timeout(
        &fire, static_cast<double>(std::min(rounded_ms, maximum_ms)), &record);
    record.armed = true;
}

/// Invokes a due timer, or re-arms it when a bounded timeout expires early.
void fire(void* context) noexcept {
    auto& record = *static_cast<timer_record*>(context);
    task<void() noexcept> target;
    {
        auto& service = state();
        std::scoped_lock lock{service.mutex};
        assert(!service.closed && record.armed);
        if (record.remaining() != 0) {
            arm(record);
            return;
        }
        service.remove(record);
        record.armed = false;
        target = std::move(record.target);
        record.release();
    }
    target();
    target = nullptr;
    record.release();
}

/// Registers an accepted timer, releasing its proxy ownership if shutdown canceled it.
void register_timer(void* context) noexcept {
    auto& record = *static_cast<timer_record*>(context);
    auto& service = state();
    std::scoped_lock lock{service.mutex};
    if (service.closed) {
        record.release();
        return;
    }
    arm(record);
}

#if defined(__EMSCRIPTEN_PTHREADS__)
/// Bounces registration onto the event loop rather than a proxy cancellation point.
void bounce_registration(void* context) noexcept {
    emscripten_async_call(&register_timer, context, 0);
}
#endif

/// Closes admission and cancels armed timeouts on their registration thread.
void close_on_main() noexcept {
    assert(emscripten_is_main_runtime_thread());
    auto& service = state();
    std::scoped_lock lock{service.mutex};
    service.closed = true;
    while (service.head) {
        auto& record = *service.head;
        service.remove(record);
        record.target = nullptr;
        if (record.armed) {
            emscripten_clear_timeout(record.timeout_id);
            record.armed = false;
            record.release();
        }
        record.release();
    }
}

#if defined(__EMSCRIPTEN_PTHREADS__)
/// Allows a pthread shutdown caller to wait for event-loop cancellation.
struct shutdown_request {
    std::mutex mutex;
    std::condition_variable condition;
    bool complete = false;
};

/// Cancels timers and acknowledges the waiting shutdown caller.
void close_and_signal(void* context) noexcept {
    close_on_main();
    auto& request = *static_cast<shutdown_request*>(context);
    std::scoped_lock lock{request.mutex};
    request.complete = true;
    request.condition.notify_one();
}

/// Defers cancellation until running timer callbacks have returned to the event loop.
void bounce_shutdown(void* context) noexcept {
    emscripten_async_call(&close_and_signal, context, 0);
}
#endif

/// Cancels accepted timers before process teardown.
void close_timers() noexcept {
#if defined(__EMSCRIPTEN_PTHREADS__)
    if (!emscripten_is_main_runtime_thread()) {
        shutdown_request request;
        emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_VI, &bounce_shutdown, &request);
        std::unique_lock<std::mutex> lock{request.mutex};
        request.condition.wait(lock, [&] { return request.complete; });
        return;
    }
#endif
    close_on_main();
}

} // namespace

/// Cancels accepted timers when the shared core shutdown handler runs.
void shutdown_system_timer() noexcept { close_timers(); }

} // namespace detail
STLAB_VERSION_NAMESPACE_END()

inline namespace v2 {

/// Relocates an accepted timer target into the core, reporting resource failures explicitly.
extern "C" auto stlab_v2_system_timer_submit(const unsigned char* task_abi_guard,
                                             const stlab_v2_task_concept* vtable,
                                             stlab_v2_task_proc invoke,
                                             void* source,
                                             std::int64_t delay_ns) noexcept
    -> stlab_v2_timer_status {
    using namespace STLAB_VERSION_NAMESPACE()::detail;
    check_timer_submission(task_abi_guard, delay_ns);
    assert(vtable && invoke && source);
    try {
        register_core_shutdown();
        auto record = std::make_unique<timer_record>(delay_ns);
        auto& service = state();
        {
            std::scoped_lock lock{service.mutex};
            assert(!service.closed && "Scheduling a timer after pre_exit().");
            if (service.closed) std::terminate();
            record->target = task<void() noexcept>{vtable, invoke, source};
            service.insert(*record);
        }
        auto* context = record.release();
#if defined(__EMSCRIPTEN_PTHREADS__)
        if (!emscripten_is_main_runtime_thread()) {
            emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_VI, &bounce_registration,
                                                        context);
        } else
#endif
        {
            register_timer(context);
        }
        return {0, 0};
    } catch (const std::bad_alloc&) {
        return {1, 0};
    }
}

} // namespace v2
} // namespace stlab
