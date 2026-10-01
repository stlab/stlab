/*
    Copyright 2025 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/
/**************************************************************************************************/

#include <stlab/config.hpp>
#include <stlab/pre_exit.hpp>

#include "concurrency/detail/core_shutdown.hpp"

#include <cassert>
#include <cstdint>
#include <exception>
#include <mutex>
#include <vector>

namespace stlab {
inline namespace v2 {

namespace {

/// Holds the LIFO handlers for one public operation and any deferred cooperative continuation.
struct pre_exit_stack_t {
    using lock_t = std::unique_lock<std::mutex>;

    /// Tracks the single pre-exit operation, including asynchronous cooperative retirement.
    enum class phase : std::uint8_t { idle, running, deferred, closed };

    std::mutex _mutex;
    // The size constructor can propagate debug-iterator allocation failure; vector() is noexcept.
    std::vector<pre_exit_handler> _stack = std::vector<pre_exit_handler>(0);
    phase _phase = phase::idle;

    /// Starts the public operation or resumes its deferred continuation exactly once.
    void start(bool resume = false) {
        lock_t lock{_mutex};
        if (_phase != (resume ? phase::deferred : phase::idle)) {
            assert(false && "pre_exit invoked more than once or invalid resume");
            std::terminate();
        }
        _phase = phase::running;
    }

    /// Defers the remaining handlers until cooperative work has finished.
    void defer() {
        lock_t lock{_mutex};
        assert(_phase == phase::running);
        _phase = phase::deferred;
    }

    /// Returns whether the active handler deferred the operation.
    auto deferred() -> bool {
        lock_t lock{_mutex};
        return _phase == phase::deferred;
    }

    /// Push an exit handler. Precondition that stack is not closed.
    void push(pre_exit_handler f) {
        lock_t lock{_mutex};
        if (_phase == phase::closed) {
            assert(false && "Adding a pre-exit handler after pre_exit() completed.");
            std::terminate();
        }
        _stack.push_back(f);
    }

    /// Pop one exit handler, returns `nullptr` and closes stack if empty.
    auto pop() -> pre_exit_handler {
        lock_t lock{_mutex};
        if (_stack.empty()) {
            assert(_phase == phase::running);
            _phase = phase::closed;
            return nullptr;
        }
        auto result = _stack.back();
        _stack.pop_back();
        return result;
    }

    /// Requires the operation to finish before global teardown.
    ~pre_exit_stack_t() {
        assert(_phase == phase::closed && "WARNING: `pre_exit()` not called before program exit.");
    }
};

/// Returns the process-wide pre-exit stack.
auto pre_exit_stack() -> auto& {
    static pre_exit_stack_t _q;
    return _q;
}

/// Pops handlers until completion or an asynchronous core handler defers the remaining stack.
/// - Complexity: linear in popped handlers, excluding their work.
void run_pre_exit_handlers() {
    auto& stack = pre_exit_stack();
    while (auto f = stack.pop()) {
        f();
        if (stack.deferred()) return;
    }
    detail::complete_core_shutdown();
}

} // namespace

extern "C" void stlab_pre_exit() {
    pre_exit_stack().start();
    run_pre_exit_handlers();
}

extern "C" void stlab_at_pre_exit(pre_exit_handler f) { pre_exit_stack().push(f); }

} // namespace v2

STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {

void register_core_shutdown_handler(core_executor_cleanup cleanup) {
    v2::pre_exit_stack().push(cleanup);
}

void defer_pre_exit() noexcept { v2::pre_exit_stack().defer(); }

void resume_pre_exit() noexcept {
    v2::pre_exit_stack().start(true);
    v2::run_pre_exit_handlers();
}

} // namespace detail
STLAB_VERSION_NAMESPACE_END()

} // namespace stlab
