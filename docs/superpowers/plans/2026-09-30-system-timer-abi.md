# System Timer ABI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide core-owned ABI-stable timers, supported steady-clock deadlines, and cooperative threadless Emscripten execution.

**Architecture:** A backend-free timer wrapper relocates tasks into a selected compiled core backend using an explicit resource-error protocol. Emscripten timers run on the main runtime event loop. The core reports blocking capability so public waits can implement threadless polling without platform branches.

**Tech Stack:** C++17/20, CMake presets, Ninja, doctest, MSVC, libdispatch, Emscripten, CTest.

**Spec:** `docs\superpowers\specs\2026-09-30-system-timer-abi-design.md`

**Status:** Implementation, integration, and review corrections are complete. The checklists
below preserve the approved execution steps; final results and platform limitations are recorded
in `docs\superpowers\2026-09-30-system-timer-abi-handoff.md`. Ready for pull-request review.

## Global Constraints

- Preserve existing native executor behavior and task storage ABI guards.
- No C++ exception, clock representation, or error-category object crosses the timer ABI.
- A failed submission leaves the relocation source unconsumed.
- Timer bookkeeping may allocate; do not add a redundant heap-allocated callable wrapper.
- Positive delays round upward; nonpositive delays execute asynchronously without delay.
- All accepted targets are executed or canceled once and destroyed once.
- Assert invalid internal state; never run queued tasks inline as a fallback.
- Public concurrency headers must not acquire platform/task-system branches.
- Preserve C++17 compatibility and adjacent `///` contracts.
- Preserve unrelated and user-owned changes; never amend commits.
- Include the Copilot coauthor trailer in commits.

## File and Interface Map

Timer task:
- `include\stlab\concurrency\system_timer.hpp`: C ABI declarations, checked duration/deadline conversion, status-to-exception translation.
- `src\concurrency\system_timer_{portable,windows,libdispatch,emscripten}.cpp`: selected core implementation.
- `src\CMakeLists.txt`, `src\stlab.def`: select backend and export timer submission.
- `test\system_timer_test.cpp`, standalone timer lifecycle tests, `test\CMakeLists.txt`: public-contract tests.

Cooperative execution task:
- `CMakeLists.txt`, `cmake\StlabUtil.cmake`, Emscripten toolchain: option availability, defaults, and configuration validation.
- `src\concurrency\executor_abi.cpp`: Emscripten main-queue routing and blocking capability.
- `include\stlab\concurrency\default_executor.hpp`, `await.hpp`: capability declaration and wait semantics.
- Emscripten test executables: asynchronous progression and polling/termination contracts.

Integration task:
- `.github\matrix.json`, presets where needed, `README.md`, authoritative API comments.
- Dated handoff with actual validation evidence.

## Task 1: Core timer ABI and native timer backends

**Produces:** A shared public submission signature:

```cpp
struct stlab_v2_timer_status {
    std::int32_t code; // 0 success, 1 allocation, 2 generic error, 3 system error
    std::int32_t native_error;
};
extern "C" stlab_v2_timer_status stlab_v2_system_timer_submit(
    const unsigned char* task_abi_guard,
    const stlab_v2_task_concept* vtable,
    stlab_v2_task_proc invoke, void* source,
    std::int64_t delay_ns) noexcept;
```

- [ ] Add tests for supported deadlines with deprecation warnings treated as errors, future/past deadlines, zero/negative delays, exactly-once move-only target execution, pending capture cancellation, and shutdown before initial submission.
- [ ] Run `cmake --build --preset=debug-cpp20 --target stlab.test.system_timer` and demonstrate the restored deadline test fails against the old API.
- [ ] Implement the thin wrapper and explicit fixed-width status protocol. Normalize duration values before casts; reject nonfinite/unrepresentable positive delays explicitly. Preserve existing task relocation source ownership on all failure paths.
- [ ] Implement native compiled backends. Portable uses a steady-clock heap, wakes/rechecks on insertion, and joins/cancels at shutdown. Windows uses relative FILETIME deadlines and cleanup groups. Libdispatch uses cancelable sources with synchronized execution/cancellation accounting.
- [ ] Register one lazy core teardown handler at first timer/default-executor use. Preserve application-handler LIFO order; cancel timers and join active callbacks before draining initialized executors. Allow executor first-use by a finishing timer callback, and diagnose submissions after shutdown even when core was never initialized.
- [ ] Add backend selection and DLL export.
- [ ] Run timer/lifecycle tests for native and portable configurations and C++17. Link standalone consumers to `stlab-core` only; exercise shared-core builds.
- [ ] Record changes and validation before moving to integration.

Example observable scheduling test:

```cpp
auto [set, result] = stlab::package<int()>(stlab::immediate_executor, [] { return 42; });
const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
stlab::system_timer(deadline, std::move(set));
CHECK(stlab::await(std::move(result)) == 42);
CHECK(std::chrono::steady_clock::now() >= deadline);
```

## Task 2: Threadless configuration and waits

**Consumes:** Existing main executor ABI.
**Produces:**

```cpp
extern "C" std::int32_t stlab_v2_default_executor_supports_blocking() noexcept;
```

- [ ] Create event-loop-driven public tests for all three priority submissions, ready values/void/exceptions, non-ready polling with a retained future and arbitrary timeouts, later completion, and terminating non-ready `await()`.
- [ ] Run the new Emscripten tests against the old configuration to establish failure, or explicitly record unavailable toolchain validation rather than inventing a pass.
- [ ] Make `STLAB_EMSCRIPTEN_PTHREADS` available in Emscripten root configuration. OFF selects `none`/`emscripten`/`emscripten`; reject explicit conflicts and positive pool limits. Check actual `__EMSCRIPTEN_PTHREADS__` support.
- [ ] Route cooperative executor calls to `stlab_v2_main_executor_submit`, retaining the ABI guard. Compile portable worker/queue implementation only for native/threaded task systems.
- [ ] Add the capability ABI and Windows export. Return zero for cooperative mode and one otherwise.
- [ ] Apply wait behavior before allocation or recovery-continuation attachment:

```cpp
if (!stlab_v2_default_executor_supports_blocking()) return std::move(x); // await_for
if (!stlab_v2_default_executor_supports_blocking()) std::terminate(); // non-ready await
```

- [ ] Make `invoke_waiting` terminate before invoking a blocking-style operation in cooperative mode.
- [ ] Update Doxygen wait/executor contracts and preserve threaded tests.
- [ ] Run native future/executor tests and configuration negative tests.

## Task 3: Emscripten timer backend

**Consumes:** Task 1 timer submission/status interface and existing main executor.

- [ ] Add asynchronous timer tests for zero delay, restored deadlines, no early execution, main-thread affinity, worker submission, capture release, and shutdown while registration is queued.
- [ ] Compile/run the tests before implementing the backend to establish failure.
- [ ] Implement core-owned pending records with main-runtime registration, `emscripten_set_timeout`, and same-thread `emscripten_clear_timeout`. Accept storage before relocating the target. Keep callback metadata alive until queued proxy callbacks cannot access it.
- [ ] Record the acceptance deadline; proxy latency must not restart the delay. Check deadline before invocation. Bound individual timeout arms to `2147483647` milliseconds and re-arm until due.
- [ ] Synchronize closing, active callbacks, and pending registrations. Allow main-thread timer callbacks to call `pre_exit()` without self-wait. Keep native callback shutdown preconditions documented.
- [ ] Preserve explicit errors and source ownership; an asynchronous registration cannot report success then silently drop work.
- [ ] Run both pthread and threadless event-loop scenarios.

## Task 4: Integration, CI, documentation, and review

- [ ] Update non-pthread CI to select supported cooperative configuration and run contract tests instead of the portable/none vestigial setup. Preserve pthread full-suite coverage.
- [ ] Document `STLAB_EMSCRIPTEN_PTHREADS=OFF`, event-loop yielding, ignored polling timeouts, restored deadlines, timer callback placement, resource errors, and shutdown preconditions in README and API comments.
- [ ] Preserve main-queue admission and pending work after `pre_exit()`. Test a final main exit task following direct submissions from retired timer/default-executor producers; document that this FIFO fence is not a transitive drain and producers must not synchronously require main progress during shutdown.
- [ ] Run targeted timer/future/executor tests, core-only/shared checks, C++17, relevant sanitizer and formatting/lint checks. Run the full native suite if integration touches scheduler behavior.
- [ ] Review the final diff for exactly-once ownership, exception boundaries, shutdown races, and platform-free client headers.
- [ ] Write `docs\superpowers\2026-09-30-system-timer-abi-handoff.md` with actual results and environment limitations, not predictions.
- [ ] Leave implementation on the isolated branch; do not push, merge, or open a PR without user authorization.

## Plan Self-Review

Task 1 covers native timer relocation, errors, deadlines, and lifecycle. Task 2 covers option selection, cooperative executors, capability queries, and waits. Task 3 covers Emscripten timer placement/range/shutdown. Task 4 covers public documentation, cross-platform CI, and final verification. The ABI signatures above are the integration contracts shared between tasks.
