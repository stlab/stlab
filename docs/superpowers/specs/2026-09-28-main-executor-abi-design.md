# Main Executor ABI Design

- Status: Draft for review
- Date: 2026-09-28
- Follows: [#604](https://github.com/stlab/stlab/pull/604) — DLL-safe shared executor ABI
- Related: [#601](https://github.com/stlab/stlab/issues/601) — `system_timer` pre-exit cancellation

## Background

`main_executor` posts tasks to the application's main queue. Today it is implemented inline in
`include/stlab/concurrency/main_executor.hpp`, branching on `STLAB_MAIN_EXECUTOR` between Qt,
libdispatch, and Emscripten. There is no Windows implementation unless Qt is selected, and there is
no stlab-owned implementation for configurations without a platform main queue.

The original purpose of `main_executor` is portability of application code: iOS code posting to the
main queue should build unchanged for WebAssembly with Emscripten. `main_executor` adapts existing
platform main-queue mechanisms behind one interface; it does not supersede or impose a model on
them.

#604 moved process-shared executor state behind a versioned `extern "C"` ABI exported from
`stlab-core`. This design extends that ABI to the main executor and adds a portable backend for
parity with the portable task system.

## Findings that shape the design

- **libdispatch, Qt, Emscripten** each define a process main queue that any thread can post to.
- **Windows has no process main queue.** Every thread that creates a window has its own message
  queue; applications may run several UI threads (e.g. WPF dispatcher-per-thread, Explorer). The
  "main thread" is only the first thread by convention, and no API identifies a UI thread. Posting
  requires a target (`HWND`, or a `DispatcherQueue` obtained on the target thread).
- Windows mechanisms surveyed: Win32 `PostMessage` to a message-only window (universal; every
  mainstream framework pumps Win32 messages; 10,000 posted messages per queue quota);
  `Windows.System.DispatcherQueue` (OS, Windows 10 1709+, flat C header, priorities, shutdown
  semantics); WinUI 3 `Microsoft.UI.Dispatching.DispatcherQueue` (C++/WinRT and Windows App SDK
  only, adds no coverage over Win32 messages). Because none of these is a process main queue, none
  is used to implement `main_executor`.
- `dispatch_main()` parks the calling thread servicing the main queue and never returns.

## Goals

1. One portable interface: every configuration that has a main executor exports the same
   `stlab_v2_main_executor_submit` and `stlab_v2_main_executor_run` C ABI, so code using them is
   source-portable across iOS/macOS, Qt, WebAssembly, and portable builds.
2. Wrap native main queues where they exist; do not emulate one implicitly where the platform has
   none.
3. Provide an opt-in portable backend (stlab-owned FIFO queue driven by `run()`), analogous to the
   portable task system, usable on any platform including Windows.
4. Follow the #604 conventions: versioned `extern "C"` symbols in `stlab-core`, `.def`-restricted
   exports on Windows, allocation-free submission via task relocation, and no `STLAB_TASK_SYSTEM`,
   `STLAB_CORE_SHARED`, or `_WIN32` branching in public headers.

## Non-goals

- A Windows-native main executor. Windows has no main queue; a targeted executor (e.g.
  `window_executor(HWND)`) is a possible separate proposal.
- Executors for secondary UI threads.
- Runtime-installable backends or drivers. The backend is fixed when `stlab-core` is built.
- A `run()` that returns, a stop operation, or a drain-one-task (`execute_main()`) API.
- Main-queue priorities.

## Design

### 1. Backend selection (compile time)

`STLAB_MAIN_EXECUTOR` selects the backend compiled into `stlab-core`:

| Value | Default when | `submit` | `run` |
|---|---|---|---|
| `libdispatch` | libdispatch task system | `dispatch_async_f(dispatch_get_main_queue(), …)` | `dispatch_main()` |
| `qt5` / `qt6` | Qt found (non-Apple, non-Emscripten) | `QCoreApplication::postEvent` to a receiver on the application thread | `std::exit(QCoreApplication::exec())` |
| `emscripten` | Emscripten | pthreads: `emscripten_async_run_in_main_runtime_thread` + `emscripten_async_call`; single-threaded runtime: `emscripten_async_call` directly | `emscripten_exit_with_live_runtime()` |
| `portable` | never (opt-in) | push to stlab-owned FIFO queue | drain the queue on the calling thread forever |
| `none` | otherwise (including Windows without Qt) | not declared | not declared |

Defaults are unchanged from today's `stlab_detect_main_executor`; `portable` is added as an
explicit choice.

### 2. Public C ABI

Declared in `main_executor.hpp` only when `STLAB_MAIN_EXECUTOR` is not `none`:

```cpp
namespace stlab {
inline namespace v2 {

/// Submits one task to the main queue.
///
/// - Precondition: `task_abi_guard` points to `detail::current_task_storage_abi_guard::value`.
/// - Precondition: `vtable` and `invoke` are not `nullptr`.
/// - Precondition: `source` is the `relocation_source()` of a live `task<void() noexcept>`
///   sharing `vtable`/`invoke`, valid for the duration of this call.
/// - Postcondition: exactly one invocation of the relocated target is scheduled on the main
///   queue, after all tasks previously submitted from the calling thread.
extern "C" void stlab_v2_main_executor_submit(const unsigned char* task_abi_guard,
                                              const stlab_v2_task_concept* vtable,
                                              stlab_v2_task_proc invoke,
                                              void* source) noexcept;

/// Makes the calling thread service the main queue; never returns.
///
/// - Precondition: called at most once per process, from the thread the platform designates
///   as main where it designates one.
extern "C" [[noreturn]] void stlab_v2_main_executor_run();

} // namespace v2
} // namespace stlab
```

- Submission uses the same relocation arguments as the #604 executor submits, so the ABI performs
  no allocation beyond the backend's own task storage.
- The process usually ends by calling `std::exit()` (after `pre_exit()`) from a task, as with
  `dispatch_main()`. On Emscripten, executor tasks remain `noexcept`: the task calls `pre_exit()`,
  schedules a separate `emscripten_async_call()` callback, returns normally, and that callback calls
  `emscripten_force_exit(status)` (with `-sEXIT_RUNTIME=1`).
- The C++ surface is `stlab::main_executor` (unchanged usage) and a new inline
  `stlab::main_executor_run()` forwarding to the C symbol.
- Deviation from the draft ABI: `run()` is not `noexcept`. Emscripten
  `emscripten_exit_with_live_runtime()` unwinds with a JavaScript exception; with `noexcept`,
  optimized Release wasm tests terminate in `stlab_v2_main_executor_run`.
- The Emscripten backend supports both pthread and non-pthread builds. Pthread builds retain the
  proxy-to-main-runtime-thread bounce to avoid running while the main thread holds locks; non-pthread
  builds are already on the single runtime thread and post `run_one` directly with
  `emscripten_async_call()`.

### 3. Header restructuring

`main_executor.hpp` loses all backend code (Qt, libdispatch, Emscripten includes and types). It
contains only the ABI declarations and a thin `main_executor_type` that builds a
`task<void() noexcept>` and calls `stlab_v2_main_executor_submit`, mirroring `executor_type` in
`default_executor.hpp`. The only preprocessor condition is `STLAB_MAIN_EXECUTOR(NONE)`.

Backend implementations move to `src/concurrency/main_executor_<backend>.cpp`, one of which is
compiled into `stlab-core`. Backend link dependencies (Qt, libdispatch) move from `stlab` to
`stlab-core`.

### 4. Portable backend

- One process-wide FIFO queue of relocated `task<void() noexcept>` objects, protected by a mutex,
  with a condition variable for wake-up. Task storage reuses the #604 relocation storage.
- Tasks submitted before `run()` remain queued and execute once `run()` is called.
- `run()` binds the calling thread as the main thread and loops: wait, pop front, invoke.
- Registers an `at_pre_exit` handler eagerly during static initialization. Therefore `pre_exit()`
  before the first submission is supported: the queue closes, later submissions are destroyed
  without invocation, and `run()` blocks until process exit.
- Calling `run()` a second time violates a precondition and asserts.

### 5. Export surface and build

- `src/stlab.def` gains `stlab_v2_main_executor_submit` and `stlab_v2_main_executor_run`. `.def`
  exports are unconditional, so a `none` build of `stlab-core` provides stubs that assert; the
  public header does not declare them in that configuration.
- `STLAB_MAIN_EXECUTOR` accepts `portable`; `config.hpp.in` gains
  `STLAB_MAIN_EXECUTOR_PORTABLE()`.
- CI adds a Windows `STLAB_CORE_SHARED=ON` configuration with `STLAB_MAIN_EXECUTOR=portable`, and a
  portable main executor configuration on Linux.
- README, CLAUDE.md, and the `main_executor.hpp` Doxygen are updated to describe the backends and
  the Windows position.

## Testing

Tests are derived from the contract. Because `run()` never returns, each scenario is its own
executable that finishes by calling `std::exit()` from a task, or on Emscripten by calling
`pre_exit()` from the task and scheduling a later `emscripten_force_exit(status)` callback,
registered with CTest and built when the selected backend is `portable` (and, where CI supports it,
native backends):

- Tasks execute in submission order on the thread that called `run()`.
- Tasks submitted before `run()` execute after `run()` starts.
- Tasks submitted concurrently from many threads each execute exactly once.
- After `pre_exit()`, pending and later-submitted tasks are destroyed without invocation.
- Shared-core smoke test: a consumer linked only against the import library submits via
  `stlab_v2_main_executor_submit` and drives `stlab_v2_main_executor_run`.

## Resolved questions

- Qt `run()` is `std::exit(QCoreApplication::exec())`. Its precondition is that a
  `QCoreApplication` exists; applications must call `pre_exit()` before quitting the Qt loop so
  STLab process-shared state is closed before exit.
- Emscripten `run()` is `emscripten_exit_with_live_runtime()` and is intentionally not `noexcept`,
  as recorded in Design §2. Task 4 verified the pthread path with `-sPROXY_TO_PTHREAD` and
  `-fwasm-exceptions`; review fixes added and verified the non-pthread path, where the backend posts
  with `emscripten_async_call()` directly. Test shutdown uses a two-stage `pre_exit()` plus
  `emscripten_force_exit(status)` callback and requires `-sEXIT_RUNTIME=1`.

## Possible follow-on work

- A Windows targeted executor (`window_executor(HWND)` via a message-only window, or
  `Windows.System.DispatcherQueue`), as a separate proposal.
