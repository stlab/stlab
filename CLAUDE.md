# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

STLab is a C++ library (Boost Software License 1.0) providing concurrency primitives — futures, channels, executors, and serial queues — in the `stlab` namespace. The library supports C++17/20/23 and targets Linux (GCC, Clang), macOS (Apple Clang), Windows (MSVC), and WebAssembly (Emscripten).

Public headers live in `include/stlab/`. There are two `.cpp` implementation files in `src/`. Everything else is tests, documentation, or build infrastructure.

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
| `debug-clang-libcxx` | Clang + libc++ on Linux |
| `clang-tidy-win64` | Static analysis on Windows (use from VS Developer Prompt) |
| `docs` | Doxygen API reference |
| `install` | Release build for installation |

## Testing

The test framework is **doctest** v2.5.2. Test executables are named `stlab.test.<component>`:

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

Doxygen comments in `include/stlab/**/*.hpp` are the authoritative API docs. Group definitions live in `docs/doxygen/stlab_groups.hpp` (not compiled). The main page is `docs/doxygen/mainpage.dox`.

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

The library auto-detects the platform's threading and task systems. You can override with CMake variables:

- `STLAB_THREAD_SYSTEM` — `win32`, `pthread`, `pthread-apple`, `none`
- `STLAB_TASK_SYSTEM` — `libdispatch` (Apple GCD), `portable`, `windows`
- `STLAB_MAIN_EXECUTOR` — `libdispatch`, `qt5`, `qt6`, `emscripten`, `none`
- `STLAB_NO_STD_COROUTINES=ON` — suppress C++20 coroutines for non-conforming compilers

The `portable` task system is the cross-platform fallback that works on all platforms including Emscripten.

## Code Style

Formatting is enforced by `.clang-format`:
- 100-column limit
- 4-space indentation, no tabs
- Left-aligned pointer declarators
- Sorted includes and using declarations

Linting via `.clang-tidy` checks `cert-*`, `performance-*`, `modernize-*`, and `misc-include-cleaner` against headers in `include/stlab/**/*.hpp`. `modernize-use-trailing-return-type` is disabled.

## Architecture

The concurrency subsystem (`include/stlab/concurrency/`) is the core of the library:

- **`future.hpp`** — `stlab::future<T>` and `stlab::package()`. Futures are lazy/value-semantic, not `std::future`. Supports `.then()`, `.recover()`, `.detach()`, and C++20 coroutines (`co_await`).
- **`channel.hpp`** — `stlab::sender<T>` / `stlab::receiver<T>` for reactive pipelines. Multiple process stages can be composed.
- **`executor_base.hpp`** / **`default_executor.hpp`** — Executors are `stlab::executor_t` (a type-erased callable). The default executor dispatches to the platform task pool.
- **`serial_queue.hpp`** — A serial dispatch queue built on executors.
- **`main_executor.hpp`** — Executor that runs on the application main thread (platform-specific).
- **`task.hpp`** — `stlab::task<Sig>` — a move-only type-erased callable (like `std::function` but non-copyable).
- **`system_timer.hpp`** — Timer-based future scheduling.

Non-concurrency headers:
- **`forest.hpp`** / **`forest_algorithms.hpp`** — A node-based tree container with cursor-based traversal.
- **`copy_on_write.hpp`** (via `stlab-copy-on-write` dependency) — Value-semantic CoW wrapper.
- **`pre_exit.hpp`** — Register cleanup functions to run before `std::exit()`. Must be called before exiting to avoid races with the task pool.

## Development Process

### Function Contracts

Every function declaration must have a documentation comment written in contract style, using `///` syntax. The contract lives adjacent to the declaration so it stays synchronized with the code.

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
