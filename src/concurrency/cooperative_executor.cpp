/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include "detail/cooperative_executor.hpp"
#include "detail/core_shutdown.hpp"

#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/task.hpp>
#include <stlab/config.hpp>

#include <emscripten/eventloop.h>
#include <emscripten/threading.h>

#include <cassert>
#include <cstdint>
#include <deque>
#include <exception>
#include <utility>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {
namespace {

/// Serializes cooperative tasks and retires executor work before releasing the main exit fence.
class cooperative_dispatcher {
    using task_t = task<void() noexcept>;

    /// Tracks executor admission and dispatch eligibility.
    enum class phase : std::uint8_t { running, draining, closed };

    std::deque<task_t> _executor;
    std::deque<task_t> _main;
    std::deque<task_t> _deferred_main;
    std::deque<cooperative_task_kind> _order;
    phase _phase = phase::running;
    bool _wake_pending = false;
    bool _dispatching = false;
    bool _drain_origin = false;

    /// Services one task or completes an empty executor drain.
    static void dispatch(void* context) noexcept {
        static_cast<cooperative_dispatcher*>(context)->run_one();
    }

    /// Posts exactly one event-loop wake when eligible work or drain completion remains.
    void request_wake() noexcept {
        if (_wake_pending || _dispatching) return;
        if (_phase == phase::running && _order.empty()) return;
        if (_phase == phase::closed && _main.empty() && _deferred_main.empty()) return;
        _wake_pending = true;
        (void)emscripten_set_immediate(&dispatch, this);
    }

    /// Removes the oldest task from a nonempty submission stream.
    static auto take(std::deque<task_t>& queue) -> task_t {
        assert(!queue.empty());
        auto result = std::move(queue.front());
        queue.pop_front();
        return result;
    }

    /// Invokes and destroys one target before deciding whether executor retirement is complete.
    void run_one() noexcept {
        assert(emscripten_is_main_runtime_thread() && _wake_pending && !_dispatching);
        _wake_pending = false;
        _dispatching = true;
        task_t target;
        bool executor = false;
        if (_phase == phase::running) {
            assert(!_order.empty());
            executor = _order.front() == cooperative_task_kind::executor;
            _order.pop_front();
            target = take(executor ? _executor : _main);
        } else if (_phase == phase::draining) {
            if (!_executor.empty()) {
                executor = true;
                target = take(_executor);
            }
        } else {
            target = take(_main.empty() ? _deferred_main : _main);
        }
        _drain_origin = executor && _phase == phase::draining;
        if (target) target();
        // A task initiating shutdown stages its exit fence; its capture cleanup remains a producer.
        _drain_origin = executor && _phase == phase::draining;
        target = nullptr;
        _drain_origin = false;
        if (_phase == phase::draining && _executor.empty()) {
            _phase = phase::closed;
            complete_core_shutdown();
            _drain_origin = true;
            resume_pre_exit();
            _drain_origin = false;
        }
        _dispatching = false;
        request_wake();
    }

public:
    /// Appends one task without allocating a wrapper or relocating already queued targets.
    void submit(const stlab_v2_task_concept* vtable,
                stlab_v2_task_proc invoke,
                void* source,
                cooperative_task_kind kind) {
        assert(emscripten_is_main_runtime_thread());
        if (kind == cooperative_task_kind::executor && _phase == phase::closed) {
            assert(false && "cooperative executor submission after shutdown");
            std::terminate();
        }
        if (_phase == phase::running) _order.push_back(kind);
        auto& queue = kind == cooperative_task_kind::executor ?
                          _executor :
                          (_phase == phase::running || _drain_origin ? _main : _deferred_main);
        queue.emplace_back(vtable, invoke, source);
        request_wake();
    }

    /// Suspends ordinary main dispatch and allows executor descendants until the drain completes.
    /// - Complexity: linear in queued routing tokens; no task relocation.
    void retire() noexcept {
        assert(emscripten_is_main_runtime_thread() && _phase == phase::running);
        defer_pre_exit();
        _phase = phase::draining;
        _order.clear();
        _drain_origin = false;
        request_wake();
    }
};

/// Returns the retained dispatcher so main admission survives core shutdown.
auto dispatcher() -> cooperative_dispatcher& {
    static auto& result = *new cooperative_dispatcher; // NOLINT(cppcoreguidelines-owning-memory)
    return result;
}

} // namespace

void submit_cooperative_task(const stlab_v2_task_concept* vtable,
                             stlab_v2_task_proc invoke,
                             void* source,
                             cooperative_task_kind kind) {
    dispatcher().submit(vtable, invoke, source, kind);
}

void shutdown_cooperative_executor() noexcept { dispatcher().retire(); }

} // namespace detail
STLAB_VERSION_NAMESPACE_END()
} // namespace stlab
