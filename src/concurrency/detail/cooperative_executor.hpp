/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#ifndef STLAB_SRC_CONCURRENCY_DETAIL_COOPERATIVE_EXECUTOR_HPP
#define STLAB_SRC_CONCURRENCY_DETAIL_COOPERATIVE_EXECUTOR_HPP

#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/task.hpp>
#include <stlab/config.hpp>

#include <cstdint>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {

/// Identifies the submission stream sharing the cooperative event-loop dispatcher.
enum class cooperative_task_kind : std::uint8_t { main, executor };

/// Relocates one task into its stream, preserving merged FIFO until executor retirement.
/// - Precondition: called on the main runtime thread; relocation describes a live source task.
/// - Precondition: executor admission has not closed for executor submissions.
void submit_cooperative_task(const stlab_v2_task_concept* vtable,
                             stlab_v2_task_proc invoke,
                             void* source,
                             cooperative_task_kind kind);

/// Defers the remaining pre-exit handlers while event-loop callbacks drain executor work.
/// - Precondition: called once by the active shared core pre-exit handler.
/// - Complexity: linear in queued routing tokens; no task relocation.
void shutdown_cooperative_executor() noexcept;

} // namespace detail
STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

#endif
