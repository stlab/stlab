# System timer ABI and threadless Emscripten design

Date: 2026-09-30

## Goals

Move system timer state and platform scheduling into `stlab-core`, accessible through
the versioned C ABI. Use `emscripten_set_timeout()` for all Emscripten timers, including
pthread builds. Make `STLAB_EMSCRIPTEN_PTHREADS=OFF` select a supported cooperative
execution configuration. Restore supported `steady_clock::time_point` scheduling
alongside duration scheduling.

Preserve native executor behavior, task relocation ABI guards, and allocation-free
relocation of small task targets. Timer bookkeeping can require allocation; do not add
an extra heap-allocated callable wrapper merely to cross the ABI.

## Approved decisions

- Reuse `STLAB_EMSCRIPTEN_PTHREADS=OFF`; do not add a separate cooperative-mode option.
- In threadless mode, default to `STLAB_THREAD_SYSTEM=none`,
  `STLAB_TASK_SYSTEM=emscripten`, and `STLAB_MAIN_EXECUTOR=emscripten`.
- Route threadless high/default/low executor submissions asynchronously to the existing
  main executor queue. Priority hints have no distinction in this configuration.
- Route Emscripten timer registration and execution to the main runtime thread,
  regardless of the submitting thread.
- Terminate on a non-ready synchronous `await()` in threadless mode.
- Make threadless `await_for()` return the supplied future immediately, ignoring the
  timeout and preserving the pending result.
- Restore the `steady_clock::time_point` overload without deprecation.
- Cancel pending timers at `pre_exit()`, release their captures, and synchronize with
  callbacks already executing on other threads.
- Report timer resource failures through explicit C ABI status, translating them into
  exceptions in the client wrapper rather than throwing across the ABI.

## Existing implementation and deprecation history

`system_timer.hpp` currently contains libdispatch, Windows thread-pool, and portable
timer implementations, including client-side singleton state. Executor and main
executor submission already use task relocation through a versioned C ABI in
`stlab-core`.

The Emscripten toolchain option currently controls pthread compile/link flags, but
turning it off does not itself select a threadless task implementation. The current
non-pthread CI job builds only selected main executor targets.

Time-point scheduling was deprecated starting in commit
`88c8aa271afd53a40ca73a1b7c88a1ddae837ffa` on June 19, 2019, addressing
[#262](https://github.com/stlab/stlab/issues/262), "`system_timer` interface is cumbersome."
Commit `686c379f65a72b3ebbdc961446c539e35f5cf6e6` applied the public portable adapter
deprecation on June 20. The issue records the change as fixed in 1.5.0.

The stated rationale was that time points cannot be cast between clocks and that
expressing relative scheduling was cumbersome. It does not identify an incompatibility
between offering both duration and steady-clock deadline overloads. The 2023
`noexcept` task changes retained the earlier deprecation; they did not introduce it.

## Public scheduling contract

Retain the existing `system_timer` object and move-only `task<void() noexcept>`
submission interface.

Duration scheduling requests asynchronous execution after the specified delay.
Nonpositive delays request asynchronous execution without a delay, never an inline
call. A positive delay must not be shortened by rounding to a backend's resolution.
Execution can be late because of queueing, clock resolution, or event-loop availability.

The restored deadline overload accepts `std::chrono::steady_clock::time_point`.
It samples `steady_clock::now()` in the client and converts the remaining duration
to the same delay submission path. A past or current deadline requests asynchronous
execution without a delay. Sampling and submission overhead can make execution late,
but must not make it early.

Neither a C++ clock representation nor its epoch crosses the ABI. No arbitrary-clock
or wall-clock deadline overload is added.

Use a fixed-width signed 64-bit nanosecond delay at the ABI boundary, normalized to
nonnegative values. Positive fractional nanoseconds round upward. Inputs must be
finite and the positive normalized delay must fit the representation; diagnose
violations rather than performing an overflowing conversion. Negative delays and past
deadlines must be normalized before conversions or subtraction can overflow.

## Timer ABI and ownership

Add `stlab_v2_system_timer_submit` and its public ABI contract. It accepts the existing
task storage ABI guard, task concept, invocation pointer, relocation source, and
normalized delay. Export it from the Windows core DLL.

The submission entry point does not propagate exceptions. Its explicit fixed-width
status distinguishes success, allocation failure, and system-resource failure.
System-resource failure includes the numeric error and category information needed
to construct the corresponding exception in the client. The wrapper translates
allocation failure to `std::bad_alloc` and system-resource failure to
`std::system_error`; do not transport C++ exception objects, strings, or category
objects across the boundary.

Allocate required submission storage before consuming the task's relocation source.
On failure, leave the source unconsumed. On success, relocate its target exactly once
into core-owned scheduling storage. The caller still destroys the moved-from task
normally. Destroy the relocated target exactly once after execution or cancellation.

Catch only the resource failures that the status protocol represents. Internal
invariant violations are fatal, not recoverable status results. Never report success
if work was dropped, and never execute a task inline as a submission fallback.

Client modules supplying task operation pointers must remain loaded until all their
accepted timer tasks have completed or been canceled and destroyed.

## Compiled timer backends

Platform includes, singleton state, shutdown registration, queues, and callbacks move
out of the public header into compiled implementation files in `stlab-core`.

### Native backends

Libdispatch continues to schedule timer tasks using its global queue. Preserve
synchronization with executing callbacks without keeping canceled task captures alive
until a far-future deadline.

Windows continues to use thread-pool timers. Use relative deadlines rather than
converting steady-clock delays through calendar time. Round positive delays upward to
the platform resolution, and clean up partially created resources on failure.

The portable backend retains its steady-clock priority queue and timer worker.
Insertion of an earlier deadline must wake the worker and cause it to reconsider the
earliest deadline. Shutdown wakes and joins the worker and destroys pending targets.

### Emscripten backend

Select this timer backend based on the Emscripten target, independently of whether the
default task system is portable/pthread or cooperative.

`emscripten_set_timeout()` executes callbacks on the registering thread, and
`emscripten_clear_timeout()` must run on that same thread. Therefore, register,
re-arm, cancel, and execute timers on the main runtime thread. Pthread submissions
must be proxied asynchronously to that event loop, using the existing main executor
routing pattern rather than depending on the submitting worker returning to its own
event loop. Avoid executing user timer code during a proxy cancellation point.

Record the deadline at acceptance, so proxy latency does not restart the original
delay. Check remaining time when arming or re-arming. Round delays upward, split waits
that exceed JavaScript's timeout range, and recheck the monotonic deadline before
invocation so clamping or precision loss cannot cause early execution.

No portable timer thread is constructed on Emscripten.

## Shutdown contract

`pre_exit()` closes timer admission, cancels all accepted timers whose callbacks have
not committed to execution, and destroys their captures. It synchronizes with callbacks
already executing on other threads before returning. Shutdown must also account for
accepted Emscripten submissions still awaiting main-thread registration.

Timers and default-executor resources share one core teardown handler, registered
lazily on the first use of either service. Public `at_pre_exit()` handlers retain
their existing reverse-registration order, including the ability to register more
handlers during `pre_exit()`. Application handlers needed to unblock running core
work must be registered after that first use, so they run before core teardown.

Within the shared core handler, close timer admission and destroy pending captures,
wait for committed timer callbacks, then drain and join initialized default
executors. Keep executors available to active timer callbacks until timer shutdown
finishes, including first use of an executor by a finishing callback. Do not register
timer and executor cleanup independently: their relative initialization order cannot
determine their correct shutdown order.

The main queue remains available after `pre_exit()`, including the portable backend.
A main task can retire producers with `pre_exit()` and then enqueue a final exit task.
That task follows main work posted by the retired producers; it does not transitively
drain work that earlier main tasks enqueue behind it. Producers being joined must not
synchronously require main-queue progress while `pre_exit()` occupies the main thread.

Cancellation and callback execution have one synchronized ownership transition:
each accepted task is either invoked once or canceled without invocation, and is
destroyed once in either case.

Scheduling after timer shutdown is a diagnosed precondition violation. Shutdown
before the first timer or executor submission must not permit a new live core service
to be created afterward, even though no core teardown handler was needed before that
first use. Diagnose late first-use without creating a timer thread or task pool.

On native backends, do not call `pre_exit()` from a timer callback that shutdown would
need to join or wait for. Document this precondition. On Emscripten, a timer callback
may initiate `pre_exit()` on the main runtime thread: the current callback is already
committed, and shutdown cancels other pending timers without waiting for itself.
That callback must return normally.

Keep the existing Emscripten final-exit protocol: initiate `pre_exit()`, then perform
`emscripten_force_exit()` from a separate non-`noexcept` callback with the executable
linked using `-sEXIT_RUNTIME=1`.

## Threadless configuration and execution

Make the existing `STLAB_EMSCRIPTEN_PTHREADS` option available to Emscripten
configuration, not only to users of the repository's test toolchain.

When it is off, use the threadless defaults listed above. Reuse the existing generated
`STLAB_THREADS_NONE()` and `STLAB_TASK_SYSTEM_EMSCRIPTEN()` flags. Reject incompatible
explicit selections, including a threaded task backend, a non-Emscripten main
executor, or a positive `STLAB_TASK_POOL_MAXIMUM`. Do not silently override a
conflicting selection.

Pthread-enabled Emscripten builds retain the portable default task system. Detect and
reject mismatches between requested threading configuration and actual toolchain
pthread support. Non-Emscripten executor configurations retain their existing behavior.

In cooperative mode, executor submissions enter the existing main queue through the
core implementation. They are not executed inline. Tasks and coroutine continuations
cooperate by returning control to the JavaScript event loop; this design does not add
preemption, Asyncify, stack switching, or a nested event-loop pump.

Public concurrency headers must not acquire task-system, shared-core, or Windows
implementation branches. Runtime blocking capability comes through the versioned
core C ABI.

## Wait contracts

Add a versioned ABI query for whether blocking waits are supported by the configured
core. Threaded cores report support; the cooperative Emscripten core does not.

| Operation                         | Threadless behavior                                             |
| --------------------------------- | --------------------------------------------------------------- |
| `await()` on a ready future       | Existing value, void, or exception behavior                     |
| `await()` on a non-ready future   | `std::terminate()`, without blocking                            |
| `await_for()` with any timeout    | Return the supplied future immediately                          |
| Deprecated `blocking_get` helpers | Follow their corresponding await operation                      |
| Coroutine `co_await`              | Existing nonblocking continuation behavior                      |
| `invoke_waiting()`                | Terminate before invoking the supplied blocking-style operation |

Threadless `await_for()` does not attach a continuation, allocate wait state, or
consume a pending result merely to poll. Its return type remains `future<T>`, not an
optional or invalid-future sentinel. A caller can use `is_ready()`, or `get_try()` for
the type-appropriate polling result. Ready exceptional futures retain their existing
exception behavior when inspected.

The timeout is an upper bound on waiting, not a guarantee that execution sleeps for
that interval. Document the threadless immediate-return behavior explicitly.
Repeated polling must yield to the event loop; a synchronous busy loop prevents the
work being polled from progressing.

Threaded `await()`, `await_for()`, and portable pool blocking compensation retain
their existing behavior.

## Validation

Derive tests from public contracts and observable behavior.

- Timer tests cover duration and restored deadline overloads, future and past
  deadlines, zero/negative delays, upward rounding, no inline invocation, and
  exactly-once execution/destruction of move-only captures.
- Portable tests cover an earlier deadline submitted while the worker is waiting
  for a later deadline.
- Standalone lifecycle scenarios cover pending timer cancellation, capture release,
  synchronization with active callbacks, and shutdown before first submission.
- Core-only consumers prove that timer submission requires no internal C++ scheduler
  symbols. Extend Windows shared-core coverage for native and portable task systems,
  including the timer ABI export and the existing task ABI mismatch guard.
- Emscripten event-loop tests cover timer callback placement, submission from pthreads,
  cooperative executor routing, delayed tasks, cancellation, and continued progress
  after returning to the event loop.
- Threadless wait tests cover ready values/void/exceptions, immediate polling with
  retained pending futures and arbitrary timeout values, later completion, and
  non-ready `await()` termination in a separate process.
- Configuration checks cover valid pthread/threadless selections and explicit
  conflicts. Update the non-pthread CI entry to exercise supported cooperative
  behavior instead of using the vestigial portable/none combination.

Use project presets and the smallest relevant test selections locally, with C++17
compatibility, relevant sanitizer/lint checks, and native platform coverage in CI.
Do not run blocking-oriented test cases unchanged in the cooperative configuration;
provide event-loop-driven scenarios for that contract.

Update authoritative Doxygen contracts, configuration documentation, and the related
CI configuration with implementation. Record actual verification and any environment
limitations in the implementation handoff.

## Scope boundaries

No arbitrary-clock deadline API, public timer cancellation handle, runtime-installable
backend, or executor native-handle API is introduced. Do not change native executor
placement, make native timers depend on a main executor, or convert coroutine awaiting
into a synchronous wait.

This document records the approved design. Implementation follows after written-spec
review and preparation of the implementation plan.
