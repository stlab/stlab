# Main Executor ABI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move every `main_executor` backend behind a versioned C ABI (`stlab_v2_main_executor_submit` / `stlab_v2_main_executor_run`) compiled into `stlab-core`, and add an opt-in portable backend.

**Architecture:** `main_executor.hpp` becomes a thin, backend-free wrapper that relocates a `task<void() noexcept>` across the C ABI (same pattern as `default_executor.hpp` from #604). One backend source file, selected by `STLAB_MAIN_EXECUTOR`, is compiled into `stlab-core`. The portable, libdispatch, and Emscripten backends share a mutex-protected FIFO (`main_task_queue`) so submission does not allocate per task; native backends post exactly one wake per task, and each wake pops exactly one task. Qt keeps one posted `QEvent` per task that owns the task.

**Tech Stack:** C++17/20, CMake 3.24+ presets, Ninja, MSVC, GCC/Clang, Apple Clang + libdispatch, Qt5/Qt6, Emscripten (`-sPROXY_TO_PTHREAD`, `-fwasm-exceptions`), CTest.

**Spec:** `docs/superpowers/specs/2026-09-28-main-executor-abi-design.md`

## Global Constraints

- ABI symbols: `stlab_v2_main_executor_submit(const unsigned char* task_abi_guard, const stlab_v2_task_concept* vtable, stlab_v2_task_proc invoke, void* source) noexcept` and `[[noreturn]] stlab_v2_main_executor_run() noexcept`, both `extern "C"` in `namespace stlab { inline namespace v2 {`.
- The guard argument is accepted and ignored (never compared, loaded, or branched on), exactly as for the #604 executor submits.
- `run()` never returns on any backend.
- `STLAB_MAIN_EXECUTOR` values: `libdispatch`, `qt5`, `qt6`, `emscripten`, `portable`, `none`. Defaults are unchanged (`portable` is never auto-selected); Windows and Linux without Qt default to `none`.
- The ABI and `stlab::main_executor_run()` are declared only when `STLAB_MAIN_EXECUTOR` is not `none`. `stlab-core` always defines both symbols (asserting stubs for `none`) so the Windows `.def` export list is unconditional.
- Public headers must not branch on `STLAB_TASK_SYSTEM`, `STLAB_CORE_SHARED`, or `_WIN32`. After Task 4, `main_executor.hpp` branches only on `STLAB_MAIN_EXECUTOR(NONE)`.
- Portable backend: after `pre_exit()`, pending and later-submitted tasks are destroyed without invocation. Native backends keep platform semantics.
- No new per-task heap allocation in the portable, libdispatch, or Emscripten submission paths (deque growth is amortized).
- Assert violated invariants; never execute queued work inline as a fallback.
- Every new class, struct, and function declaration has an adjacent `///` contract comment.
- Tests are derived from the contract, not the implementation. Each test scenario is a standalone executable that ends by calling `stlab::pre_exit()` then `std::exit()` from a task.
- Preserve C++17 compatibility; `.clang-format` (100 columns, 4-space indent).
- All work happens in the worktree `D:\repos\github.com\stlab\stlab\.claude\worktrees\main-executor-abi` on branch `main-executor-abi`. Never commit to `main`.
- Commit messages end with the trailer `Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>`.

## Environment Notes

- Windows commands run inside a VS developer environment:
  `& $env:ComSpec /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cd /d D:\repos\github.com\stlab\stlab\.claude\worktrees\main-executor-abi && <command>'`
  The `'vswhere.exe' is not recognized` line printed by `vcvars64.bat` is harmless noise.
- Qt and Emscripten are not installed on Windows. WSL `Ubuntu` has `cmake` 3.28, `ninja`, `g++`, `git`, but no Qt, no node, and `sudo` needs a password. Task 3 and Task 4 describe no-sudo installs; if they fail, stop and ask the user to run `sudo apt-get install -y qt6-base-dev` (Qt) and rely on CI for Emscripten.

## File Structure

- `include/stlab/concurrency/main_executor.hpp` — Backend-free public header: ABI declarations, `detail::main_executor_type`, `main_executor`, `main_executor_run()`.
- `src/concurrency/detail/main_task_queue.hpp` (new) — `detail::main_task_queue`: FIFO of relocated tasks with `push`, `pop`, `wait_pop`, `close`. Shared by portable, libdispatch, and Emscripten backends.
- `src/concurrency/main_executor_portable.cpp` (new) — Portable backend.
- `src/concurrency/main_executor_libdispatch.cpp` (new) — libdispatch backend.
- `src/concurrency/main_executor_qt.cpp` (new) — Qt5/Qt6 backend.
- `src/concurrency/main_executor_emscripten.cpp` (new) — Emscripten backend.
- `src/concurrency/main_executor_none.cpp` (new) — Asserting stubs so `.def` exports resolve.
- `src/CMakeLists.txt` — Select exactly one backend source for `stlab-core`; move Qt/libdispatch main-executor link deps to `stlab-core`.
- `src/stlab.def` — Export the two new symbols.
- `CMakeLists.txt` — Accept and validate `portable`; move main-executor link deps off `stlab`.
- `cmake/StlabUtil.cmake`, `include/stlab/config.hpp.in` — `STLAB_MAIN_EXECUTOR_PORTABLE()`.
- `CMakePresets.json` — `debug-portable-main` preset; `docs` preset uses `portable` so the API is documented.
- `test/main_executor_test_host.hpp` (new) — Test harness: host setup (Qt application object), `run`, and `finish`.
- `test/main_executor_order_test.cpp` (new) — FIFO, pre-`run()` submissions, thread affinity.
- `test/main_executor_concurrent_test.cpp` (new) — Concurrent submitters, exactly-once.
- `test/main_executor_pre_exit_test.cpp` (new) — Portable only: `pre_exit()` discards tasks.
- `test/CMakeLists.txt` — Register the tests for backends that implement the ABI.
- `.github/matrix.json`, `.github/workflows/stlab.yml` — CI configurations (Windows shared portable main, Linux portable main, Linux Qt6); Unix configure honors `cmake_options`.
- `README.md`, `CLAUDE.md`, `.github/copilot-instructions.md` (if tracked), spec, and handoff doc `docs/superpowers/2026-09-28-main-executor-abi-handoff.md`.

---

### Task 1: ABI, portable backend, build plumbing, and contract tests

**Files:**
- Modify: `include/stlab/concurrency/main_executor.hpp`
- Create: `src/concurrency/detail/main_task_queue.hpp`
- Create: `src/concurrency/main_executor_portable.cpp`
- Create: `src/concurrency/main_executor_none.cpp`
- Modify: `src/CMakeLists.txt`, `src/stlab.def`
- Modify: `CMakeLists.txt:126-127`, `cmake/StlabUtil.cmake:117-127`, `include/stlab/config.hpp.in:59-64`
- Modify: `CMakePresets.json` (new `debug-portable-main` configure/build/test presets; `docs` preset `STLAB_MAIN_EXECUTOR` → `portable`)
- Create: `test/main_executor_test_host.hpp`, `test/main_executor_order_test.cpp`, `test/main_executor_concurrent_test.cpp`, `test/main_executor_pre_exit_test.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces (C ABI, `namespace stlab { inline namespace v2 {`):
  - `extern "C" void stlab_v2_main_executor_submit(const unsigned char* task_abi_guard, const stlab_v2_task_concept* vtable, stlab_v2_task_proc invoke, void* source) noexcept;`
  - `extern "C" [[noreturn]] void stlab_v2_main_executor_run() noexcept;`
- Produces (C++, `namespace stlab { STLAB_VERSION_NAMESPACE_BEGIN()`): `detail::main_executor_type`, `inline constexpr auto main_executor`, `[[noreturn]] inline void main_executor_run() noexcept`.
- Produces (internal, `src/concurrency/detail/main_task_queue.hpp`, `namespace stlab { STLAB_VERSION_NAMESPACE_BEGIN() namespace detail {`): `class main_task_queue` with `auto push(const task<void() noexcept>::concept_t*, task<void() noexcept>::invoke_t, void*) -> bool`, `auto pop() -> task<void() noexcept>`, `auto wait_pop() -> task<void() noexcept>`, `void close()`.
- Produces (tests): `test/main_executor_test_host.hpp` with `namespace main_executor_test { template <class F> [[noreturn]] void run(int& argc, char** argv, F start); [[noreturn]] void finish(bool ok, const char* message); }`.
- Produces (CMake): `test/CMakeLists.txt` variable `stlab_main_executor_abi_backends` (list); later tasks append to it.

- [ ] **Step 1: Add the `portable` configuration value**

In `include/stlab/config.hpp.in`, after `#cmakedefine01 STLAB_MAIN_EXECUTOR_QT6()` add:

```cpp
#cmakedefine01 STLAB_MAIN_EXECUTOR_PORTABLE()
```

In `cmake/StlabUtil.cmake` `stlab_generate_config_file`, extend the main-executor chain:

```cmake
  elseif (STLAB_MAIN_EXECUTOR STREQUAL "qt6")
    set( STLAB_MAIN_EXECUTOR_QT6 TRUE )
  elseif (STLAB_MAIN_EXECUTOR STREQUAL "portable")
    set( STLAB_MAIN_EXECUTOR_PORTABLE TRUE )
  elseif (STLAB_MAIN_EXECUTOR STREQUAL "none")
```

Also update the table comment above `stlab_detect_main_executor` with a row: `# | portable     | stlab-owned queue serviced by main_executor_run() (opt-in) |`.

In `CMakeLists.txt` replace line 127 and add validation directly after it:

```cmake
set(STLAB_MAIN_EXECUTOR ${STLAB_DEFAULT_MAIN_EXECUTOR} CACHE STRING "Main executor to use (qt5|qt6|libdispatch|emscripten|portable|none).")
set_property(CACHE STLAB_MAIN_EXECUTOR PROPERTY STRINGS qt5 qt6 libdispatch emscripten portable none)
if(NOT STLAB_MAIN_EXECUTOR MATCHES "^(qt5|qt6|libdispatch|emscripten|portable|none)$")
  message(FATAL_ERROR "STLAB_MAIN_EXECUTOR must be one of qt5|qt6|libdispatch|emscripten|portable|none (got \"${STLAB_MAIN_EXECUTOR}\").")
endif()
```

- [ ] **Step 2: Add the presets**

In `CMakePresets.json` `configurePresets`, after `debug-portable`:

```json
        {
            "name": "debug-portable-main",
            "description": "Debug build with the portable main executor",
            "inherits": "base-cpp20",
            "cacheVariables": {
                "STLAB_MAIN_EXECUTOR": "portable"
            }
        },
```

In `buildPresets` add `{ "name": "debug-portable-main", "configurePreset": "debug-portable-main" }` and in `testPresets` add `{ "name": "debug-portable-main", "description": "Run tests with the portable main executor", "configurePreset": "debug-portable-main" }`. In the `docs` preset change `"STLAB_MAIN_EXECUTOR": "none"` to `"STLAB_MAIN_EXECUTOR": "portable"` so Doxygen sees the declarations.

- [ ] **Step 3: Write the test harness**

Create `test/main_executor_test_host.hpp`:

```cpp
/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#ifndef STLAB_TEST_MAIN_EXECUTOR_TEST_HOST_HPP
#define STLAB_TEST_MAIN_EXECUTOR_TEST_HOST_HPP

#include <stlab/concurrency/main_executor.hpp>
#include <stlab/config.hpp>
#include <stlab/pre_exit.hpp>

#include <cstdio>
#include <cstdlib>

#if STLAB_MAIN_EXECUTOR(QT5) || STLAB_MAIN_EXECUTOR(QT6)
#include <QCoreApplication>
#endif

namespace main_executor_test {

/// Runs pre-exit handlers and terminates the process with a status reflecting `ok`.
///
/// - Postcondition: never returns; prints `message` to `stderr` when `ok` is `false`.
[[noreturn]] inline void finish(bool ok, const char* message) {
    if (!ok) std::fprintf(stderr, "FAILED: %s\n", message);
    stlab::pre_exit();
    std::exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
}

/// Establishes the host application the backend requires, calls `start()`, then services the main
/// queue on the calling thread.
///
/// - Precondition: called once, from `main()`, with `main()`'s own `argc` (Qt retains a
///   reference to it).
template <class F>
[[noreturn]] void run(int& argc, char** argv, F start) {
#if STLAB_MAIN_EXECUTOR(QT5) || STLAB_MAIN_EXECUTOR(QT6)
    QCoreApplication application{argc, argv}; // Never destroyed: run() does not return.
#else
    (void)argc;
    (void)argv;
#endif
    start();
    stlab::main_executor_run();
}

} // namespace main_executor_test

#endif
```

- [ ] **Step 4: Write the order test**

Create `test/main_executor_order_test.cpp`:

```cpp
/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

// Contract: tasks run in submission order on the main queue; tasks submitted before
// `main_executor_run()` run after it starts; the main queue is serviced on the thread that calls
// `main_executor_run()` (Emscripten: the main runtime thread).

#include "main_executor_test_host.hpp"

#include <stlab/concurrency/main_executor.hpp>
#include <stlab/config.hpp>

#include <cstddef>
#include <thread>
#include <vector>

namespace {

constexpr int task_count = 1000;

std::vector<int> order;
std::thread::id main_queue_thread;
bool single_thread = true;

} // namespace

int main(int argc, char** argv) {
    const auto run_thread = std::this_thread::get_id();

    main_executor_test::run(argc, argv, [run_thread] {
        order.reserve(task_count);
        for (int i = 0; i != task_count; ++i) {
            stlab::main_executor([i]() noexcept {
                if (order.empty()) main_queue_thread = std::this_thread::get_id();
                single_thread = single_thread && main_queue_thread == std::this_thread::get_id();
                order.push_back(i);
            });
        }

        stlab::main_executor([run_thread]() noexcept {
            bool in_order = order.size() == static_cast<std::size_t>(task_count);
            for (int i = 0; in_order && i != task_count; ++i) in_order = order[i] == i;

            bool on_run_thread = main_queue_thread == run_thread;
#if STLAB_MAIN_EXECUTOR(EMSCRIPTEN)
            on_run_thread = true; // The Emscripten main queue is the main runtime thread.
#endif
            if (!in_order) main_executor_test::finish(false, "tasks did not run in FIFO order");
            if (!single_thread) main_executor_test::finish(false, "tasks ran on multiple threads");
            if (!on_run_thread) main_executor_test::finish(false, "tasks ran off the run() thread");
            main_executor_test::finish(true, "");
        });
    });
}
```

- [ ] **Step 5: Write the concurrent-submission test**

Create `test/main_executor_concurrent_test.cpp`:

```cpp
/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

// Contract: every task submitted concurrently from many threads runs exactly once on the main
// queue, and tasks from one submitting thread run in that thread's submission order.

#include "main_executor_test_host.hpp"

#include <stlab/concurrency/main_executor.hpp>

#include <cstddef>
#include <thread>
#include <vector>

namespace {

constexpr int thread_count = 8;
constexpr int tasks_per_thread = 1000;
constexpr int total = thread_count * tasks_per_thread;

// Accessed only from the main queue.
std::vector<int> runs(total, 0);
std::vector<int> last_seen(thread_count, -1);
bool per_thread_fifo = true;
int completed = 0;

// Written by `main()` before submitters start; joined from the final task.
std::vector<std::thread> submitters;

void check_and_finish() noexcept {
    for (auto& t : submitters) t.join();
    for (int n : runs) {
        if (n != 1) main_executor_test::finish(false, "a task did not run exactly once");
    }
    if (!per_thread_fifo) main_executor_test::finish(false, "per-thread order was not preserved");
    main_executor_test::finish(true, "");
}

} // namespace

int main(int argc, char** argv) {
    main_executor_test::run(argc, argv, [] {
        submitters.reserve(thread_count);
        for (int t = 0; t != thread_count; ++t) {
            submitters.emplace_back([t] {
                for (int i = 0; i != tasks_per_thread; ++i) {
                    stlab::main_executor([t, i]() noexcept {
                        ++runs[static_cast<std::size_t>(t * tasks_per_thread + i)];
                        per_thread_fifo = per_thread_fifo && last_seen[t] == i - 1;
                        last_seen[t] = i;
                        if (++completed == total) check_and_finish();
                    });
                }
            });
        }
    });
}
```

- [ ] **Step 6: Write the portable pre-exit test**

Create `test/main_executor_pre_exit_test.cpp`:

```cpp
/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

// Contract (portable backend): after `pre_exit()`, pending and later-submitted main-executor
// tasks are destroyed without being invoked.

#include "main_executor_test_host.hpp"

#include <stlab/concurrency/main_executor.hpp>
#include <stlab/pre_exit.hpp>

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

std::weak_ptr<int> pending_state;
bool pending_invoked = false;
bool later_invoked = false;

void report(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main(int argc, char** argv) {
    main_executor_test::run(argc, argv, [] {
        stlab::main_executor([]() noexcept {
            stlab::pre_exit();
            report(pending_state.expired(), "pending task was not destroyed by pre_exit()");
            report(!pending_invoked, "pending task was invoked");

            auto later = std::make_shared<int>(0);
            std::weak_ptr<int> later_state = later;
            stlab::main_executor([p = std::move(later)]() noexcept { later_invoked = true; });
            report(later_state.expired(), "task submitted after pre_exit() was not destroyed");
            report(!later_invoked, "task submitted after pre_exit() was invoked");

            std::exit(EXIT_SUCCESS); // pre_exit() already ran.
        });

        auto pending = std::make_shared<int>(0);
        pending_state = pending;
        stlab::main_executor([p = std::move(pending)]() noexcept { pending_invoked = true; });
    });
}
```

- [ ] **Step 7: Register the tests**

In `test/CMakeLists.txt`, after the `stlab.test.portable_shared_smoke` block, add:

```cmake
################################################################################
#
# Main executor scenarios. `main_executor_run()` never returns, so each scenario is a standalone
# executable that exits from a task. They link only against `stlab-core` to prove a consumer can
# use the main executor through the exported C ABI alone.
#
set(stlab_main_executor_abi_backends portable)

if(STLAB_MAIN_EXECUTOR IN_LIST stlab_main_executor_abi_backends)
  set(stlab_main_executor_tests order concurrent)
  if(STLAB_MAIN_EXECUTOR STREQUAL "portable")
    list(APPEND stlab_main_executor_tests pre_exit)
  endif()

  foreach(scenario IN LISTS stlab_main_executor_tests)
    set(target stlab.test.main_executor_${scenario})
    add_executable(${target} main_executor_${scenario}_test.cpp main_executor_test_host.hpp)
    target_link_libraries(${target} PRIVATE stlab-core)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 60)
    stlab_copy_runtime_dlls(${target})
  endforeach()
endif()
```

- [ ] **Step 8: Configure and verify the tests fail to build**

Run (Windows, dev environment): `cmake --preset=debug-portable-main && cmake --build --preset=debug-portable-main`
Expected: compile FAIL in the new tests — `main_executor_run` is not a member of `stlab` (the header has no portable branch yet).

- [ ] **Step 9: Write the shared queue**

Create `src/concurrency/detail/main_task_queue.hpp`:

```cpp
/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#ifndef STLAB_SRC_CONCURRENCY_DETAIL_MAIN_TASK_QUEUE_HPP
#define STLAB_SRC_CONCURRENCY_DETAIL_MAIN_TASK_QUEUE_HPP

#include <stlab/concurrency/task.hpp>
#include <stlab/config.hpp>

#include <cassert>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {

/// FIFO of main-executor tasks shared by submitting threads and the thread servicing the main
/// queue.
class main_task_queue {
    using task_t = task<void() noexcept>;

    std::mutex _mutex;
    std::condition_variable _ready;
    std::deque<task_t> _tasks;
    bool _closed{false};

public:
    /// Appends the task relocated from `source`.
    ///
    /// - Precondition: `source` is the `relocation_source()` of a live task sharing
    ///   `vtable`/`invoke`.
    /// - Postcondition: returns `true` if the task was appended; if the queue is closed, the
    ///   relocated task is destroyed without invocation and `false` is returned.
    auto push(const task_t::concept_t* vtable, task_t::invoke_t invoke, void* source) -> bool {
        {
            std::unique_lock<std::mutex> lock{_mutex};
            if (!_closed) {
                _tasks.emplace_back(vtable, invoke, source);
                lock.unlock();
                _ready.notify_one();
                return true;
            }
        }
        task_t discarded{vtable, invoke, source};
        return false;
    }

    /// Removes and returns the oldest task.
    ///
    /// - Precondition: the queue is not empty.
    auto pop() -> task_t {
        std::lock_guard<std::mutex> lock{_mutex};
        assert(!_tasks.empty() && "main executor wake without a queued task.");
        auto result = std::move(_tasks.front());
        _tasks.pop_front();
        return result;
    }

    /// Waits until a task is available, then removes and returns the oldest task.
    ///
    /// - Postcondition: after `close()`, never returns.
    auto wait_pop() -> task_t {
        std::unique_lock<std::mutex> lock{_mutex};
        _ready.wait(lock, [&] { return !_tasks.empty(); });
        auto result = std::move(_tasks.front());
        _tasks.pop_front();
        return result;
    }

    /// Destroys all pending tasks without invoking them and makes later `push()` calls discard
    /// their task.
    ///
    /// - Complexity: linear in the number of pending tasks.
    void close() {
        std::deque<task_t> discarded;
        {
            std::lock_guard<std::mutex> lock{_mutex};
            _closed = true;
            swap(discarded, _tasks);
        }
    }
};

} // namespace detail
STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

#endif
```

- [ ] **Step 10: Write the portable backend and the `none` stubs**

Create `src/concurrency/main_executor_portable.cpp`:

```cpp
/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include "detail/main_task_queue.hpp"

#include <stlab/concurrency/main_executor.hpp>
#include <stlab/config.hpp>
#include <stlab/pre_exit.hpp>

#include <atomic>
#include <cassert>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {
namespace {

/// Returns the process-shared main queue, registering its pre-exit handler on first use.
///
/// The queue is intentionally never destroyed so no main-queue task is destroyed during static
/// destruction.
auto main_tasks() -> main_task_queue& {
    static auto& queue = *new main_task_queue; // NOLINT(cppcoreguidelines-owning-memory)
    static const bool registered = [] {
        at_pre_exit([]() noexcept { main_tasks().close(); });
        return true;
    }();
    (void)registered;
    return queue;
}

/// Set once the main queue starts being serviced.
std::atomic<bool> running{false};

} // namespace
} // namespace detail
STLAB_VERSION_NAMESPACE_END()

inline namespace v2 {

/// Submits one task to the portable main queue.
extern "C" void stlab_v2_main_executor_submit(const unsigned char* /*task_abi_guard*/,
                                              const stlab_v2_task_concept* vtable,
                                              stlab_v2_task_proc invoke,
                                              void* source) noexcept {
    assert(vtable != nullptr && invoke != nullptr && "Task vtable/invoke must not be null.");
    (void)STLAB_VERSION_NAMESPACE()::detail::main_tasks().push(vtable, invoke, source);
}

/// Services the portable main queue on the calling thread; never returns.
extern "C" [[noreturn]] void stlab_v2_main_executor_run() noexcept {
    [[maybe_unused]] const bool was_running =
        STLAB_VERSION_NAMESPACE()::detail::running.exchange(true);
    assert(!was_running && "main_executor_run() called more than once.");
    auto& queue = STLAB_VERSION_NAMESPACE()::detail::main_tasks();
    while (true) {
        auto task = queue.wait_pop();
        task();
    }
}

} // namespace v2
} // namespace stlab
```

Create `src/concurrency/main_executor_none.cpp`:

```cpp
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
extern "C" [[noreturn]] void stlab_v2_main_executor_run() noexcept {
    assert(false && "No main executor is configured (STLAB_MAIN_EXECUTOR=none).");
    std::abort();
}

} // namespace v2
} // namespace stlab
```

- [ ] **Step 11: Rewrite the header for the portable backend (native branches unchanged for now)**

In `include/stlab/concurrency/main_executor.hpp`:

1. Update the file comment:

```cpp
/*! @file main_executor.hpp
 *  @brief Executor for the application's main queue.
 *
 *  @details
 *  Tasks submitted to `main_executor` run in submission order on the main queue selected by
 *  `STLAB_MAIN_EXECUTOR` when `stlab-core` is built: the libdispatch main queue, the Qt
 *  application event loop, the Emscripten main runtime thread, or (opt-in) a portable
 *  stlab-owned queue. `main_executor_run()` services the main queue on the calling thread and
 *  never returns, like `dispatch_main()`; the program ends by calling `pre_exit()` and
 *  `std::exit()` from a task.
 *
 *  Windows has no process main queue (each UI thread owns its message queue), so no main executor
 *  is provided there unless `STLAB_MAIN_EXECUTOR` selects Qt or `portable`.
 */
```

2. Replace the include block with:

```cpp
#include <stlab/config.hpp>

#if STLAB_MAIN_EXECUTOR(PORTABLE)
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/task.hpp>

#include <type_traits>
#include <utility>
#elif STLAB_MAIN_EXECUTOR(QT5) || STLAB_MAIN_EXECUTOR(QT6)
// ... existing Qt include block, unchanged ...
#elif STLAB_MAIN_EXECUTOR(LIBDISPATCH)
#include <dispatch/dispatch.h>
#elif STLAB_MAIN_EXECUTOR(EMSCRIPTEN)
#include <stlab/concurrency/default_executor.hpp>
#endif
```

(`default_executor.hpp` supplies `stlab_v2_task_proc`; `task.hpp` supplies `stlab_v2_task_concept` and `detail::current_task_storage_abi_guard`.)

3. Immediately after the includes (before `namespace stlab { STLAB_VERSION_NAMESPACE_BEGIN()`), add:

```cpp
#if STLAB_MAIN_EXECUTOR(PORTABLE)

namespace stlab {
inline namespace v2 {

/** @addtogroup stlab_concurrency_executor_abi
 *  @{
 */

/// Submits one task to the main queue.
///
/// - Precondition: `task_abi_guard` points to `detail::current_task_storage_abi_guard::value`.
/// - Precondition: `vtable` and `invoke` are not `nullptr`.
/// - Precondition: `source` is the `relocation_source()` of a live `task<void() noexcept>` sharing
///   `vtable`/`invoke`, valid for the duration of this call.
/// - Postcondition: exactly one invocation of the relocated target is scheduled on the main queue,
///   after every task previously submitted from the calling thread.
extern "C" void stlab_v2_main_executor_submit(const unsigned char* task_abi_guard,
                                              const stlab_v2_task_concept* vtable,
                                              stlab_v2_task_proc invoke,
                                              void* source) noexcept;

/// Services the main queue on the calling thread; never returns.
///
/// - Precondition: called at most once per process, from the thread the platform designates as
///   main where it designates one.
extern "C" [[noreturn]] void stlab_v2_main_executor_run() noexcept;

/** @} */

} // namespace v2
} // namespace stlab

#endif
```

4. Inside `namespace detail {`, before the `#if STLAB_MAIN_EXECUTOR(QT5) || ...` branch, add a portable branch and turn the existing `#if` into `#elif`:

```cpp
#if STLAB_MAIN_EXECUTOR(PORTABLE)

/// Executor that submits `void() noexcept` tasks to the main queue through the shared core ABI.
struct main_executor_type {
    using result_type = void;

    /// Schedules `f` to run on the main queue after every task previously submitted from the
    /// calling thread.
    template <class F>
    auto operator()(F&& f) const -> std::enable_if_t<std::is_nothrow_invocable_v<std::decay_t<F>>> {
        task<void() noexcept> t{std::forward<F>(f)};
        stlab_v2_main_executor_submit(&current_task_storage_abi_guard::value,
                                      t.relocation_concept(), t.relocation_invoke(),
                                      t.relocation_source());
    }
};

#elif STLAB_MAIN_EXECUTOR(QT5) || STLAB_MAIN_EXECUTOR(QT6)
```

5. After `inline constexpr auto main_executor = detail::main_executor_type{};`, add:

```cpp
#if STLAB_MAIN_EXECUTOR(PORTABLE)
/// Services the main queue on the calling thread; never returns.
///
/// - Precondition: called at most once per process, from the thread the platform designates as
///   main where it designates one.
[[noreturn]] inline void main_executor_run() noexcept { stlab_v2_main_executor_run(); }
#endif
```

and update the `main_executor` comment to:

```cpp
/// Runs `void() noexcept` tasks in submission order on the configured main queue.
```

- [ ] **Step 12: Select the backend source and export the symbols**

In `src/CMakeLists.txt`, after the `target_sources(stlab-core ...)` line, add:

```cmake
if(STLAB_MAIN_EXECUTOR STREQUAL "portable")
  target_sources(stlab-core PRIVATE concurrency/main_executor_portable.cpp)
else()
  target_sources(stlab-core PRIVATE concurrency/main_executor_none.cpp)
endif()
```

In `src/stlab.def` append:

```
    stlab_v2_main_executor_submit
    stlab_v2_main_executor_run
```

- [ ] **Step 13: Build and run the portable main executor tests**

Run: `cmake --preset=debug-portable-main && cmake --build --preset=debug-portable-main && ctest --preset=debug-portable-main --output-on-failure`
Expected: build succeeds with no warnings; all tests pass, including `stlab.test.main_executor_order`, `stlab.test.main_executor_concurrent`, `stlab.test.main_executor_pre_exit`.

Then the shared-core variant:
`cmake --preset=debug-portable-main -B build/portable-main-shared -DSTLAB_CORE_SHARED=ON && cmake --build build/portable-main-shared && ctest --test-dir build/portable-main-shared --output-on-failure`
Expected: all pass (the scenarios link only against the `stlab-core` import library).

Then regression on the default configuration (main executor `none`):
`cmake --build --preset=debug-cpp20 && ctest --preset=debug-cpp20`
Expected: 13/13 pass; no main-executor tests are registered.

- [ ] **Step 14: Verify the tests detect a broken backend**

Temporarily change `main_task_queue::wait_pop()` to pop from the back (`_tasks.back()` / `pop_back()`), rebuild `debug-portable-main`, and run `ctest --preset=debug-portable-main -R main_executor_order --output-on-failure`.
Expected: FAIL with `tasks did not run in FIFO order`. Revert the change and confirm it passes again.

- [ ] **Step 15: Commit**

```bash
git add include/stlab/config.hpp.in cmake/StlabUtil.cmake CMakeLists.txt CMakePresets.json include/stlab/concurrency/main_executor.hpp src/CMakeLists.txt src/stlab.def src/concurrency/detail/main_task_queue.hpp src/concurrency/main_executor_portable.cpp src/concurrency/main_executor_none.cpp test/CMakeLists.txt test/main_executor_test_host.hpp test/main_executor_order_test.cpp test/main_executor_concurrent_test.cpp test/main_executor_pre_exit_test.cpp
git commit -m "feat: add main executor C ABI with portable backend" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 2: libdispatch backend behind the ABI

**Files:**
- Create: `src/concurrency/main_executor_libdispatch.cpp`
- Modify: `include/stlab/concurrency/main_executor.hpp` (remove the inline libdispatch branch; widen ABI conditions)
- Modify: `src/CMakeLists.txt`, `CMakeLists.txt:207-208`, `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `detail::main_task_queue` (`push`, `pop`) from Task 1; the ABI declarations from Task 1.
- Produces: `stlab_v2_main_executor_submit` / `stlab_v2_main_executor_run` for `STLAB_MAIN_EXECUTOR=libdispatch`.

- [ ] **Step 1: Enable the contract tests for libdispatch**

In `test/CMakeLists.txt` change `set(stlab_main_executor_abi_backends portable)` to `set(stlab_main_executor_abi_backends portable libdispatch)`.

- [ ] **Step 2: Widen the header conditions**

In `main_executor.hpp`, replace every `#if STLAB_MAIN_EXECUTOR(PORTABLE)` that guards the ABI declarations, `main_executor_type`, and `main_executor_run()` with `#if STLAB_MAIN_EXECUTOR(PORTABLE) || STLAB_MAIN_EXECUTOR(LIBDISPATCH)`. Delete the `#elif STLAB_MAIN_EXECUTOR(LIBDISPATCH)` include branch (`<dispatch/dispatch.h>`) and the `#elif STLAB_MAIN_EXECUTOR(LIBDISPATCH)` `main_executor_type` definition.

- [ ] **Step 3: Confirm the tests fail without the backend (macOS only)**

On macOS: `cmake --preset=debug-cpp20 && cmake --build --preset=debug-cpp20 && ctest --preset=debug-cpp20 -R main_executor --output-on-failure`
Expected: the main-executor tests abort in the `none` stub (`No main executor is configured`), because `src/CMakeLists.txt` still compiles `main_executor_none.cpp` for libdispatch. If macOS is unavailable locally, skip to Step 4 and rely on CI (Task 5) for this backend.

- [ ] **Step 4: Write the backend**

Create `src/concurrency/main_executor_libdispatch.cpp`:

```cpp
/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include "detail/main_task_queue.hpp"

#include <stlab/concurrency/main_executor.hpp>
#include <stlab/config.hpp>

#include <dispatch/dispatch.h>

#include <cassert>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {
namespace {

/// Returns the process-shared main-executor task queue.
///
/// The queue is intentionally never destroyed so pending main-queue wakes never observe a
/// destroyed queue.
auto main_tasks() -> main_task_queue& {
    static auto& queue = *new main_task_queue; // NOLINT(cppcoreguidelines-owning-memory)
    return queue;
}

/// Runs the oldest queued task. Each wake is posted for exactly one pushed task.
void run_one(void* /*context*/) noexcept { main_tasks().pop()(); }

} // namespace
} // namespace detail
STLAB_VERSION_NAMESPACE_END()

inline namespace v2 {

/// Submits one task to the libdispatch main queue.
extern "C" void stlab_v2_main_executor_submit(const unsigned char* /*task_abi_guard*/,
                                              const stlab_v2_task_concept* vtable,
                                              stlab_v2_task_proc invoke,
                                              void* source) noexcept {
    assert(vtable != nullptr && invoke != nullptr && "Task vtable/invoke must not be null.");
    [[maybe_unused]] const bool pushed =
        STLAB_VERSION_NAMESPACE()::detail::main_tasks().push(vtable, invoke, source);
    assert(pushed && "libdispatch main queue is never closed.");
    dispatch_async_f(dispatch_get_main_queue(), nullptr,
                     &STLAB_VERSION_NAMESPACE()::detail::run_one);
}

/// Services the libdispatch main queue; never returns.
extern "C" [[noreturn]] void stlab_v2_main_executor_run() noexcept { dispatch_main(); }

} // namespace v2
} // namespace stlab
```

- [ ] **Step 5: Select the source and move the link dependency**

In `src/CMakeLists.txt` add a branch before `else()`:

```cmake
elseif(STLAB_MAIN_EXECUTOR STREQUAL "libdispatch")
  target_sources(stlab-core PRIVATE concurrency/main_executor_libdispatch.cpp)
```

In `CMakeLists.txt` replace

```cmake
if(STLAB_MAIN_EXECUTOR STREQUAL "libdispatch")
  target_link_libraries(stlab PUBLIC libdispatch::libdispatch)
```

with

```cmake
if(STLAB_MAIN_EXECUTOR STREQUAL "libdispatch")
  target_link_libraries(stlab-core PRIVATE libdispatch::libdispatch)
```

- [ ] **Step 6: Verify**

Windows regression: `cmake --build --preset=debug-cpp20 && ctest --preset=debug-cpp20` and `cmake --build --preset=debug-portable-main && ctest --preset=debug-portable-main` — Expected: all pass.
macOS (CI `macOS apple-clang latest`, `macOS TSan`, `macOS TSan (portable)` jobs, or locally if available): `ctest --preset=debug-cpp20 -R main_executor --output-on-failure` — Expected: `order` and `concurrent` pass.

- [ ] **Step 7: Commit**

```bash
git add include/stlab/concurrency/main_executor.hpp src/concurrency/main_executor_libdispatch.cpp src/CMakeLists.txt CMakeLists.txt test/CMakeLists.txt
git commit -m "feat: move libdispatch main executor behind the C ABI" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 3: Qt backend behind the ABI

**Files:**
- Create: `src/concurrency/main_executor_qt.cpp`
- Modify: `include/stlab/concurrency/main_executor.hpp` (remove Qt includes and `main_executor_type`; widen conditions)
- Modify: `src/CMakeLists.txt`, `CMakeLists.txt:209-212`, `test/CMakeLists.txt`

**Interfaces:**
- Consumes: ABI declarations from Task 1; `test/main_executor_test_host.hpp` (already constructs `QCoreApplication` for Qt).
- Produces: `stlab_v2_main_executor_submit` / `stlab_v2_main_executor_run` for `STLAB_MAIN_EXECUTOR=qt5|qt6`.

- [ ] **Step 1: Enable the contract tests for Qt**

In `test/CMakeLists.txt` set `set(stlab_main_executor_abi_backends portable libdispatch qt5 qt6)` and, inside the `foreach`, after `target_link_libraries(${target} PRIVATE stlab-core)`, add:

```cmake
    if(STLAB_MAIN_EXECUTOR STREQUAL "qt5")
      target_link_libraries(${target} PRIVATE Qt5::Core)
    elseif(STLAB_MAIN_EXECUTOR STREQUAL "qt6")
      target_link_libraries(${target} PRIVATE Qt6::Core)
    endif()
```

- [ ] **Step 2: Widen the header conditions and delete the inline Qt code**

In `main_executor.hpp`, the three guards become `#if STLAB_MAIN_EXECUTOR(PORTABLE) || STLAB_MAIN_EXECUTOR(LIBDISPATCH) || STLAB_MAIN_EXECUTOR(QT5) || STLAB_MAIN_EXECUTOR(QT6)`. Delete the Qt include branch (including the Qt version `#error` check, which moves to the `.cpp`) and the Qt `main_executor_type` class.

- [ ] **Step 3: Set up Qt in WSL and confirm the tests fail**

In WSL Ubuntu (no sudo):

```bash
python3 -m pip install --user --break-system-packages aqtinstall
python3 -m aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O ~/Qt
git -C /mnt/d/repos/github.com/stlab/stlab/.claude/worktrees/main-executor-abi status --short
cmake -S /mnt/d/repos/github.com/stlab/stlab/.claude/worktrees/main-executor-abi -B ~/stlab-build/qt6 -GNinja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=20 -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64 \
  -DCPM_SOURCE_CACHE=$HOME/.cache/cpm
cmake --build ~/stlab-build/qt6 && ctest --test-dir ~/stlab-build/qt6 -R main_executor --output-on-failure
```

Expected: configure reports `stlab: Main Executor: qt6`; the main-executor tests abort in the `none` stub (backend not yet written). If `aqtinstall` is unavailable, stop and ask the user to run `sudo apt-get install -y qt6-base-dev` in WSL, then configure without `CMAKE_PREFIX_PATH`.

- [ ] **Step 4: Write the backend**

Create `src/concurrency/main_executor_qt.cpp`:

```cpp
/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include <stlab/concurrency/main_executor.hpp>
#include <stlab/concurrency/task.hpp>
#include <stlab/config.hpp>

#include <QtGlobal>
#if (STLAB_MAIN_EXECUTOR(QT5) &&                                                                \
         (QT_VERSION < QT_VERSION_CHECK(5, 0, 0) || QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)) || \
     STLAB_MAIN_EXECUTOR(QT6) &&                                                                \
         (QT_VERSION < QT_VERSION_CHECK(6, 0, 0) || QT_VERSION >= QT_VERSION_CHECK(7, 0, 0)))
#error "Mismatching Qt versions"
#endif
#include <QCoreApplication>
#include <QEvent>
#include <QObject>

#include <cassert>
#include <cstdlib>
#include <memory>
#include <utility>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {
namespace {

using main_task_t = task<void() noexcept>;

/// Receives main-executor events on the application thread.
struct event_receiver : QObject {
    /// Runs the task carried by a main-executor event.
    ///
    /// - Postcondition: returns `true` iff `event` was a main-executor event.
    bool event(QEvent* event) override;
};

/// Posted Qt event that owns one main-executor task and the receiver that runs it.
class executor_event : public QEvent {
    main_task_t _task;
    std::unique_ptr<event_receiver> _receiver;

public:
    /// Constructs an event owning `task`, with a receiver living on the application thread.
    ///
    /// - Precondition: a `QCoreApplication` instance exists.
    explicit executor_event(main_task_t task) :
        QEvent(QEvent::User), _task(std::move(task)), _receiver(std::make_unique<event_receiver>()) {
        _receiver->moveToThread(QCoreApplication::instance()->thread());
    }

    /// Invokes the owned task.
    void execute() noexcept { _task(); }

    /// Returns the object the event must be posted to.
    [[nodiscard]] auto receiver() const -> QObject* { return _receiver.get(); }
};

bool event_receiver::event(QEvent* event) {
    auto* main_event = dynamic_cast<executor_event*>(event);
    if (!main_event) return false;
    main_event->execute();
    return true;
}

} // namespace
} // namespace detail
STLAB_VERSION_NAMESPACE_END()

inline namespace v2 {

/// Posts one task to the Qt application event loop.
extern "C" void stlab_v2_main_executor_submit(const unsigned char* /*task_abi_guard*/,
                                              const stlab_v2_task_concept* vtable,
                                              stlab_v2_task_proc invoke,
                                              void* source) noexcept {
    assert(vtable != nullptr && invoke != nullptr && "Task vtable/invoke must not be null.");
    assert(QCoreApplication::instance() && "main_executor requires a QCoreApplication.");
    using namespace STLAB_VERSION_NAMESPACE()::detail;
    auto event = std::make_unique<executor_event>(main_task_t{vtable, invoke, source});
    auto* receiver = event->receiver();
    QCoreApplication::postEvent(receiver, event.release());
}

/// Runs the Qt application event loop and exits the process with its result; never returns.
extern "C" [[noreturn]] void stlab_v2_main_executor_run() noexcept {
    assert(QCoreApplication::instance() && "main_executor_run() requires a QCoreApplication.");
    std::exit(QCoreApplication::exec());
}

} // namespace v2
} // namespace stlab
```

- [ ] **Step 5: Select the source and move the link dependency**

In `src/CMakeLists.txt` add before `else()`:

```cmake
elseif(STLAB_MAIN_EXECUTOR STREQUAL "qt5" OR STLAB_MAIN_EXECUTOR STREQUAL "qt6")
  target_sources(stlab-core PRIVATE concurrency/main_executor_qt.cpp)
```

In `CMakeLists.txt` change `target_link_libraries(stlab PUBLIC Qt5::Core)` to `target_link_libraries(stlab-core PRIVATE Qt5::Core)` and `target_link_libraries(stlab PUBLIC Qt6::Core)` to `target_link_libraries(stlab-core PRIVATE Qt6::Core)`.

- [ ] **Step 6: Verify**

WSL: `cmake --build ~/stlab-build/qt6 && ctest --test-dir ~/stlab-build/qt6 --output-on-failure`
Expected: all tests pass, including `stlab.test.main_executor_order` and `stlab.test.main_executor_concurrent`.
Windows regression: `debug-cpp20` and `debug-portable-main` builds and tests pass.

- [ ] **Step 7: Commit**

```bash
git add include/stlab/concurrency/main_executor.hpp src/concurrency/main_executor_qt.cpp src/CMakeLists.txt CMakeLists.txt test/CMakeLists.txt
git commit -m "feat: move Qt main executor behind the C ABI" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 4: Emscripten backend behind the ABI; header final form

**Files:**
- Create: `src/concurrency/main_executor_emscripten.cpp`
- Modify: `include/stlab/concurrency/main_executor.hpp` (remove the last inline backend; all guards become `#if !STLAB_MAIN_EXECUTOR(NONE)`; delete the documentation-only `NONE` struct)
- Modify: `src/CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `detail::main_task_queue` (`push`, `pop`) from Task 1; ABI declarations from Task 1.
- Produces: `stlab_v2_main_executor_submit` / `stlab_v2_main_executor_run` for `STLAB_MAIN_EXECUTOR=emscripten`. Final header with no backend code.

- [ ] **Step 1: Enable the tests for every non-`none` backend**

In `test/CMakeLists.txt` replace the `set(stlab_main_executor_abi_backends ...)` line and the `if(STLAB_MAIN_EXECUTOR IN_LIST stlab_main_executor_abi_backends)` line with:

```cmake
if(NOT STLAB_MAIN_EXECUTOR STREQUAL "none")
```

- [ ] **Step 2: Final header**

In `main_executor.hpp`:
- All ABI/wrapper/`main_executor_run()` guards become `#if !STLAB_MAIN_EXECUTOR(NONE)`.
- The include block becomes:

```cpp
#include <stlab/config.hpp>

#if !STLAB_MAIN_EXECUTOR(NONE)
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/task.hpp>

#include <type_traits>
#include <utility>
#endif
```

- Delete the Emscripten `main_executor_type` (and its comment block) and the `#elif STLAB_MAIN_EXECUTOR(NONE)` documentation-only struct. The `detail` namespace then contains only the ABI-based `main_executor_type` under `#if !STLAB_MAIN_EXECUTOR(NONE)`, and `main_executor` is declared under the same guard.
- Confirm with `rg "STLAB_MAIN_EXECUTOR\(" include/stlab/concurrency/main_executor.hpp` that only `STLAB_MAIN_EXECUTOR(NONE)` remains.

- [ ] **Step 3: Set up Emscripten in WSL and confirm the tests fail**

```bash
git clone --depth 1 https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install latest && ~/emsdk/emsdk activate latest
source ~/emsdk/emsdk_env.sh
cmake -S /mnt/d/repos/github.com/stlab/stlab/.claude/worktrees/main-executor-abi -B ~/stlab-build/wasm -GNinja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=23 -DCPM_SOURCE_CACHE=$HOME/.cache/cpm \
  -DCMAKE_TOOLCHAIN_FILE=/mnt/d/repos/github.com/stlab/stlab/.claude/worktrees/main-executor-abi/cmake/Platform/Emscripten-STLab.cmake
cmake --build ~/stlab-build/wasm && ctest --test-dir ~/stlab-build/wasm -R main_executor --output-on-failure
```

Expected: configure reports `stlab: Main Executor: emscripten`; the tests abort in the `none` stub. If the toolchain file rejects emsdk's bundled node as too old, set `NODE_JS` in `~/emsdk/.emscripten` to a node ≥ 16.16 (emsdk `latest` bundles a newer node, so this is not expected).

- [ ] **Step 4: Write the backend**

Create `src/concurrency/main_executor_emscripten.cpp`:

```cpp
/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include "detail/main_task_queue.hpp"

#include <stlab/concurrency/main_executor.hpp>
#include <stlab/config.hpp>

#include <emscripten.h>
#include <emscripten/threading.h>

#include <cassert>
#include <cstdlib>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {
namespace {

/// Returns the process-shared main-executor task queue.
///
/// The queue is intentionally never destroyed so pending main-thread wakes never observe a
/// destroyed queue.
auto main_tasks() -> main_task_queue& {
    static auto& queue = *new main_task_queue; // NOLINT(cppcoreguidelines-owning-memory)
    return queue;
}

/// Runs the oldest queued task. Each wake is posted for exactly one pushed task.
void run_one(void* /*context*/) noexcept { main_tasks().pop()(); }

/// Defers `run_one` to the main runtime thread's event loop.
///
/// `emscripten_async_run_in_main_runtime_thread()` may run its function at any POSIX thread
/// cancellation point while wasm is executing on the main thread, which can re-enter code holding
/// locks. Bouncing through `emscripten_async_call()` runs the task from the main run loop instead.
void bounce(void* context) noexcept { emscripten_async_call(&run_one, context, 0); }

} // namespace
} // namespace detail
STLAB_VERSION_NAMESPACE_END()

inline namespace v2 {

/// Submits one task to the Emscripten main runtime thread.
extern "C" void stlab_v2_main_executor_submit(const unsigned char* /*task_abi_guard*/,
                                              const stlab_v2_task_concept* vtable,
                                              stlab_v2_task_proc invoke,
                                              void* source) noexcept {
    assert(vtable != nullptr && invoke != nullptr && "Task vtable/invoke must not be null.");
    [[maybe_unused]] const bool pushed =
        STLAB_VERSION_NAMESPACE()::detail::main_tasks().push(vtable, invoke, source);
    assert(pushed && "Emscripten main queue is never closed.");
    emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_VI,
                                                &STLAB_VERSION_NAMESPACE()::detail::bounce,
                                                nullptr);
}

/// Ends the calling thread while keeping the runtime alive to service the main queue; never
/// returns.
extern "C" [[noreturn]] void stlab_v2_main_executor_run() noexcept {
    emscripten_exit_with_live_runtime();
    std::abort(); // Unreachable; satisfies [[noreturn]] if the declaration lacks it.
}

} // namespace v2
} // namespace stlab
```

- [ ] **Step 5: Select the source**

In `src/CMakeLists.txt` add before `else()`:

```cmake
elseif(STLAB_MAIN_EXECUTOR STREQUAL "emscripten")
  target_sources(stlab-core PRIVATE concurrency/main_executor_emscripten.cpp)
```

- [ ] **Step 6: Verify, including the `noexcept` unwind risk**

WSL: `source ~/emsdk/emsdk_env.sh && cmake --build ~/stlab-build/wasm && ctest --test-dir ~/stlab-build/wasm --output-on-failure`
Expected: all tests pass, including `stlab.test.main_executor_order` and `stlab.test.main_executor_concurrent`.

Risk check: with `-fwasm-exceptions`, `emscripten_exit_with_live_runtime()` unwinds via a JS exception. If the tests fail with `terminate`/`abort` coming from `stlab_v2_main_executor_run`, remove `noexcept` from `stlab_v2_main_executor_run` in the header declaration and in all five backend definitions (`portable`, `libdispatch`, `qt`, `emscripten`, `none`) and from `stlab::main_executor_run()`, rerun, and record the deviation in the spec's Design §2 and the handoff doc.

Windows regression: `debug-cpp20` (13/13) and `debug-portable-main` pass.

- [ ] **Step 7: Commit**

```bash
git add include/stlab/concurrency/main_executor.hpp src/concurrency/main_executor_emscripten.cpp src/CMakeLists.txt test/CMakeLists.txt
git commit -m "feat: move Emscripten main executor behind the C ABI" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 5: CI coverage

**Files:**
- Modify: `.github/matrix.json`, `.github/workflows/stlab.yml`

**Interfaces:**
- Consumes: the `STLAB_MAIN_EXECUTOR=portable` option (Task 1) and the tests registered for every non-`none` backend (Task 4).

- [ ] **Step 1: Honor `cmake_options` on Unix and install Qt when requested**

In `.github/workflows/stlab.yml`, in `Configure // Unix !Emscripten`, change the cmake line to:

```yaml
          cmake -S. -B../build -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20 ${{ matrix.config.cmake_options }}
```

Add before `Set enviroment variables // Linux GCC`:

```yaml
      - name: Install dependencies // Linux Qt6
        if: ${{ matrix.config.qt == 'qt6' }}
        shell: bash
        run: |
          sudo apt-get update
          sudo apt-get install -y qt6-base-dev
```

- [ ] **Step 2: Add matrix entries**

Append to `.github/matrix.json` `config`:

```json
    {
      "name": "Linux GCC portable main executor",
      "compiler": "gcc",
      "os": "ubuntu-latest",
      "cmake_options": "-DSTLAB_MAIN_EXECUTOR=portable"
    },
    {
      "name": "Linux GCC Qt6 main executor",
      "compiler": "gcc",
      "os": "ubuntu-latest",
      "qt": "qt6",
      "cmake_options": "-DSTLAB_MAIN_EXECUTOR=qt6"
    },
    {
      "name": "Windows shared portable main executor",
      "compiler": "Visual Studio",
      "os": "windows-latest",
      "cmake_options": "-DSTLAB_CORE_SHARED=ON -DSTLAB_MAIN_EXECUTOR=portable"
    }
```

Check `scripts/flatten_json.py` passes unknown keys (`qt`) through; if it drops them, key the Qt install step on `contains(matrix.config.cmake_options, 'qt6')` instead of `matrix.config.qt`.

- [ ] **Step 3: Validate locally**

Run: `python scripts/flatten_json.py < .github/matrix.json`
Expected: valid single-line JSON including the three new entries (with `qt` preserved).

- [ ] **Step 4: Commit**

```bash
git add .github/matrix.json .github/workflows/stlab.yml
git commit -m "ci: cover portable, Qt6, and shared portable main executors" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 6: Documentation, lint, and handoff

**Files:**
- Modify: `README.md:85`, `CLAUDE.md` (Platform/Scheduler Configuration and Architecture bullets), `.github/copilot-instructions.md` if tracked by git (`git ls-files .github/copilot-instructions.md`)
- Modify: `docs/superpowers/specs/2026-09-28-main-executor-abi-design.md` (resolve Open questions)
- Create: `docs/superpowers/2026-09-28-main-executor-abi-handoff.md`

- [ ] **Step 1: Update option docs**

`README.md` line 85 becomes:

```markdown
- `-DSTLAB_MAIN_EXECUTOR=`[`qt5`, `qt6`, `libdispatch`, `emscripten`, `portable`, `none`] to select the main executor to use. Default is platform dependent; `portable` (an stlab-owned queue serviced by `stlab::main_executor_run()`) is opt-in. Windows has no process main queue, so the default there is `none` unless Qt is found.
```

In `CLAUDE.md`, change the `STLAB_MAIN_EXECUTOR` bullet to `` `STLAB_MAIN_EXECUTOR` — `libdispatch`, `qt5`, `qt6`, `emscripten`, `portable`, `none` `` and the `main_executor.hpp` architecture bullet to: `` **`main_executor.hpp`** — Executor for the application's main queue, behind the `stlab_v2_main_executor_*` C ABI; `main_executor_run()` services it and never returns. ``; add `debug-portable-main` to the presets table (`Portable main executor`). Apply the same edits to `.github/copilot-instructions.md` only if it is tracked.

- [ ] **Step 2: Resolve the spec's open questions**

Replace the spec's `## Open questions` section with `## Resolved questions` recording: Qt `run()` is `std::exit(QCoreApplication::exec())` with the precondition that a `QCoreApplication` exists and the application calls `pre_exit()` before quitting the loop; Emscripten `run()` is `emscripten_exit_with_live_runtime()`, verified under `-sPROXY_TO_PTHREAD` and `-fwasm-exceptions` in Task 4 (note any `noexcept` deviation). Add a line under Design §4 that the portable pre-exit handler is registered on first use of the main executor, so a first submission after `pre_exit()` is a precondition violation (asserted by `at_pre_exit`), matching the default executor.

- [ ] **Step 3: Lint**

Run (Windows dev environment): `cmake --preset=clang-tidy-win64 -DSTLAB_MAIN_EXECUTOR=portable && cmake --build --preset=clang-tidy-win64`
Expected: zero warnings. Fix any in the changed files.

Run `clang-format --dry-run --Werror` over every new/modified `.hpp`/`.cpp`. Expected: no output.

- [ ] **Step 4: Docs build**

Run: `cmake --preset=docs && cmake --build --preset=docs` (skip if Doxygen is not installed; CI `Doxygen API docs` job covers it).
Expected: no new warnings; `stlab_v2_main_executor_submit`, `stlab_v2_main_executor_run`, `main_executor`, and `main_executor_run` appear in the generated reference.

- [ ] **Step 5: Final regression sweep (Windows)**

Run: `debug-cpp20`, `debug-cpp17`, `debug-asan`, `debug-portable-main`, and `build/portable-main-shared` builds with their tests.
Expected: all pass.

- [ ] **Step 6: Write the handoff doc**

Create `docs/superpowers/2026-09-28-main-executor-abi-handoff.md` with sections: **Completed** (per task, with commit hashes), **Verification** (which backends were verified locally vs. CI only), **Deliberate deferrals** (Windows targeted executor such as `window_executor(HWND)` / `Windows.System.DispatcherQueue`; secondary UI thread executors; runtime-installable backends; `run()` stop/drain APIs), and **Remaining tasks** (anything left from verification, e.g. CI-only confirmation of libdispatch). For each deferral, create a GitHub issue with `gh issue create` only if the user approves, and reference it.

- [ ] **Step 7: Commit**

```bash
git add README.md CLAUDE.md docs/superpowers/specs/2026-09-28-main-executor-abi-design.md docs/superpowers/2026-09-28-main-executor-abi-handoff.md
git commit -m "docs: document main executor ABI backends and handoff" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```
