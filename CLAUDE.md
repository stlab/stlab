# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

STLab is a C++ library (Boost Software License 1.0) providing futures, channels, await helpers, serial queues, and general utilities in the `stlab` namespace. Its public dependency `stlab-execution` provides tasks, executors, timers, thread naming, and `pre_exit` under their existing include paths and source-level names. The library supports C++17/20/23 and targets Linux (GCC, Clang), macOS (Apple Clang), Windows (MSVC), and WebAssembly (Emscripten).

STLab-owned public headers live in `include/stlab/`; `src/stlab.cpp` is its only compiled source. Execution-owned headers and runtime implementations live in the `stlab-execution` repository, not this source tree. Everything else is tests, documentation, or build infrastructure.

## Build Commands

All builds use **CMake + Ninja** via presets defined in `CMakePresets.json`. The build directory is always `build/<preset-name>/`.

```bash
# Standard debug build (C++20)
cmake --preset=debug-cpp20
cmake --build --preset=debug-cpp20

# Run all tests
ctest --preset=debug-cpp20

# Run a single test executable
./build/debug-cpp20/test/stlab.test.future

# Run with a doctest filter
./build/debug-cpp20/test/stlab.test.future -tc="future_test_*"
```

Key presets:

| Preset | Purpose |
|--------|---------|
| `debug-cpp20` | Standard debug build (default for development) |
| `debug-cpp17` | C++17 compatibility build |
| `debug-sanitizer` | TSan + UBSan |
| `debug-asan` | Address sanitizer |
| `debug-portable` | Force portable task system (no platform scheduler) |
| `debug-portable-main` | Portable main executor |
| `debug-clang-libcxx` | Clang + libc++ on Linux |
| `clang-tidy-win64` | Static analysis on Windows (use from VS Developer Prompt) |
| `docs` | Doxygen API reference |
| `install` | Release build for installation |

## Testing

The test framework is **doctest** v2.5.3. Test executables are named `stlab.test.<component>`:

- `stlab.test.future` — futures/promises (includes coroutine tests on C++20)
- `stlab.test.channel` — channels and pipelines
- `stlab.test.executor` — executor implementations
- `stlab.test.serial_queue` — serial queues
- `stlab.test.forest` — forest/tree container
- `stlab.test.cow`, `stlab.test.task`, `stlab.test.traits`, `stlab.test.utility`, `stlab.test.tuple`, `stlab.test.tuple_algorithm`, `stlab.test.system_timer`

Run a subset of tests with CTest's `-R` flag:
```bash
ctest --preset=debug-cpp20 -R future
```

## Documentation

Doxygen comments in `include/stlab/**/*.hpp` are the authoritative API docs. The main page and directory-level group definitions live in `docs/doxygen/mainpage.dox`. Use `.dox` files for documentation-only content, not headers that Doxygen presents as includable files.

```bash
# Build API reference locally → build/docs/html/
cmake --preset=docs
cmake --build --preset=docs
```

For the full Jekyll + Doxygen site (requires Ruby, Bundler, CMake, Ninja, Doxygen):
```bash
./docs/tools/docs/build-site.sh
```

## Platform/Scheduler Configuration

The `stlab-execution` dependency auto-detects the platform's threading and task systems. Source builds through STLab accept the following CMake variables; backend selection is owned by execution, while STLab owns coroutine configuration:

- `STLAB_THREAD_SYSTEM` — `win32`, `pthread`, `pthread-apple`, `none`
- `STLAB_TASK_SYSTEM` — `libdispatch` (Apple GCD), `portable`, `windows`
- `STLAB_MAIN_EXECUTOR` — `libdispatch`, `qt5`, `qt6`, `emscripten`, `portable`, `none`
- `STLAB_NO_STD_COROUTINES=ON` — suppress C++20 coroutines for non-conforming compilers
- `STLAB_EMSCRIPTEN_PTHREADS=OFF` — disable Emscripten pthread compiler/linker flags for targeted non-pthread WebAssembly builds

The `portable` task system is the cross-platform fallback that works on all platforms including Emscripten.

Use `BUILD_SHARED_LIBS` to select static or shared source-built libraries. Installed execution
targets keep their linkage independently of a client's setting; no execution-specific linkage
option is required.

## Code Style

Formatting is enforced by `.clang-format`:
- 100-column limit
- 4-space indentation, no tabs
- Left-aligned pointer declarators
- Sorted includes and using declarations

Linting via `.clang-tidy` checks `cert-*`, `performance-*`, `modernize-*`, and `misc-include-cleaner` against headers in `include/stlab/**/*.hpp`. `modernize-use-trailing-return-type` is disabled.

## Git and Worktree Workflow

- Perform changes in an isolated git worktree; do not commit directly to `main`.
- Before opening a PR, run the relevant build, test, sanitizer, and lint checks documented
  above. Read the output and resolve warnings, not only errors.
- For multi-phase work, maintain a dated handoff document under `docs/superpowers/` describing
  completed work, deliberate deferrals, and remaining tasks.

## Library-First Design

STLab is a reusable library. Implement public behavior for the general problem described by
the interface and contract, not only for the current test or one consuming application. If a
general solution must be deferred, document the boundary explicitly and track the remaining
work rather than silently special-casing the current use case.

## Conflicting Goals

When two stated goals or constraints cannot both be satisfied by any known design, do not
silently pick one and drop the other. Stop and report the conflict explicitly: name both goals,
explain why they are in tension for this specific change, and let the human partner decide which
to relax (or ask for a design that avoids the trade-off entirely). Do not present a design that
sacrifices a goal as if it fully satisfies all goals.

## Allocation and Ownership

Avoid unnecessary heap allocation and ownership transfer in performance-sensitive paths. Prefer
references, views, iterators, existing small-buffer-optimized types, or static polymorphism when
they express the contract clearly. In particular, preserve the allocation-free and ABI-boundary
requirements of the Windows DLL-safe executor design.

The task pool, timers, main executor, `pre_exit`, and their ABI implementation are owned by
`stlab-execution`; changes to those implementations belong in that repository. STLab consumes
the public `stlab::execution` target, directly or through `stlab::stlab`.

All communication with execution's process-shared library state must cross a versioned C ABI; shared-library
clients must not reference internal C++ implementation symbols. CI must cover each supported
Windows shared-execution task-system configuration, including the native and portable task systems.
Except for `system_timer.hpp`, public concurrency headers must not branch on `STLAB_TASK_SYSTEM`,
`STLAB_CORE_SHARED`, or `_WIN32`; those choices belong in compiled implementation files behind the
versioned C ABI.

## Architecture

STLab retains these concurrency APIs in `include/stlab/concurrency/`:

- **`future.hpp`** — `stlab::future<T>` and `stlab::package()`. Futures are lazy/value-semantic, not `std::future`. Supports `.then()`, `.recover()`, `.detach()`, and C++20 coroutines (`co_await`).
- **`channel.hpp`** — `stlab::sender<T>` / `stlab::receiver<T>` for reactive pipelines. Multiple process stages can be composed.
- **`serial_queue.hpp`** — A serial dispatch queue built on executors.
- **`await.hpp`** — Await helpers built on execution's scheduling primitives.

The public `stlab-execution` dependency supplies the following APIs under their existing
`stlab/concurrency/` include paths. Their contracts and implementations are maintained in
that repository, not here:

- **`executor_base.hpp`** / **`default_executor.hpp`** — Executor abstractions and platform task dispatch.
- **`main_executor.hpp`** — Executor for the application's main queue, behind the `stlab_v2_main_executor_*` C ABI; `main_executor_run()` services it and never returns.
- **`task.hpp`** — `stlab::task<Sig>` — a move-only type-erased callable (like `std::function` but non-copyable).
- **`system_timer.hpp`** — Timer-based future scheduling.
- **`immediate_executor.hpp`** / **`set_current_thread_name.hpp`** — Immediate execution and thread naming.

Execution also owns **`stlab/pre_exit.hpp`**, which registers process cleanup and shuts down
the runtime. Link `stlab::stlab` for both layers or `stlab::execution` for execution alone;
the legacy `stlab::stlab-core` target is an INTERFACE compatibility target, not another runtime.

STLab-owned non-concurrency headers:
- **`forest.hpp`** / **`forest_algorithms.hpp`** — A node-based tree container with cursor-based traversal.
- **`copy_on_write.hpp`** (via `stlab-copy-on-write` dependency) — Value-semantic CoW wrapper.

## Development Process

### Function Contracts

Every class, struct, and function declaration must have a documentation comment written in
contract style, using `///` syntax. The contract lives adjacent to the declaration so it stays
synchronized with the code.

**Required sections** (include only those that apply):

1. **Summary** — A sentence fragment describing what the function does or returns. Concise; omit needless words.
2. **Preconditions** — `/// - Precondition: <condition>` — document only when not obviously implied by the summary.
3. **Postconditions** — `/// - Postcondition: <condition>` — document only when not implicit in the summary.
4. **Complexity** — `/// - Complexity: <description>` — required whenever the operation is not O(1). Use prose, e.g. `at most N log N comparisons`.

**Example:**
```cpp
/// Removes and returns the last element.
///
/// - Precondition: the container is non-empty.
/// - Complexity: O(1).
T pop_back();
```

Project-wide policy: assume O(1) time and space unless a `Complexity:` note says otherwise.

If you cannot write a simple contract for a function, treat that as a signal that the design needs improvement.

Do not recover from violated internal invariants with alternate behavior; assert the invariant and
stop rather than silently changing execution semantics. In particular, executor scheduling must
not execute queued work inline as a fallback.

Prefer reasoning from established one-to-one accounting invariants over maintaining redundant
state. When every submitted operation has exactly one completion token, preserve that token until
the operation succeeds rather than adding counters to rediscover whether work remains.

Preserve placement and routing information returned by sharded data structures. Notify the worker
responsible for the selected shard first; allow active workers to steal work rather than discarding
the locality hint and scanning for an arbitrary consumer.

Use `# Examples` for public APIs where an example materially clarifies usage. For unsafe
operations, document the caller invariants under `# Safety`; distinguish runtime errors from
violated preconditions.

### Unit Tests

When writing unit tests, derive them from the **contract and public interface only** — do not read or consider the implementation. The test suite should verify observable behavior as specified by the contract:

- Each precondition violation (if testable) should have a corresponding test.
- Each postcondition should be asserted.
- Edge cases implied by the summary (empty input, single element, etc.) should be covered.
- Complexity guarantees are not typically tested, but correctness under the specified conditions is.

Tests written against the implementation risk encoding bugs rather than verifying intent.

## CI

CI runs via GitHub Actions (`.github/workflows/stlab.yml`). The build matrix is defined in `.github/matrix.json` and flattened by `scripts/flatten_json.py`. CI tests: Linux GCC, Linux Clang, macOS Apple Clang, Windows MSVC, and Emscripten/WASM. macOS additionally runs with TSan+UBSan on both the native and portable task systems.

CPM package downloads are cached in `.cache/cpm/` (gitignored).

## Code Review Findings

Address review findings in the same pass when the fix is small and in scope. If a finding
requires a substantial design change or is out of scope, create a GitHub issue with
`gh issue create` and reference it rather than silently deferring the work.
