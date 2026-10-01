/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/system_timer.hpp>
#include <stlab/concurrency/task.hpp>
#include <stlab/config.hpp>

#include "detail/core_shutdown.hpp"
#include "detail/system_timer_shutdown.hpp"
#include "detail/timer_common.hpp"

#include <dispatch/dispatch.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

namespace stlab {
inline namespace v2 {
namespace {

/// Core-owned cancelable libdispatch sources.
class dispatch_timers {
    /// Pending source and its directly relocated task.
    struct record {
        dispatch_timers& owner;
        detail::timer_delay delay;
        task<void() noexcept> target;
        dispatch_source_t source = nullptr;
        record* next = nullptr;
        record* previous = nullptr;

        /// Prepares bookkeeping before source creation and task relocation.
        record(dispatch_timers& service, std::int64_t delay_ns) noexcept :
            owner(service), delay(delay_ns) {}
    };

    std::mutex _mutex;
    std::condition_variable _finished;
    record* _pending = nullptr;
    bool _closed = false;

    /// Arms a bounded monotonic wait; the event handler rechecks the remaining delay.
    static void arm(record& entry, std::int64_t remaining) noexcept {
        constexpr std::int64_t chunk =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::hours(1)).count();
        dispatch_source_set_timer(entry.source,
                                  dispatch_time(DISPATCH_TIME_NOW, std::min(remaining, chunk)),
                                  DISPATCH_TIME_FOREVER, 0);
    }

    /// Makes execution or cancellation one synchronized ownership transition.
    static void event(void* context) noexcept {
        auto& entry = *static_cast<record*>(context);
        task<void() noexcept> target;
        {
            std::scoped_lock lock(entry.owner._mutex);
            if (entry.owner._closed) return;
            const auto remaining = entry.delay.remaining();
            if (remaining != 0) {
                arm(entry, remaining);
                return;
            }
            target = std::move(entry.target);
        }
        target();
        target = nullptr;
        dispatch_source_cancel(entry.source);
    }

    /// Releases the target and source after all event handlers for this source have returned.
    static void canceled(void* context) noexcept {
        auto& entry = *static_cast<record*>(context);
        auto& owner = entry.owner;
        entry.target = nullptr;
        std::scoped_lock lock(owner._mutex);
        if (entry.previous)
            entry.previous->next = entry.next;
        else
            owner._pending = entry.next;
        if (entry.next) entry.next->previous = entry.previous;
        dispatch_release(entry.source);
        delete &entry;
        owner._finished.notify_one();
    }

public:
    /// Prepares record and source before consuming the client's task.
    void submit(const stlab_v2_task_concept* vtable,
                stlab_v2_task_proc invoke,
                void* source,
                std::int64_t delay_ns) {
        std::scoped_lock lock(_mutex);
        detail::check_timer_open(_closed);
        auto entry = std::make_unique<record>(*this, delay_ns);
        entry->source =
            dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                   dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));
        if (!entry->source) throw std::bad_alloc();
        dispatch_set_context(entry->source, entry.get());
        dispatch_source_set_event_handler_f(entry->source, event);
        dispatch_source_set_cancel_handler_f(entry->source, canceled);
        entry->target = task<void() noexcept>(vtable, invoke, source);
        entry->next = _pending;
        if (_pending) _pending->previous = entry.get();
        _pending = entry.get();
        arm(*entry, entry->delay.remaining());
        dispatch_resume(entry->source);
        (void)entry.release();
    }

    /// Cancels pending sources and waits for all committed callbacks and capture destruction.
    ///
    /// - Precondition: not called from a timer callback.
    /// - Complexity: linear in the number of pending timers.
    void close() noexcept {
        std::unique_lock<std::mutex> lock(_mutex);
        _closed = true;
        for (auto* entry = _pending; entry; entry = entry->next)
            dispatch_source_cancel(entry->source);
        _finished.wait(lock, [&] { return _pending == nullptr; });
    }
};

/// Returns the service, whose closed state exists before initial submission.
auto timers() -> dispatch_timers& {
    static dispatch_timers service;
    return service;
}

/// Makes the closed state available eagerly, without ordering cleanup through the handler stack.
[[maybe_unused]] const bool initialized = [] {
    (void)timers();
    return true;
}();

} // namespace

extern "C" stlab_v2_timer_status stlab_v2_system_timer_submit(const unsigned char* guard,
                                                              const stlab_v2_task_concept* vtable,
                                                              stlab_v2_task_proc invoke,
                                                              void* source,
                                                              std::int64_t delay_ns) noexcept {
    detail::check_timer_submission(guard, delay_ns);
    try {
        detail::register_core_shutdown();
        timers().submit(vtable, invoke, source, delay_ns);
        return {0, 0};
    } catch (const std::bad_alloc&) {
        return {1, 0};
    } catch (const std::system_error& error) {
        return detail::timer_resource_error(error);
    }
}

} // namespace v2

STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {

void shutdown_system_timer() noexcept { timers().close(); }

} // namespace detail
STLAB_VERSION_NAMESPACE_END()

} // namespace stlab
