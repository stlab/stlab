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
#include <exception>
#include <mutex>
#include <vector>

namespace stlab {
inline namespace v2 {

namespace {

struct pre_exit_stack_t {
    using lock_t = std::unique_lock<std::mutex>;

    std::mutex _mutex;
    // The size constructor can propagate debug-iterator allocation failure; vector() is noexcept.
    std::vector<pre_exit_handler> _stack = std::vector<pre_exit_handler>(0);
    bool _closed{false};

    /// Push an exit handler. Precondition that stack is not closed.
    void push(pre_exit_handler f) {
        lock_t lock{_mutex};
        if (_closed) {
            assert(false && "Adding a pre-exit handler after pre_exit() completed.");
            std::terminate();
        }
        _stack.push_back(f);
    }

    /// Pop one exit handler, returns `nullptr` and closes stack if empty.
    auto pop() -> pre_exit_handler {
        lock_t lock{_mutex};
        if (_stack.empty()) {
            assert(!_closed && "WARNING `pre_exit()` invoked more than once.");
            _closed = true;
            return nullptr;
        }
        auto result = _stack.back();
        _stack.pop_back();
        return result;
    }

    ~pre_exit_stack_t() {
        assert(_closed && "WARNING: `pre_exit()` not called before program exit.");
    }
};

auto pre_exit_stack() -> auto& {
    static pre_exit_stack_t _q;
    return _q;
}

} // namespace

extern "C" void stlab_pre_exit() {
    auto& _s = pre_exit_stack();
    while (auto f = _s.pop()) {
        f();
    };
    detail::complete_core_shutdown();
}

extern "C" void stlab_at_pre_exit(pre_exit_handler f) { pre_exit_stack().push(f); }

} // namespace v2

STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {

void register_core_shutdown_handler(core_executor_cleanup cleanup) {
    v2::pre_exit_stack().push(cleanup);
}

} // namespace detail
STLAB_VERSION_NAMESPACE_END()

} // namespace stlab
