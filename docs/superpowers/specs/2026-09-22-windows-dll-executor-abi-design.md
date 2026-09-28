# Windows DLL-Safe Executor ABI Design

- Status: Approved for implementation planning
- Date: 2026-09-22
- Related follow-on: [stlab/stlab#601](https://github.com/stlab/stlab/issues/601) — redesign `system_timer` pre-exit cancellation to destroy queued tasks

## Background

STLab currently ships all process-shared scheduling state (default/high/low executors,
`pre_exit`, `system_timer`) as static objects inside header templates plus two `.cpp` files
(`pre_exit.cpp`, `concurrency/default_executor.cpp`). This works when a single binary links the
library once, but breaks down when multiple Windows DLLs in the same process each need to share
one task pool: Windows DLLs are "insulated" — each DLL gets its own copy of template
instantiations and static locals unless state is explicitly shared through an exported, ABI-stable
boundary.

This design adds a small, versioned, `extern "C"` ABI for the executors and reshapes the portable
task system's internals so a single sharded task queue can back every platform task system
(portable, Windows thread pool, libdispatch) without adding per-task heap allocation — in fact,
removing the per-task `new`/`delete` that Windows and libdispatch backends currently perform.

`system_timer` is explicitly **out of scope** for this design. While verifying current behavior we
confirmed (via an instrumented probe against the Windows portable-task-system build) that a
`system_timer` task scheduled for a distant future time is **not destructed** by `pre_exit()`, nor
by the current static `system_timer` teardown path if the timer never fires — the queued callable
state is simply leaked until process exit. Fixing this requires a more significant redesign of
`system_timer`'s own lifecycle and is tracked separately in
[stlab/stlab#601](https://github.com/stlab/stlab/issues/601). This design only touches executors and
the (unchanged) `pre_exit` API.

## Goals

1. Provide a public, documented, ABI-stable C API for the process-shared executor state
   (`default_executor`, `high_executor`, `low_executor`), suitable for exporting from — and
   importing into — a Windows DLL via a `.def` file.
2. Keep `stlab_pre_exit` / `stlab_at_pre_exit` exactly as they are today (already `extern "C"`,
   already ABI-stable); fold them into the same restrictive export surface rather than modifying
   their signatures or semantics.
3. Support the same C ABI across all task systems (portable, Windows thread pool, libdispatch),
   so an application that provides shared executor services to plugins is not limited to one
   backend.
4. Do not regress — and where possible improve — executor hot-path performance: no new heap
   allocation is introduced by the ABI boundary, and the refactor is expected to *remove* the
   existing per-task `new`/`delete` in the Windows and libdispatch backends.
5. Do not force existing consumers into shared linking. Fully static builds (today's default)
   must remain available and behaviorally unchanged.
6. Keep the DLL-export surface minimal: only the documented C ABI symbols are visible from a
   shared build; no other library symbols are exported.

## Non-goals

- Redesigning `system_timer` lifecycle/cancellation (tracked in #601).
- Per-element task cancellation. This is handled externally via cancellation handles from another
  library; it is not part of this ABI.
- An extensible/discoverable C API (e.g., a returned function table, capability negotiation, or
  querying for optional functionality). Callers link directly against the exact major-versioned
  symbols they require. If semantics change incompatibly, the old symbol is renamed/removed and
  callers must rebuild — this is a deliberate simplicity trade-off to avoid combinatorial
  configuration/version testing.
- Non-Windows shared-library export restriction. ELF/Mach-O shared builds keep their current
  default visibility; the `.def`-based restriction described here is Windows/MSVC-specific.
  Equivalent per-platform C ABIs on non-Windows systems are welcome later if they can be added at
  zero overhead, but are not required by this design.

## Current behavior (verified)

- `pre_exit.hpp` / `pre_exit.cpp` already model the intended shape: an anonymous-namespace
  `pre_exit_stack_t` holds the process-shared handler stack; two `extern "C"` free functions
  (`stlab_pre_exit`, `stlab_at_pre_exit`) are the only exported surface; C++ inline wrappers
  (`stlab::pre_exit()`, `stlab::at_pre_exit()`) call through them. This is the model the new
  executor ABI follows.
- `default_executor.hpp` / `default_executor.cpp` currently implement three backends selected at
  compile time via `STLAB_TASK_SYSTEM`:
  - **libdispatch:** `dispatch_group_async_f` with a `new f_t(...)` per submitted callable, freed
    in the dispatch callback.
  - **Windows:** a `task_system<P>` wrapping `CreateThreadpoolWork`, with `std::make_unique<F>`
    per submitted callable, released into the callback and destroyed there.
  - **Portable:** `priority_task_system` — a fixed pool of worker threads, each with a private
    `notification_queue` (sharded by thread index), plus overflow "waiter" threads. Submission
    scans shards starting from an incrementing index via `try_push`, falling back to a blocking
    `push` on the last shard if all `try_push` calls are contended. Workers scan shards via
    `try_pop` starting at their own index before blocking on their own queue's `pop()`. No
    heap allocation beyond the queue element itself (`task<void() noexcept>`, which has its own
    small-buffer optimization).
- We confirmed by direct probing (temporary, non-committed test program linked against the
  `debug-cpp20` Windows build) that:
  - `stlab::system_timer(std::chrono::hours(1), callable)` followed immediately by
    `stlab::pre_exit()` does **not** destruct `callable`'s captured state.
  - The callable also remains undestructed through the current static `system_timer` object's own
    teardown, if the timer thread never fires it.
  - This is a pre-existing leak, not introduced by this design; it motivates #601 but does not
    block this executor-focused work.

## Architecture

### 1. Unified sharded task queue with platform-specific wake

Today only the portable backend has a shared, sharded, allocation-light queue; Windows and
libdispatch instead hand ownership of a heap-allocated closure directly to the platform API. This
design generalizes the portable queue into the **single task-storage implementation used by every
backend**, so the C ABI only ever needs one, uniform task representation.

- Three queue-set instances are retained, one per priority (`high`, `medium`/`default`, `low`),
  matching current `executor_priority` semantics.
- Each queue-set keeps the existing sharded/`try_pop`/`try_push` structure from
  `priority_task_system`'s `notification_queue`, refactored out from the thread-owning code so it
  can be shared by all backends.
- **Submitting a task always does two things:**
  1. Push the task into shard `i = index++ % shard_count` of the matching priority's queue-set
     (same algorithm as today's portable `execute<P>()`).
  2. Schedule exactly one "wake" against the platform's own pool for that priority.
- **Portable backend:** waking means notifying an already-running pool thread/condvar, unchanged
  from today's behavior.
- **Windows backend:** waking means submitting one lightweight `CreateThreadpoolWork` item per
  task, at the priority-mapped `TP_CALLBACK_PRIORITY_*`. The work item's context is just the shard
  index hint (passed by value, e.g. cast through `void*`/`ULONG_PTR`) — **no heap allocation**,
  because the task's actual closure lives in the shared queue's own storage, not in a
  per-submission heap block.
- **libdispatch backend:** waking means one `dispatch_group_async_f`/`dispatch_async_f` call per
  task at the priority-mapped QoS/queue, with the same shard-index-only context — no heap
  allocation of the closure itself.
- Windows and libdispatch **do not merge their three priority pools/queues into one.** Priority is
  still communicated to the platform via its own priority-scheduling primitive (Windows
  `TP_CALLBACK_PRIORITY_*`, libdispatch QoS), because that signal affects the platform's scheduling
  of *other* work in the process too, not just STLab's own tasks. Only the task-storage queue
  itself is unified; the platform-facing priority mechanism is untouched.

### 2. Wake / reschedule invariant

**Invariant:** for every task pushed into a shard, there is exactly one outstanding wake scheduled
against the platform pool for it. A wake's job is to consume exactly one task.

- On wake, the worker scans shards starting at the hint index using `try_pop`. If it finds a task,
  it executes it; that wake's obligation is discharged.
- If the scan finds nothing, this does **not** mean the covered task disappeared — `try_pop`
  failing can simply mean the shard is contended, or another wake already raced ahead and consumed
  the task this wake was "for," leaving some other still-pending task effectively uncovered by any
  outstanding wake. Since dropping here would violate the invariant, the worker must **reschedule
  another wake attempt** (submit a new lightweight platform work item / dispatch call) before
  returning, rather than treating an empty scan as "nothing to do."
- This keeps the total number of wake attempts bounded by (pushes + contention retries): every
  successful pop permanently discharges one wake, and reschedules only occur while genuinely
  racing.
- Platform pool threads must never block waiting on the shared queue on the Windows or libdispatch
  backends — only the portable backend's own dedicated worker threads may block, since blocking a
  platform-owned pool thread risks that platform's own overcommit/starvation heuristics.

### 3. Public C ABI

Declared with C++ conventions (the header is meant to be included by C++ code; `extern "C"` is
used only for linkage, not to force a C-style surface):

```cpp
namespace stlab {
inline namespace v2 {

using stlab_v2_task_proc = void (*)(void*) noexcept;

/// Submits one task to the shared default-priority sharded queue and schedules exactly one wake
/// against the platform pool for it. `task` is invoked with `context` exactly once. The caller
/// guarantees `context` remains valid until `task` is invoked.
extern "C" void stlab_v2_default_executor_submit(stlab_v2_task_proc task, void* context);
/// As `stlab_v2_default_executor_submit`, using the high-priority queue/pool mapping.
extern "C" void stlab_v2_high_executor_submit(stlab_v2_task_proc task, void* context);
/// As `stlab_v2_default_executor_submit`, using the low-priority queue/pool mapping.
extern "C" void stlab_v2_low_executor_submit(stlab_v2_task_proc task, void* context);

} // namespace v2
} // namespace stlab
```

- **Caller-owned, allocation-free contract:** `task`/`context` are a plain function pointer plus
  context pointer; the C ABI performs no allocation and takes no ownership of `context`. The
  existing `executor_type::operator()` C++ wrapper is responsible for placing the actual callable
  into the shared queue's own task storage (which already owns/destructs it, exactly as today's
  portable queue does) before calling submit — so submission-time allocation is a wrapper-layer
  concern, not an ABI concern, and for futures/packages the task storage can often reuse existing
  shared state rather than allocating fresh.
- **`stlab_pre_exit()` / `stlab_at_pre_exit()` are unchanged** — same signatures, same semantics,
  same file. They are not renamed and not reworked as part of this design.
- **Versioning policy:** symbol names are major-versioned (`stlab_v2_...`). All clients linking
  against a given build must use the same API major version — this is enforced structurally: if a
  function's semantics change incompatibly, it is renamed (and the old symbol deleted) rather than
  preserved or shimmed automatically. A shim to an old name is possible as a deliberate compatibility
  shim if desired later, but removal of a symbol is always an intentional breaking change requiring
  all clients to rebuild. There is no discovery/negotiation mechanism (no function table, no
  capability query) — a component links against exactly the symbols it needs.

### 4. Export surface (`.def`) and visibility

- `WINDOWS_EXPORT_ALL_SYMBOLS` is **removed** from `src/CMakeLists.txt`. It is not used anywhere in
  this design.
- A new `src/stlab.def` lists exactly the symbols that make up the public C ABI:

  ```
  EXPORTS
      stlab_pre_exit
      stlab_at_pre_exit
      stlab_v2_default_executor_submit
      stlab_v2_high_executor_submit
      stlab_v2_low_executor_submit
  ```

- The `.def` is applied only when building `stlab-core` (see below) as a shared library on
  Windows/MSVC. No other symbol in the library is exported from that DLL.

### 5. `stlab-core` target split (static by default, optional shared)

Today all process-shared-state code lives in the same two `.cpp` files that will eventually sit
alongside other, unrelated `.cpp` files as the library grows. To let a consumer statically link
everything except the process-shared-state code — while still supporting the "must use a Windows
DLL for shared state" case — the ABI-relevant code is factored into its own CMake target from the
start, rather than retrofitted later:

- **`stlab-core`**: a new target containing only process-shared-state `extern "C"` implementation
  — `pre_exit.cpp`, the new sharded-queue/executor-ABI source (e.g.
  `concurrency/executor_abi.cpp`), and any future process-shared-state source. This is the only
  target that ever needs the `.def`/export restriction.
- **`stlab`** (existing main target): everything else — the header-only public API surface and any
  future non-shared-state `.cpp` files. It links against `stlab-core`'s exported C symbols and is
  otherwise unaffected by how `stlab-core` is built.
- **New option `STLAB_CORE_SHARED` (default `OFF`)** controls only `stlab-core`'s link type. It is
  deliberately **not** tied to the existing global `BUILD_SHARED_LIBS`, because forcing that choice
  onto every target in a consumer's build graph would violate the "don't force clients into shared
  linking" goal.
  - `STLAB_CORE_SHARED=OFF` (default): `stlab-core` is a plain static library. The `.def` file and
    export restriction are irrelevant and skipped. Existing consumers building everything statically
    see no behavior change.
  - `STLAB_CORE_SHARED=ON` (Windows opt-in): `stlab-core` builds as a DLL with `stlab.def` applied,
    exporting only the five symbols listed above. This is the mode for applications that must share
    one task pool / pre-exit stack across multiple Windows DLLs in the same process.
- Install/export support (`cpp-library`'s `_cpp_library_setup_install`) is updated to include
  `stlab-core` alongside `stlab` in the installed target set, in both link modes.

## Testing

- `stlab.test.executor` gains cases for:
  - Correctness: every submitted task executes exactly once across all shards/priorities.
  - Priority ordering: platform priority mapping is preserved per backend.
  - The wake/reschedule invariant under contention: a stress test with many concurrent submitters
    and a small shard count verifies no task is ever dropped (all submitted tasks are eventually
    executed) even when `try_pop` scans race.
- A new Windows-only smoke test, built only when `STLAB_CORE_SHARED=ON`, links solely against the
  `.def`-exported import library and confirms: (a) `stlab_v2_default_executor_submit` executes a
  task, and (b) `stlab_pre_exit()` still drains registered handlers — proving the "only these five
  symbols are visible" contract holds and that a minimal consumer can build against just the C ABI.
- No changes are needed to `stlab.test.system_timer` for this design; timer behavior changes are
  entirely deferred to #601.

## Open follow-on work (not in this design)

- [#601](https://github.com/stlab/stlab/issues/601): redesign `system_timer` so tasks scheduled
  before or after `pre_exit()` are destructed without execution, instead of leaking queued
  callable state.
- Optional future work: equivalent C ABI symbols for non-Windows shared builds, if they can be
  added without overhead to the portable/libdispatch paths.
