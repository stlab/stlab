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

#include <Windows.h> // NOLINT(misc-include-cleaner): Windows SDK umbrella header.

#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <system_error>
#include <utility>

namespace stlab {
inline namespace v2 {
namespace {

// Windows SDK declarations are provided through Windows.h, not its implementation headers.
// NOLINTBEGIN(misc-include-cleaner)
/// Core-owned Windows timers and their cleanup group.
class windows_timers {
    /// One pending or executing timer; its target uses the existing task inline storage.
    struct record {
        windows_timers& owner;
        detail::timer_delay delay;
        task<void() noexcept> target;
        PTP_TIMER timer = nullptr;
        record* next = nullptr;
        record* previous = nullptr;

        /// Prepares bookkeeping without consuming the source task.
        record(windows_timers& service, std::int64_t delay_ns) noexcept :
            owner(service), delay(delay_ns) {}
    };

    std::mutex _mutex;
    bool _closed = false;
    PTP_POOL _pool = nullptr;
    PTP_CLEANUP_GROUP _group = nullptr;
    TP_CALLBACK_ENVIRON _environment{};
    record* _pending = nullptr;

    /// Reports a Windows resource failure before ownership transfer.
    [[noreturn]] static void resource_failure(DWORD error) {
        throw std::system_error(static_cast<int>(error), std::system_category());
    }

    /// Initializes the native resources transactionally.
    void prepare() {
        if (_pool) return;
        const auto pool = CreateThreadpool(nullptr);
        if (!pool) resource_failure(GetLastError());
        const auto group = CreateThreadpoolCleanupGroup();
        if (!group) {
            const auto error = GetLastError();
            CloseThreadpool(pool);
            resource_failure(error);
        }
        InitializeThreadpoolEnvironment(&_environment);
        SetThreadpoolCallbackPool(&_environment, pool);
        SetThreadpoolCallbackCleanupGroup(&_environment, group, nullptr);
        _pool = pool;
        _group = group;
    }

    /// Arms a relative FILETIME, rounding positive nanoseconds upward to 100 ns.
    static void arm(record& entry, std::int64_t remaining) noexcept {
        const auto ticks = remaining / 100 + (remaining % 100 != 0);
        // Zero FILETIME is an absolute instant in the past and therefore queues asynchronously.
        const auto relative = static_cast<std::uint64_t>(-ticks);
        FILETIME due{static_cast<DWORD>(relative), static_cast<DWORD>(relative >> 32)};
#pragma warning(push)
#pragma warning(disable : 6553)
        SetThreadpoolTimer(entry.timer, &due, 0, 0);
#pragma warning(pop)
    }

    /// Unlinks a completed record while admission and group cleanup are excluded.
    void remove(record& entry) noexcept {
        if (entry.previous)
            entry.previous->next = entry.next;
        else
            _pending = entry.next;
        if (entry.next) entry.next->previous = entry.previous;
    }

    /// Commits execution under the same lock that closes admission.
    static void CALLBACK callback(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER) noexcept {
        auto& entry = *static_cast<record*>(context);
        auto& owner = entry.owner;
        task<void() noexcept> target;
        {
            std::scoped_lock lock(owner._mutex);
            if (owner._closed) return;
            const auto remaining = entry.delay.remaining();
            if (remaining != 0) {
                arm(entry, remaining);
                return;
            }
            target = std::move(entry.target);
        }
        target();
        target = nullptr;
        {
            std::scoped_lock lock(owner._mutex);
            if (owner._closed) return; // Group cleanup waits for us and releases this record.
            owner.remove(entry);
            CloseThreadpoolTimer(entry.timer);
            delete &entry;
        }
    }

public:
    /// Prepares all native resources before the noexcept relocation and arm.
    void submit(const stlab_v2_task_concept* vtable,
                stlab_v2_task_proc invoke,
                void* source,
                std::int64_t delay_ns) {
        std::scoped_lock lock(_mutex);
        detail::check_timer_open(_closed);
        prepare();
        auto entry = std::make_unique<record>(*this, delay_ns);
        entry->timer = CreateThreadpoolTimer(callback, entry.get(), &_environment);
        if (!entry->timer) resource_failure(GetLastError());
        entry->target = task<void() noexcept>(vtable, invoke, source);
        entry->next = _pending;
        if (_pending) _pending->previous = entry.get();
        _pending = entry.get();
        arm(*entry, entry->delay.remaining());
        (void)entry.release();
    }

    /// Closes admission, releases pending captures, then waits for committed callbacks and
    /// destruction.
    ///
    /// - Precondition: not called from a timer callback.
    /// - Complexity: linear in the number of pending timers.
    void close() noexcept {
        {
            std::scoped_lock lock(_mutex);
            _closed = true;
        }
        if (!_pool) return;
        // Closed callbacks never mutate the list or targets; committed targets are callback-local.
        for (auto* entry = _pending; entry; entry = entry->next)
            entry->target = nullptr;
        CloseThreadpoolCleanupGroupMembers(_group, TRUE, nullptr);
        while (_pending) {
            auto* entry = _pending;
            _pending = entry->next;
            delete entry;
        }
        CloseThreadpoolCleanupGroup(_group);
        DestroyThreadpoolEnvironment(&_environment);
        CloseThreadpool(_pool);
        _group = nullptr;
        _pool = nullptr;
    }
};
// NOLINTEND(misc-include-cleaner)

/// Returns the service without lazily registering shutdown.
auto timers() -> windows_timers& {
    static windows_timers service;
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
