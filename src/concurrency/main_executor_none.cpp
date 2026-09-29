/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

// With STLAB_MAIN_EXECUTOR=none the public header does not declare the main executor ABI; these
// definitions exist only so the unconditional Windows `.def` export list resolves.

#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/task.hpp>

#include <cassert>
#include <cstdlib>

namespace stlab {
inline namespace v2 {

/// Stub: no main executor is configured.
///
/// - Precondition: never called.
extern "C" void stlab_v2_main_executor_submit(const unsigned char* /*task_abi_guard*/,
                                              const stlab_v2_task_concept* /*vtable*/,
                                              stlab_v2_task_proc /*invoke*/,
                                              void* /*source*/) noexcept {
    assert(false && "No main executor is configured (STLAB_MAIN_EXECUTOR=none).");
    std::abort();
}

/// Stub: no main executor is configured.
///
/// - Precondition: never called.
extern "C" [[noreturn]] void stlab_v2_main_executor_run() {
    assert(false && "No main executor is configured (STLAB_MAIN_EXECUTOR=none).");
    std::abort();
}

} // namespace v2
} // namespace stlab
