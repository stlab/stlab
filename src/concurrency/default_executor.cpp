/*
    Copyright 2025 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <stlab/concurrency/default_executor.hpp>

#if STLAB_TASK_SYSTEM(LIBDISPATCH)
#include <dispatch/dispatch.h>
#endif

#include "stlab/config.hpp"   // for STLAB_TASK_SYSTEM, STLAB_TASK_SYSTEM_L...
#include "stlab/pre_exit.hpp" // for at_pre_exit

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {

#if STLAB_TASK_SYSTEM(LIBDISPATCH)
auto group() -> const group_t& {
    // Use an immediately executed lambda to atomically register pre-exit handler
    // and create the dispatch group.
    static group_t g{[] {
        at_pre_exit([]() noexcept { // <br>
            dispatch_group_wait(g._group, DISPATCH_TIME_FOREVER);
        });
        return group_t{};
    }()};
    return g;
}

group_t::~group_t() {
    if (_group) {
        dispatch_group_wait(_group, DISPATCH_TIME_FOREVER);
#if !STLAB_FEATURE(OBJC_ARC)
        dispatch_release(_group);
#endif
    }
}

#elif STLAB_TASK_SYSTEM(PORTABLE)
priority_task_system& pts() {
    // Uses the `nullptr` constructor with an immediate executed lambda to register the task
    // system in a thread safe manner.
    static priority_task_system only_task_system{[] {
        at_pre_exit([]() noexcept { only_task_system.join(); });
        return nullptr;
    }()};
    return only_task_system;
}

#elif STLAB_TASK_SYSTEM(WINDOWS)

/// Returns the singleton instance of the task system for the given priority.
template <executor_priority P>
task_system<P>& single_task_system() {
    static task_system<P> _task_system{[] {
        at_pre_exit([]() noexcept { single_task_system<P>().join(); });
        return task_system<P>{};
    }()};
    return _task_system;
}

/// Instantiations of single_task_system for each priority level.
template task_system<executor_priority::high>& single_task_system<executor_priority::high>();
template task_system<executor_priority::medium>& single_task_system<executor_priority::medium>();
template task_system<executor_priority::low>& single_task_system<executor_priority::low>();

#endif

} // namespace detail
STLAB_VERSION_NAMESPACE_END()
} // namespace stlab
