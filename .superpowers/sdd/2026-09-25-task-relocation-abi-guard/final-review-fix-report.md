# Final Review Fix Report — Task Relocation ABI Guard

Date: 2026-09-25
Worktree: `windows-dll-executor-abi`
Starting HEAD: `2ba18d2f0c83ef59af49ad1fc25fe56484605aa3`
Fix commit: `5439b224064d435cd016279e0259466e25244f9e` (`Fix portable executor early wake race`)

## Summary

Addressed the portable executor lost-wake race and the three requested declaration-documentation findings. The regression is covered by a deterministic unit test of the actual waiter state transition helper; it uses no sleep, polling loop, or public API addition.

## RED evidence (before the state-machine fix)

The test `portable waiter retains a wake requested before waiting` was first run against the old transition semantics:

```text
build\pr-debug-cpp20\test\stlab.test.executor.exe -tc="portable waiter retains a wake requested before waiting"

TEST CASE: portable waiter retains a wake requested before waiting
FATAL ERROR: REQUIRE_FALSE( state.begin_wait() ) is NOT correct!
  values: REQUIRE_FALSE( true )
[doctest] test cases: 1 | 0 passed | 1 failed | 6 skipped
[doctest] assertions: 2 | 1 passed | 1 failed
```

This reproduces the specific state-machine defect: `wake()` returned false before waiting, but the subsequent wait transition proceeded to block rather than consuming a retained wake.

## Root cause and fix

The portable worker drains via `try_pop(index)` and then `pop(index)`, and only afterward enters `_waiters[index].wait()`. A submission in the gap enqueues work and calls `wake()`. Previously, `wake()` returned false when `_waiting` was false and discarded the request; `wait()` then set `_waiting` and slept with work already queued.

Added internal `stlab::detail::waiter_state` in `src/concurrency/detail/waiter_state.hpp`. Under the existing waiter mutex, a wake while not waiting records one pending wake and still returns false. The next `begin_wait()` consumes that token and returns false, causing the worker loop to retry its existing queue checks. A wake while waiting clears the waiting state, signals the condition variable, and returns true as before. This preserves pool-expansion semantics, shard hints/locality, and non-inline execution. Repeated early wake requests coalesce because the queue remains the source of work accounting.

The direct state test verifies both contracts: an early wake returns false and prevents the next wait, while a wake after entering the waiting state returns true. The state header is internal under `src/concurrency/detail`; no public API was added.

## Tests and outputs

- RED: focused regression failed for the expected assertion above.
- GREEN, C++20 focused test:
  `build\pr-debug-cpp20\test\stlab.test.executor.exe -tc="portable waiter retains a wake requested before waiting"`
  Output: `1 passed | 0 failed`; `4 assertions: 4 passed`.
- Static executor suite:
  `ctest --test-dir build\pr-debug-cpp20 -R '^stlab.test.executor$' --output-on-failure`
  Output: `100% tests passed, 0 tests failed out of 1`.
- Portable shared-core suite (Windows, `STLAB_CORE_SHARED=ON`, `STLAB_TASK_SYSTEM=portable`):
  `ctest --test-dir build\pr-shared-core-portable -R 'stlab.test.(executor|core_shared_smoke|portable_shared_smoke)' --output-on-failure`
  Output: all 3 tests passed (`executor`, `core_shared_smoke`, `portable_shared_smoke`).
- C++17 focused regression:
  Built `stlab.test.executor` in `build\debug-cpp17`, then ran the same doctest filter.
  Output: `1 passed | 0 failed`; `4 assertions: 4 passed`.
- Formatting/whitespace: `clang-format --dry-run --Werror` passed before restoring unrelated pre-existing formatting in the test file; `git diff --check` passed on the final code changes.

## Changed files

- `src/concurrency/detail/waiter_state.hpp` — internal, mutex-serialized waiter state; pending wake latch and documented contracts.
- `src/concurrency/executor_abi.cpp` — use the state transitions for portable worker wake/wait/shutdown.
- `test/executor_test.cpp` — deterministic state regression; contract documentation for the ABI guard and submission-wait helper.
- `test/core_shared_smoke_main.cpp` — contract documentation for `main`.

## Self-review

- `wake()` remains true only when the worker was already waiting and the condition variable will be signaled; early wake remains false, so pool expansion behavior is unchanged.
- A retained wake is consumed before sleeping and causes the worker to re-check queues; no queue hint is discarded and no task executes inline.
- The shared state is always accessed under the waiter's existing mutex in production.
- The regression covers the exact early-wake transition deterministically and contains no timing-only stress loop.
- C++17 and C++20 test builds passed, including portable shared-core integration coverage.
- No unrelated source changes, ABI changes, or public headers were introduced.

## Environment note

The Visual Studio `vcvars64.bat` setup printed `'vswhere.exe' is not recognized` in this environment, but the configured x64 builds completed and all listed tests passed. An initial build invocation outside the x64 developer setup selected x86 Windows libraries; rerunning inside `vcvars64.bat` resolved that environment issue.
