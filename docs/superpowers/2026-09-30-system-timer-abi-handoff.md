# System timer ABI and cooperative Emscripten handoff

## Implementation

- Moved timer scheduling and state into selected `stlab-core` backends for Windows,
  portable scheduling, libdispatch, and Emscripten.
- Added `stlab_v2_system_timer_submit`, with fixed-width delay and resource-error
  status. Client wrappers translate resource failures without transporting C++
  exceptions across the ABI. Failed submission retains the source target.
- Restored supported `steady_clock::time_point` scheduling alongside checked duration
  scheduling. The original deprecation was an interface-ergonomics change in 2019
  for [#262](https://github.com/stlab/stlab/issues/262), not a prohibition on offering
  both overloads.
- Emscripten timers register, run, re-arm, and cancel through the main runtime event
  loop using `emscripten_set_timeout()`. Long delays use bounded arms with monotonic
  deadline rechecks. Pthread submissions do not depend on the submitting worker's
  event loop.
- `STLAB_EMSCRIPTEN_PTHREADS=OFF` selects `none` threads and the cooperative Emscripten
  task/main executors. All three priority executors submit asynchronously to that
  queue. Conflicting configuration and compiler threading flags are rejected.
- Added the core blocking-capability ABI. In cooperative mode, non-ready `await()`
  and `invoke_waiting()` terminate; `await_for()` polls immediately, preserving the
  supplied future and ignoring its timeout. Coroutine awaiting remains nonblocking.
- The approved shutdown design uses one lazy core handler for timers and default
  executors, registered at first use of either service. Application exit handlers
  retain LIFO ordering. The shared handler cancels pending timers, joins active timer
  callbacks, then drains initialized executors.
- The main queue remains available after `pre_exit()`. A main shutdown task can
  retire producers, then append an exit fence behind their main-queue submissions.
  This is not transitive draining of descendants enqueued by earlier main tasks.
- Added public timer, core-only lifecycle/resource-failure, executor-shutdown,
  Emscripten event-loop, polling/coroutine/termination, and configuration tests.
  Updated API contracts, README, CMake presets, and the threadless CI matrix entry.

## Review corrections

The initial correctness review reproduced two defects; fixes were implemented:

1. Timer cleanup registered eagerly through the ordinary LIFO pre-exit stack ran
   after lazily registered executor joins. A pending timer owning the final promise
   reference could therefore prevent shutdown from reaching its cancellation.
   The shared core handler cancels timers before joining executors; a standalone
   executor-shutdown regression covers this ordering.
2. `std::min` deduction assumed `int64_t` and `chrono::nanoseconds::rep` had the same
   underlying type. Explicit fixed-width chunk types restore Linux libc++
   compatibility.

Further review reproduced a deadlock when early timer joining prevented an
application `at_pre_exit()` handler from releasing an active callback. The user
approved the lazy shared-handler design instead of global teardown phases or
restricting application handlers to final cleanup. Handlers needed to release core
work must be registered after core first-use so they run before core teardown.

Lifecycle integration and its new regressions are verified. The
portable main retention tests failed against its old closing hook and passed after
removing that hook. The producer-to-main shutdown fence reproduced the old early
timer-join deadlock. Producers must not synchronously require main-queue progress
while `pre_exit()` occupies the main thread.

The final review also reproduced cross-priority Windows teardown failure: one pool
could close while another pool's accepted work still needed to submit a continuation.
Windows now retains a completion token per accepted operation and drains the entire
executor subsystem before closing any priority pool. Continuations can first-use
another priority during drain; contention retries reuse the same native work object.
No extra per-submission heap allocation or warm-path mutex was added.

The cross-priority regression originally waited for a second task before signaling
shutdown. GDB showed that this test setup could stall a portable worker and its shard
without reaching `pre_exit()`. Removing that unnecessary preliminary wait preserves
the shutdown contract and permits the test to run on portable scheduling. The final
regression passed 25 repeated runs on both Windows native and Linux libc++.

An earlier resource-failure scenario timed out once. After rebuilding the final core,
it passed the full suites, native/portable ASan selections, and 25 additional native
repetitions. Cold/warm allocation failure, unconsumed target ownership, and successful
retry remain covered; the timeout was not reproduced.

## PR review and CI corrections

The PR review reproduced an Emscripten ABI-boundary defect: submission accepted an
incompatible task-storage guard. The backend now uses the same guard and normalized
delay check as native timers, before resource preparation or relocation. Raw-ABI
death regressions cover an incompatible guard and a negative delay.

The cooperative CI configuration failure was reproduced with an explicit SDK path
and no `em-config` on `PATH`. Nested `try_compile` configurations did not inherit
that SDK selection or the pthread option. The toolchain now propagates these
variables and the selected Node executable/flags. Configuration rejection tests
remove the SDK directory from `PATH` so an inherited shell environment cannot hide
this failure.

The pthread CI startup failure was reproduced deterministically by waiting for a
main task during host startup. With `PROXY_TO_PTHREAD`, the main runtime already
services tasks before the startup pthread calls `main_executor_run()`. The host
now marks this already-active loop correctly. Concurrent-test thread handles are
published by a main task only after construction completes, preventing the final
callback from joining a vector still being modified. FIFO and exactly-once checks
remain intact.

After these corrections, Emscripten pthread tests passed 21/21, including 20
additional concurrent-startup repetitions; cooperative tests passed 11/11,
including all five configuration rejection scenarios without SDK discovery on
`PATH`. Windows portable-main tests passed 5/5. The release cooperative timer
selection also passed 4/4, including both ABI violations with assertions disabled.

The macOS native TSan failures identified cancellation reading and destroying the
timer target without synchronizing with submission's owner mutex. Cancellation
now acquires that mutex to establish publication, releases it before destroying
user captures, then relocks for unlinking and notification. The record stays
pending until capture destruction finishes. The failed CI reports are the
pre-fix evidence. All 15 CI jobs passed at `2c07abe` in
[run 36832092800](https://github.com/stlab/stlab/actions/runs/36832092800),
including native and portable macOS TSan, both WebAssembly configurations, and
all Windows shared-core variants.

The next Copilot review identified Emscripten capture destruction under the timer
admission mutex. A canceled capture that attempts resubmission reproduced the
SDK's pthread-mutex deadlock assertion rather than the closed-admission
diagnostic. Cancellation now detaches each record and clears any timeout under
the mutex, then unlocks while destroying captures and releasing the list
reference. Post-shutdown resubmission remains a precondition violation; the new
regression checks that it is diagnosed rather than blocked by lock reentrancy.
After this correction, the pthread suite passed 22/22 and the cooperative suite
passed 12/12; the release cooperative timer selection passed 5/5.

## Final verification evidence

| Configuration | Result |
| --- | --- |
| Windows native C++20 full suite | 20/20 passed |
| Windows portable task-system full suite | 20/20 passed |
| Windows portable main-executor full suite | 25/25 passed |
| Windows C++17 full suite | 20/20 passed |
| Windows native ASan timer/executor lifecycle selection | 8/8 passed |
| Windows portable ASan timer/executor lifecycle selection | 8/8 passed |
| Windows native shared-core timer/lifecycle/shutdown and core smoke | 8/8 passed |
| Windows portable shared-core timer/lifecycle/shutdown and both smoke consumers | 9/9 passed |
| Linux Clang + libc++ timer/executor lifecycle selection | 7/7 passed |
| Emscripten pthread full suite | 19/19 passed |
| Emscripten threadless runtime and configuration suite | 9/9 passed |
| Emscripten threadless configuration rejection scenarios | All five passed: conflicting task/main/thread systems, pool size, and compiler pthread flags |
| Doxygen preset | Built; generated HTML contains timer submission and blocking-capability ABI documentation |

The configuration scenarios exercise root CMake diagnostics rather than treating any
arbitrary configuration failure as success.

Targeted lifecycle lint is warning-clean under the supported C++17 compile commands.
C++20 lint suggests designated aggregate initializers that would break C++17 support.
Formatting and patch whitespace checks pass.

## Environment limitations

Emscripten 6.0.10 and Node 24.19.0 were already installed in WSL. LLVM's in-place
`llvm-objcopy` updates fail on the Windows-backed filesystem even when the input
artifact exists; separate-output updates work. With user approval, WASM validation
uses the same configure presets with build directories on the Linux filesystem under
`/home/sparent/.cache/stlab-system-timer-abi/`. No toolchain workaround was added to
production source.

The Linux portable TSan+UBSan configuration builds, but WSL aborts each test before
execution with `ThreadSanitizer: unexpected memory mapping`. This is not a passing
race check. macOS native libdispatch runtime and sanitizer coverage remain assigned
to the existing CI jobs; no macOS host is available locally.

Pre-existing dependency-discovery/cache warnings appear in WSL configuration.
Doxygen reports missing Graphviz `dot`, but HTML generation succeeds.

## Repository state

The approved specification was committed as `81b05b2`. Implementation and related
documentation are maintained on the isolated `worktree-system-timer-abi` branch.
The user's specification-table formatting was preserved.
The final portable suite uses `ctest --test-dir build\debug-portable`, since no
`debug-portable` test preset exists.
