# Main executor ABI handoff

## Completed

- Task 1 — portable main executor ABI and contract tests:
  - `36979d6 feat: add main executor C ABI with portable backend`
  - `32af53e fix: address main executor task 1 review`
  - Added `STLAB_MAIN_EXECUTOR=portable`, `stlab_v2_main_executor_submit`, `stlab_v2_main_executor_run`, `stlab::main_executor_run()`, portable FIFO backend, `none` stubs, Windows `.def` exports, and contract tests.
  - Review fix changed portable pre-exit handling to eager static registration, so `pre_exit()` before first submission is supported and later submissions are destroyed without invocation.
- Task 2 — libdispatch backend behind the ABI:
  - `bdf2029 feat: move libdispatch main executor behind the C ABI`
  - `9004f7a fix: export libdispatch dependency for stlab-core`
  - Moved libdispatch implementation out of the public header and into `stlab-core`. (The package-config patch added in `9004f7a` was later removed; see "Follow-up".)
- Task 3 — Qt backend behind the ABI:
  - `3b3ead0 feat: move Qt main executor behind the C ABI`
  - Moved Qt5/Qt6 main executor implementation into `src/concurrency/main_executor_qt.cpp`; `run()` is `std::exit(QCoreApplication::exec())` with a `QCoreApplication` precondition.
- Task 4 — Emscripten backend and final public header shape:
  - `26e73bf feat: move Emscripten main executor behind the C ABI`
  - `a4d65fe fix: address main executor task 4 review`
  - `d4ef44a docs: note EXIT_RUNTIME requirement for Emscripten shutdown`
  - Removed backend implementation details from `main_executor.hpp`, added the Emscripten backend, removed `noexcept` from `run()`, documented the two-stage `pre_exit()` + `emscripten_force_exit(status)` shutdown rule, and added a non-pthread Emscripten path.
- Task 5 — CI coverage:
  - `60b8aae ci: cover portable, Qt6, shared portable, and non-pthread Emscripten main executors`
  - Added matrix coverage for Linux portable main, Linux Qt6 main, Windows shared portable main, and Linux WebAssembly non-pthread main executor. Added `STLAB_EMSCRIPTEN_PTHREADS` to the Emscripten toolchain file.
- Task 6 — documentation, lint, and final handoff:
  - `f0d967b docs: document main executor ABI backends and handoff`
  - Updated README, CLAUDE.md, and the design spec to reflect `portable`, eager portable pre-exit registration, Qt/Emscripten resolved questions, `STLAB_EMSCRIPTEN_PTHREADS`, and the ABI-backed main executor architecture.
- Follow-up — dependency linkage and helper cleanup:
  - Main-executor dependencies (libdispatch, Qt5/Qt6 Core) are linked `PUBLIC` on both `stlab` and `stlab-core`, matching the existing `Threads` and task-system libdispatch pattern and `main`'s `stlab` linkage. The installed package config is again generated solely by cpp-library dependency discovery; the custom `stlabConfig.cmake` patch and the `Findlibdispatch.cmake` install were removed.
  - The shared `main_tasks()` accessor moved into `src/concurrency/detail/main_task_queue.hpp`.

## Verification

| Area | Local verification | Result | CI-only / remaining coverage |
|---|---|---|---|
| Windows default C++20 | `cmake --preset=debug-cpp20 && cmake --build --preset=debug-cpp20 && ctest --preset=debug-cpp20 --output-on-failure` | Passed: 13/13 | n/a |
| Windows C++17 | `cmake --preset=debug-cpp17 && cmake --build --preset=debug-cpp17 && ctest --test-dir build\\debug-cpp17 --output-on-failure` | Passed: 13/13 | n/a |
| Windows ASan | `cmake --preset=debug-asan && cmake --build --preset=debug-asan && ctest --preset=debug-asan --output-on-failure` | Passed: 13/13 | n/a |
| Windows portable main | `cmake --preset=debug-portable-main && cmake --build --preset=debug-portable-main && ctest --preset=debug-portable-main --output-on-failure` | Passed: 17/17 | n/a |
| Windows shared portable main | `cmake --build build\\portable-main-shared && ctest --test-dir build\\portable-main-shared --output-on-failure` | Passed: 18/18 | n/a |
| clang-tidy | VS dev env `cmake --preset=clang-tidy-win64 -DSTLAB_MAIN_EXECUTOR=portable && cmake --build --preset=clang-tidy-win64` | Passed after lint fixes; known harmless `vswhere.exe` message | n/a |
| clang-format | `clang-format --dry-run --Werror` over changed `.hpp`/`.cpp` files vs `a1d471d` | Passed | n/a |
| Doxygen | `cmake --preset=docs && cmake --build --preset=docs`; generated HTML contains `stlab_v2_main_executor_submit`, `stlab_v2_main_executor_run`, `main_executor`, and `main_executor_run` | Passed; Doxygen found, `dot` missing but non-fatal | n/a |
| Emscripten pthread | Task 4/5 WSL builds and tests | Passed: full pthread suite 15/15 | CI continues to cover WebAssembly |
| Emscripten non-pthread | Task 5 WSL build of `stlab-core`, order, concurrent; runtime order test | Passed: order 1/1; concurrent build-only by design | CI non-pthread job runs order test |
| Qt6 | Task 3/4 WSL Qt6 build/test | Passed: 15/15 | Linux Qt6 apt-based CI job remains authoritative |
| libdispatch | Task 2 syntax/package checks with mocked libdispatch | Best-effort only locally | macOS CI must verify real libdispatch runtime behavior |

## Deliberate deferrals

- Windows targeted executor such as `window_executor(HWND)` or `Windows.System.DispatcherQueue`.
  - Proposed issue: design a targeted Windows UI-thread executor; do not model Windows as having a single process main queue.
- Secondary UI thread executors for platforms/app frameworks that support more than one UI/event thread.
  - Proposed issue: define targeted executor APIs for additional UI/event loops without changing `main_executor` semantics.
- Runtime-installable main executor backends.
  - Proposed issue: evaluate whether applications need to install/replace a main-queue adapter at runtime and how that interacts with the C ABI.
- `run()` stop/drain APIs.
  - Proposed issue: design explicit stop/drain behavior separately from the current never-returning `run()` contract.

No GitHub issues were created; issue creation requires explicit user approval.

## Remaining tasks

- CI-only verification of real libdispatch behavior on macOS.
- Pending CI confirmation of the apt-installed qt6-base-dev workflow job (backend already verified locally in WSL).
