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

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <new>
#include <thread>
#include <utility>
#include <vector>

namespace stlab {
inline namespace v2 {
namespace {

/// Core-owned timer heap and lazily started worker.
class portable_timers {
    /// Pending task with its monotonic remaining delay and next wake.
    struct record {
        detail::timer_delay delay;
        std::chrono::steady_clock::time_point wake;
        task<void() noexcept> target;

        /// Prepares a pending record without consuming a relocation source.
        explicit record(std::int64_t delay_ns) noexcept :
            delay(delay_ns), wake(delay.next_wake()) {}
    };

    /// Orders the timer heap by its next wake.
    struct later {
        /// Returns whether `a` should wake after `b`.
        auto operator()(const record& a, const record& b) const noexcept -> bool {
            return a.wake > b.wake;
        }
    };

    std::mutex _mutex;
    std::condition_variable _ready;
    std::vector<record> _pending;
    std::thread _worker;
    bool _closed = false;

    /// Executes committed tasks outside the admission lock.
    void run() noexcept {
        std::unique_lock<std::mutex> lock(_mutex);
        for (;;) {
            if (_closed) return;
            if (_pending.empty()) {
                _ready.wait(lock);
                continue;
            }
            const auto wake = _pending.front().wake;
            if (std::chrono::steady_clock::now() < wake) {
                _ready.wait_until(lock, wake);
                continue; // An insertion may have changed the earliest deadline.
            }
            std::pop_heap(_pending.begin(), _pending.end(), later{});
            auto& next = _pending.back();
            if (next.delay.remaining() != 0) {
                next.wake = next.delay.next_wake();
                std::push_heap(_pending.begin(), _pending.end(), later{});
                continue;
            }
            auto target = std::move(next.target);
            _pending.pop_back();
            lock.unlock();
            target();
            target = nullptr; // Destruction is part of the committed callback.
            lock.lock();
        }
    }

public:
    /// Prepares worker and queue storage, then relocates the task without further throwing work.
    void submit(const stlab_v2_task_concept* vtable,
                stlab_v2_task_proc invoke,
                void* source,
                std::int64_t delay_ns) {
        std::unique_lock<std::mutex> lock(_mutex);
        detail::check_timer_open(_closed);
        if (!_worker.joinable()) _worker = std::thread([this] { run(); });
        _pending.emplace_back(delay_ns);
        _pending.back().target = task<void() noexcept>(vtable, invoke, source);
        std::push_heap(_pending.begin(), _pending.end(), later{});
        lock.unlock();
        _ready.notify_one();
    }

    /// Closes admission, releases pending captures, and joins the committed callback.
    ///
    /// - Precondition: not called by this timer worker.
    /// - Complexity: linear in the number of pending timers.
    void close() noexcept {
        std::vector<record> canceled;
        {
            std::scoped_lock lock(_mutex);
            _closed = true;
            _pending.swap(canceled);
        }
        _ready.notify_one();
        canceled.clear();
        if (_worker.joinable()) _worker.join();
    }
};

/// Returns the native service, including its closed state before any initial submission.
auto timers() -> portable_timers& {
    static portable_timers service;
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
