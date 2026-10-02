# STLab Execution Library Extraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract independently consumable `stlab-execution`, preserving STLab source compatibility through a public CPM dependency.

**Architecture:** Improve the toolkit's compiled-target and nested-install support, then extract execution APIs and implementations without changing their behavior. Give execution its own configuration/version namespace, replace STLab's local core with the dependency, and verify standalone and integrated consumers.

**Tech Stack:** C++17/20, CMake 3.24+, Ninja presets, CPM, cpp-library, doctest, Doxygen, GitHub Actions.

**Spec:** `docs\superpowers\specs\2026-10-01-execution-library-extraction-design.md`, approved after commit `411654f`.

## Global Constraints

- New repository/package: `stlab/stlab-execution` / `stlab-execution`.
- Local repository: `D:\repos\github.com\stlab\stlab-execution`.
- CMake target: `stlab::execution`.
- Build infrastructure: `cpp-library`, with narrowly scoped improvements.
- Compatibility: Existing include paths and `stlab::` public APIs remain canonical.
- Header ownership: Exactly one package owns each header; no forwarding copies.
- Binary compatibility: Recompilation is required; existing v2 C ABI is preserved.
- Dependency direction: STLab depends on execution; execution never depends on STLab.
- The minimum supported language remains C++17.
- Never execute queued work inline as a scheduling fallback.
- Preserve task small-buffer behavior and allocation-free ABI transport.
- Preserve timer ownership/error reporting, storage guards, and native/cooperative shutdown.
- Remote repository creation and release publishing are separate authorized operations.
- No destructive cleanup, unrelated edits, or commits without the Copilot co-author trailer.

## Working directories and sequencing

The STLab worktree is already `worktree-refactor-core`. Before executing, invoke
`using-git-worktrees` and create an isolated toolkit worktree from
`D:\repos\github.com\stlab\cpp-library`, named `execution-support`, at
`D:\repos\github.com\stlab\cpp-library\.claude\worktrees\execution-support`.
Check this proposed path for an existing worktree and reuse it only if it belongs
to this task. Never edit the toolkit's main checkout.

Create the new execution repository on branch `extract-execution`, not `main`.
The approved destination did not exist during design. Recheck before creating it;
if it now contains unrelated work, stop and request direction.

Tasks are sequential: 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7. Toolkit fixes are reusable
deliverables; execution becomes independently buildable before STLab removes its
local files. Copying into the new repository is temporary development staging,
not final duplicate package ownership.

Run commands in the task's repository directory. Windows MSVC commands require
the VS developer environment in the same process as configure/build. Use existing
VS environment discovery; do not assume a particular VS installation path.
Use backslash-separated filesystem paths on Windows.

Before editing runtime or integration files, capture STLab's focused baseline
using `debug-cpp20`: build the future, channel, executor, serial-queue, timer,
and task targets and run their matching CTest entries. Record any pre-existing
failure rather than treating it as an extraction regression.

Local CMake override arguments:

```powershell
'-DCPM_cpp-library_SOURCE=D:\repos\github.com\stlab\cpp-library\.claude\worktrees\execution-support'
'-DCPM_stlab-execution_SOURCE=D:\repos\github.com\stlab\stlab-execution'
```

Keep overrides on command lines or in ignored user presets. Never commit those
absolute paths to library build files.

### Dependency pin and version procedure

Do not guess a toolkit release number. After Task 2, get the toolkit implementation
commit using `git rev-parse HEAD` in its worktree. Use that exact SHA as the
development `GIT_TAG` for cpp-library, together with the local override. Before
publishing execution, replace it with the approved released toolkit version.

During local development the new repository can have no release tag.
For standalone install verification, configure its install preset with
`-DCPP_LIBRARY_VERSION=1.0.0`. For STLab's local dependency, use a commit SHA pin
without a CPM `VERSION` constraint until execution has an approved release:
resolve `git rev-parse HEAD` in the execution repository and use its output
as the literal `GIT_TAG` in a long-form `CPMAddPackage` with
`NAME stlab-execution GITHUB_REPOSITORY stlab/stlab-execution`.
Use explicit dependency
mapping to `stlab-execution` without a minimum version during this local-only
integration stage. Task 7 replaces it with the approved release-version
requirement before STLab publication.

This avoids creating or moving local release tags merely to satisfy tests.
Source overrides permit verification before those commits exist remotely.
An unpublished SHA is not a publishable dependency: mark publication blocked
until the authorization and release-order gates are satisfied.

## File responsibility map

| Repository | Files | Responsibility |
| --- | --- | --- |
| Toolkit | `cpp-library.cmake`, `cmake\cpp-library-setup.cmake` | Public setup arguments and explicit compiled target type |
| Toolkit | `cmake\cpp-library-install.cmake` | Install controls, package-scoped deferred state |
| Toolkit | `tests\setup\test_target_type.cmake`, `tests\install\test_nested_install.cmake`, fixtures | Contract regressions |
| Toolkit | `README.md`, `.github\workflows\ci.yml` | Document/execute toolkit regressions |
| Execution | `CMakeLists.txt`, `cmake\ExecutionPlatform.cmake`, `cmake\ExecutionConfig.cmake` | Setup, backend/config ownership |
| Execution | `include\stlab\execution\config.hpp.in` | Generated lower-layer macros and independent namespace |
| Execution | Existing execution header paths, `src\concurrency`, `src\pre_exit.cpp`, `src\execution.def` | Extracted APIs/runtime |
| Execution | `test\CMakeLists.txt`, `test\main.cpp`, execution test sources | Standalone behavior/ABI tests |
| Execution | `CMakePresets.json`, `.github\workflows\ci.yml`, docs | Standalone development/platform matrix |
| STLab | Root/include/src CMake files, `config.hpp.in`, `cmake\StlabUtil.cmake` | Remove local runtime, include dependency config |
| STLab | `test\CMakeLists.txt`, mixed test sources, `test\package` | Retained integration and package consumers |
| STLab | README/changes/docs/presets/workflow | Compatibility, package ownership, integration matrix |

## Task 1: Explicit compiled library type in cpp-library

**Files:**
- Modify toolkit `cpp-library.cmake`, setup argument parsing and `_cpp_library_setup_core` call.
- Modify toolkit `cmake\cpp-library-setup.cmake`, `_cpp_library_setup_core`.
- Create toolkit `tests\setup\test_target_type.cmake`.
- Create fixture `tests\setup\fixtures\target_type\CMakeLists.txt`, `include\fixture\sample.hpp`, `src\sample.cpp`.
- Modify toolkit `README.md`, `.github\workflows\ci.yml`.

**Interfaces:**
- Consumes existing `cpp_library_setup(DESCRIPTION NAMESPACE HEADERS SOURCES ...)`.
- Produces optional `LIBRARY_TYPE STATIC|SHARED`; omitted retains `BUILD_SHARED_LIBS`.
- Header-only setup remains automatically `INTERFACE`; explicit compiled type without sources is diagnosed.

- [ ] **Step 1: Add the configuration fixture and assertions.**

Use this fixture setup (include toolkit using a `TOOLKIT_SOURCE` argument):

```cmake
cmake_minimum_required(VERSION 3.24)
include("${TOOLKIT_SOURCE}/cpp-library.cmake")
cpp_library_enable_dependency_tracking()
project(sample LANGUAGES CXX)
set(CPP_LIBRARY_VERSION 1.0.0)
set(FIXTURE_INSTALL OFF CACHE BOOL "" FORCE)
set(type_args)
if(DEFINED REQUESTED_TYPE)
  list(APPEND type_args LIBRARY_TYPE "${REQUESTED_TYPE}")
endif()
set(source_args SOURCES sample.cpp)
if(OMIT_SOURCES)
  set(source_args)
endif()
cpp_library_setup(
  DESCRIPTION "Explicit compiled target fixture"
  NAMESPACE fixture
  HEADERS sample.hpp
  ${source_args}
  ${type_args})
get_target_property(actual sample TYPE)
if(NOT actual STREQUAL EXPECTED_TYPE)
  message(FATAL_ERROR "Expected ${EXPECTED_TYPE}, got ${actual}")
endif()
if(NOT BUILD_SHARED_LIBS STREQUAL EXPECTED_BUILD_SHARED_LIBS)
  message(FATAL_ERROR "Setup changed parent BUILD_SHARED_LIBS")
endif()
```

The two source files declare/define `int fixture::sample() { return 42; }`.
Copy the fixture to test-owned temporary directories before configuring it,
because toolkit initialization writes templates into top-level source directories.
The script runs separate binary directories for these cases:

| Request | BUILD_SHARED_LIBS | Expected target |
| --- | --- | --- |
| omitted | OFF | STATIC_LIBRARY |
| omitted | ON | SHARED_LIBRARY |
| STATIC | ON | STATIC_LIBRARY |
| SHARED | OFF | SHARED_LIBRARY |

For invalid `MODULE` and explicit type without sources, require configure failure
and a specific `cpp_library_setup` diagnostic; arbitrary failure is not success.
Use `OMIT_SOURCES=ON` for that last case. The runner's assertion pattern is:

```cmake
execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${case_source}" -B "${case_binary}" -G Ninja
    "-DTOOLKIT_SOURCE=${toolkit_source}"
    "-DBUILD_SHARED_LIBS=${shared}"
    "-DEXPECTED_BUILD_SHARED_LIBS=${shared}"
    "-DEXPECTED_TYPE=${expected_type}"
    ${request_args}
  RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(expected_diagnostic)
  if(result EQUAL 0 OR NOT "${out}\n${err}" MATCHES "${expected_diagnostic}")
    message(FATAL_ERROR "Expected diagnostic ${expected_diagnostic}:\n${out}\n${err}")
  endif()
elseif(NOT result EQUAL 0)
  message(FATAL_ERROR "Target type case failed:\n${out}\n${err}")
endif()
```

Here each matrix row defines `shared`, `expected_type`, and `request_args`;
invalid cases set `expected_diagnostic` to the exact new validation message.
Resolve `toolkit_source` from the test script's repository, not the current
working directory, and create fresh source/binary directories for each row.

- [ ] **Step 2: Run red.**

```powershell
cmake -P tests\setup\test_target_type.cmake
```

Expected: explicit STATIC/SHARED cases fail the target-type assertion before
implementation. Confirm default cases still configure.

- [ ] **Step 3: Thread the argument through both setup layers.**

Add `LIBRARY_TYPE` to both argument parsers and propagate it to the core helper.
Validate before creating the target:

```cmake
if(ARG_LIBRARY_TYPE AND NOT ARG_LIBRARY_TYPE MATCHES "^(STATIC|SHARED)$")
  message(FATAL_ERROR "cpp_library_setup: LIBRARY_TYPE must be STATIC or SHARED")
endif()
if(ARG_LIBRARY_TYPE AND NOT ARG_SOURCES)
  message(FATAL_ERROR "cpp_library_setup: LIBRARY_TYPE requires SOURCES")
endif()
if(ARG_SOURCES)
  if(ARG_LIBRARY_TYPE)
    add_library(${ARG_NAME} ${ARG_LIBRARY_TYPE} ${ARG_SOURCES})
  else()
    add_library(${ARG_NAME} ${ARG_SOURCES})
  endif()
endif()
```

Replace only existing compiled-target creation; keep alias, file sets, features,
and header-only behavior. Do not temporarily set global `BUILD_SHARED_LIBS`.

- [ ] **Step 4: Run green and add the test to toolkit CI.**

Run the red command again, then:

```powershell
cmake -P tests\install\CMakeLists.txt
cmake -P tests\install\test_provider_merge.cmake
```

Expected: all fixture configurations and existing script tests pass.
Document the optional argument and add its script to the unit-test job.

- [ ] **Step 5: Commit this deliverable.**

Stage only this task's files, then:

```powershell
git commit -m "feat: support explicit compiled library types" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

## Task 2: Independent package installation and deferred state

**Files:**
- Modify toolkit `cpp-library.cmake`, `cmake\cpp-library-setup.cmake`.
- Modify toolkit `cmake\cpp-library-install.cmake`.
- Create toolkit `tests\install\test_nested_install.cmake`.
- Create toolkit `tests\install\fixtures\nested\CMakeLists.txt`, `include\stlab\parent.hpp`, `leaf\CMakeLists.txt`, `leaf\include\stlab\leaf.hpp`.
- Create toolkit `tests\install\fixtures\consumer\CMakeLists.txt`, `main.cpp`.
- Modify toolkit `README.md`, `.github\workflows\ci.yml`.

**Interfaces:**
- Produces optional `cpp_library_setup(... INSTALL_OPTION STLAB_EXECUTION_INSTALL)`.
- Omitted `INSTALL_OPTION` preserves the current uppercase namespace install option.
- Internal finalization consumes a target name, not shared global "current package" metadata.
- No new STLab-specific toolkit behavior.

- [ ] **Step 1: Write the nested fixture and round-trip script.**

Both fixture packages use namespace `stlab`, local toolkit inclusion, and
directory-local `set(CPP_LIBRARY_VERSION 1.0.0)`. Parent declares `project(parent)`;
leaf declares `project(leaf)`. Parent configures leaf before finalizing itself.

```cmake
cpp_library_setup(
  DESCRIPTION "Parent package fixture"
  NAMESPACE stlab
  HEADERS parent.hpp
  INSTALL_OPTION STLAB_PARENT_INSTALL)
add_subdirectory(leaf)
target_link_libraries(parent INTERFACE $<BUILD_INTERFACE:stlab::leaf>)
cpp_library_map_dependency("stlab::leaf" "stlab-leaf 1.0.0")
```

Leaf uses `INSTALL_OPTION STLAB_LEAF_INSTALL` and no dependency on parent.
Header functions return 20 (parent) and 22 (leaf). The consumer requires:

```cmake
find_package(stlab-parent 1.0.0 REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE stlab::parent)
```

```cpp
#include <stlab/parent.hpp>
#include <stlab/leaf.hpp>
int main() { return stlab::parent() + stlab::leaf() == 42 ? 0 : 1; }
```

The script configures/builds/installs/consumes fixtures under a uniquely named
test directory, copying source fixtures there before toolkit initialization.
Check both Config/ConfigVersion/Targets files exist, parent
config resolves only leaf, and leaf config never resolves parent. Check parent
ON/leaf OFF and parent OFF/leaf ON produce only their selected packages.
Check omitted custom options still honor legacy `STLAB_INSTALL`.

Add an untracked imported dependency to only one fixture variant. Its install
must fail with that package's dependency diagnostic, not contaminate the other
package. Assert diagnostics as well as exit status.

- [ ] **Step 2: Run red.**

```powershell
cmake -P tests\install\test_nested_install.cmake
```

Expected: custom install controls or nested config generation fail with the
existing namespace option/global metadata implementation.

- [ ] **Step 3: Add optional controls and target-scoped metadata.**

Thread `INSTALL_OPTION` through setup/core/install. Resolve the option name:

```cmake
if(ARG_INSTALL_OPTION)
  set(install_option "${ARG_INSTALL_OPTION}")
else()
  string(TOUPPER "${ARG_NAMESPACE}" namespace_upper)
  set(install_option "${namespace_upper}_INSTALL")
endif()
option(${install_option} "Enable installation of ${ARG_PACKAGE_NAME}" ${PROJECT_IS_TOP_LEVEL})
if(NOT ${install_option})
  return()
endif()
```

Store package name, version, namespace, toolkit root, and binary directory as
properties on `${ARG_NAME}`. Replace paired deferred callbacks with one ordered
finalizer `_cpp_library_finish_install(target_name)`: generate configuration,
then register validation and config/export installation. Freeze the target
argument when scheduling, rather than referencing expired function locals:

```cmake
cmake_language(EVAL CODE
  "cmake_language(DEFER CALL _cpp_library_finish_install [[${ARG_NAME}]])")
```

Do not rely on the existing comments claiming DEFER callbacks run LIFO.
Within finalization, read only that target's properties. Isolate dependency
generation's unverified-dependency collection per invocation and return it to
the caller; preserve existing three-argument dependency helper tests. Store
validation output per package. Do not erase global provider tracking records
needed by other packages.

- [ ] **Step 4: Run green and the toolkit baseline.**

```powershell
cmake -P tests\install\test_nested_install.cmake
cmake -P tests\setup\test_target_type.cmake
cmake -P tests\install\CMakeLists.txt
cmake -P tests\install\test_provider_merge.cmake
cmake -P tests\setup\test_setup_version_resolution.cmake
```

Expected: independent configs/options and successful consumer execution; failure
variants produce only their intended diagnostic. Document `INSTALL_OPTION`,
add the nested regression to CI, and preserve existing toolkit consumer tests.

- [ ] **Step 5: Commit and record the actual toolkit SHA.**

```powershell
git commit -m "fix: isolate nested package installation state" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
git rev-parse HEAD
```

Review this toolkit deliverable before starting extraction. Publication remains
a separate authorization gate.

## Task 3: Independently buildable execution library

**Files:**
- Create execution `CMakeLists.txt`, `LICENSE`, `README.md`, `.gitignore`, `.clang-format`, `CMakePresets.json`.
- Create execution `cmake\ExecutionPlatform.cmake`, `cmake\ExecutionConfig.cmake`, `cmake\CPM.cmake`, `cmake\Findlibdispatch.cmake`.
- Create execution `include\stlab\execution\config.hpp.in`.
- Copy the seven approved concurrency headers and `pre_exit.hpp` at identical include paths.
- Copy `include\stlab\concurrency\detail\libdispatch_executor_group.hpp`.
- Copy `src\pre_exit.cpp` and all `src\concurrency` files listed below.
- Copy `src\stlab.def` as execution `src\execution.def`.
- Create execution `test\header_smoke.cpp`, `test\CMakeLists.txt`.

**Interfaces:**
- Consumes `LIBRARY_TYPE`, `INSTALL_OPTION` from Tasks 1/2.
- Produces target `stlab::execution`, existing public APIs, v2 C entry points,
  and generated `stlab/execution/config.hpp`.
- Configuration macro names: `STLAB_EXECUTION_VERSION_MAJOR/MINOR/PATCH()`,
  `STLAB_EXECUTION_VERSION_NAMESPACE_BEGIN/END()`.
- Internal namespace: `execution_detail`, distinct from STLab `detail`.

- [ ] **Step 1: Create a new repository and a failing public consumer.**

Initialize the approved new directory on `extract-execution` and create only the
test/build files needed to demonstrate missing standalone headers/target:

```cpp
#include <stlab/concurrency/task.hpp>
#include <stlab/concurrency/executor_base.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/main_executor.hpp>
#include <stlab/concurrency/immediate_executor.hpp>
#include <stlab/concurrency/system_timer.hpp>
#include <stlab/concurrency/set_current_thread_name.hpp>
#include <stlab/pre_exit.hpp>
#include <memory>
int main() {
    int result = 0;
    stlab::task<void() noexcept> work =
        [p = std::make_unique<int>(42), &result]() noexcept { result = *p; };
    stlab::immediate_executor(std::move(work));
    stlab::pre_exit();
    return result == 42 ? 0 : 1;
}
```

Add this executable to `test\CMakeLists.txt`, linking only `stlab::execution`.
Use a root `test` preset with BUILD_TESTING ON and run configure/build.
Expected red: the standalone target/headers do not yet exist.

- [ ] **Step 2: Copy the approved implementation and retained license notices.**

Copy, without scheduler logic changes:

```text
src\pre_exit.cpp
src\concurrency\executor_abi.cpp
src\concurrency\core_shutdown.cpp
src\concurrency\cooperative_executor.cpp
src\concurrency\main_executor_emscripten.cpp
src\concurrency\main_executor_libdispatch.cpp
src\concurrency\main_executor_none.cpp
src\concurrency\main_executor_portable.cpp
src\concurrency\main_executor_qt.cpp
src\concurrency\system_timer_emscripten.cpp
src\concurrency\system_timer_libdispatch.cpp
src\concurrency\system_timer_portable.cpp
src\concurrency\system_timer_windows.cpp
src\concurrency\detail\cooperative_executor.hpp
src\concurrency\detail\core_shutdown.hpp
src\concurrency\detail\main_task_queue.hpp
src\concurrency\detail\system_timer_shutdown.hpp
src\concurrency\detail\timer_common.hpp
src\concurrency\detail\waiter_state.hpp
```

Do not copy `src\stlab.cpp`; it is STLab's shared-library anchor, not execution.
Move platform discovery/config generation out of `StlabUtil.cmake` into the two
focused execution modules. Copy `Findlibdispatch.cmake` and CPM support.
Preserve the original copyright notices and Boost license.

- [ ] **Step 3: Split configuration and namespace ownership.**

Replace extracted includes of `stlab/config.hpp` with
`stlab/execution/config.hpp`; replace extracted
`STLAB_VERSION_NAMESPACE_BEGIN/END` with execution-specific macros.
Rename extracted `namespace detail` and coupled references to
`execution_detail`, including ABI guard definitions. Leave explicit
`inline namespace v2` C ABI declarations and entry-point names unchanged.

The namespace template includes:

```cpp
#define STLAB_EXECUTION_VERSION_MAJOR() @PROJECT_VERSION_MAJOR@
#define STLAB_EXECUTION_VERSION_MINOR() @PROJECT_VERSION_MINOR@
#define STLAB_EXECUTION_VERSION_PATCH() @PROJECT_VERSION_PATCH@
#define STLAB_EXECUTION_VERSION_NAMESPACE_BEGIN() \
    inline namespace execution_v@PROJECT_VERSION_MAJOR@_@PROJECT_VERSION_MINOR@_@PROJECT_VERSION_PATCH@ {
#define STLAB_EXECUTION_VERSION_NAMESPACE_END() }
```

Move existing compiler-feature, thread/task/main configuration, pool maximum,
and export macro definitions to this template without changing their values.
Do not define `STLAB_VERSION_NAMESPACE_BEGIN/END` or STLab release macros here.
Keep `STLAB_CORE_BUILD`/`STLAB_CORE_API` internally compatible for this extraction.

- [ ] **Step 4: Wire compiled target and generated header file set.**

Use this target setup, supplying the resolved literal toolkit SHA:

```cmake
project(execution LANGUAGES CXX)
cpp_library_setup(
  DESCRIPTION "Portable task execution, timers, and process lifecycle"
  NAMESPACE stlab
  REQUIRES_CPP_VERSION 17
  LIBRARY_TYPE ${execution_library_type}
  INSTALL_OPTION STLAB_EXECUTION_INSTALL
  HEADERS
    concurrency/task.hpp
    concurrency/executor_base.hpp
    concurrency/default_executor.hpp
    concurrency/main_executor.hpp
    concurrency/immediate_executor.hpp
    concurrency/system_timer.hpp
    concurrency/set_current_thread_name.hpp
    pre_exit.hpp
  SOURCES
    pre_exit.cpp
    concurrency/executor_abi.cpp
    concurrency/core_shutdown.cpp)
target_sources(execution PUBLIC
  FILE_SET headers TYPE HEADERS
  BASE_DIRS "${PROJECT_BINARY_DIR}/include"
  FILES "${PROJECT_BINARY_DIR}/include/stlab/execution/config.hpp")
target_include_directories(execution PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>")
target_compile_definitions(execution PRIVATE STLAB_CORE_BUILD)
```

Fetch/include toolkit and enable tracking before `project`, as in existing STLab.
Generate config before adding its file set. Add sources from the existing backend
selection in `src\CMakeLists.txt` to `execution`, including cooperative executor
only for that task system and Windows `.def` only for shared Windows.
Keep private implementation headers private.

Choose static/shared using the approved canonical option and legacy spelling:
capture whether each option was supplied before declaring defaults; differing
values fail on a fresh configuration. On reconfiguration, a spelling changed
since the last resolved configuration wins over its unchanged cached counterpart,
even if that counterpart was redundantly supplied again. Preserve both controls
without falsely treating generated defaults as independent requests.
Without a new explicit setting, preserve existing behavior:
legacy ON selects shared; non-Windows BUILD_SHARED_LIBS ON selects shared;
otherwise static. Retain effective `STLAB_CORE_SHARED` for downstream compatibility.
Link Threads/libdispatch/Qt only as required by the selected execution backend.
Preserve exception, PIC, NOMINMAX, and sanitizer handling from the original build.

- [ ] **Step 5: Run green and independent header checks.**

Configure execution using its `test` preset and local toolkit override; build
and run `execution.test.header_smoke`. Also generate one compile-only target per
approved public header, with a translation unit containing only that include.
Each target links `stlab::execution`, never STLab.

Run the smoke program with both C++17 and C++20 presets. Check the standalone
compile commands contain no STLab source/build include directory. Confirm no
extracted source includes `stlab/config.hpp`.

- [ ] **Step 6: Commit the standalone compiled deliverable.**

```powershell
git commit -m "feat: extract standalone execution foundation" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

## Task 4: Execution-owned contract, lifecycle, and ABI tests

**Files:**
- Create execution `test\main.cpp`, `task_contract_test.cpp`, `executor_contract_test.cpp`, `executor_abi_test.cpp`, `waiter_state_test.cpp`.
- Move execution-only cases/sources listed below to execution `test`.
- Modify execution `test\CMakeLists.txt`, `CMakePresets.json`.
- Stage the corresponding STLab deletions/splits with Task 5, not before CPM wiring.

**Interfaces:**
- Consumes standalone `stlab::execution`.
- Produces execution-only runtime tests and ABI failure tests, without future,
  serial queue, or STLab test-model dependencies.

- [ ] **Step 1: Add new public contract tests before changing test ownership.**

Derive new tests from task/executor contracts, not implementation internals:

```cpp
TEST_CASE("task retains a move-only capture through move construction") {
    int observed = 0;
    stlab::task<void() noexcept> first =
        [p = std::make_unique<int>(42), &observed]() noexcept { observed = *p; };
    stlab::task<void() noexcept> second = std::move(first);
    second();
    CHECK(observed == 42);
}
TEST_CASE("immediate executor runs on the calling thread") {
    const auto caller = std::this_thread::get_id();
    auto observed = caller;
    stlab::immediate_executor([&]() noexcept { observed = std::this_thread::get_id(); });
    CHECK(observed == caller);
}
```

For each default/high/low executor, submit a task capturing a move-only owner and
fulfill `std::promise<int>` with 42. Require readiness within five seconds and
the value 42. The task must not throw. Preserve public contract coverage for an
empty task (`std::bad_function_call`), target ownership/destruction, and timers.
Reuse the existing doctest main's `pre_exit()` pattern; do not copy
`stlab/test/model.hpp` or add a dependency on it.

New tests protect unchanged contracts and can pass immediately after extraction.
Their red evidence is missing standalone test targets before registration, not
an invented behavior regression. Run them after registering the execution harness.

- [ ] **Step 2: Move standalone lifecycle and ABI sources.**

Move unchanged sources, adjusting configuration/detail namespace and target names:

```text
core_shared_smoke_main.cpp
core_shared_smoke_test.cpp
core_shutdown_first.cpp
executor_priority_shutdown.cpp
main_executor_concurrent_test.cpp
main_executor_order_test.cpp
main_executor_pre_exit_before_submit_test.cpp
main_executor_pre_exit_test.cpp
main_executor_shutdown_order_test.cpp
main_executor_test_host.hpp
system_timer_application_shutdown.cpp
system_timer_executor_shutdown.cpp
system_timer_lifecycle.cpp
system_timer_resource_failure.cpp
system_timer_shutdown_first.cpp
task_abi_mismatch_test.cpp
expect_task_abi_mismatch.cmake
emscripten_timer_test.cpp
emscripten_config_test.cmake
expect_emscripten_terminate.cmake
```

The main-host helper includes STLab's config today: change it and the moved
sources to execution config. Transfer the Emscripten test toolchain to execution
as `cmake\Platform\Emscripten-Execution.cmake`, preserving try-compile variables,
threading validation, exception flags, and Node emulator behavior.

Retain ABI mismatch rejection as an EXCLUDE_FROM_ALL target and update the driver
to build `execution.test.task_abi_mismatch`. The driver must still require an
unresolved guard diagnostic, not any link failure. Preserve test timeouts and
runtime DLL copying.

- [ ] **Step 3: Split mixed files by actual dependencies.**

| Current STLab file | Execution portion | Retained STLab portion |
| --- | --- | --- |
| `executor_test.cpp` | waiter-state regression and the two `abi_executor_submit_*` cases/helpers | serial-queue priority tests, `invoke_waiting` cases, disabled historical block |
| `system_timer_test.cpp` | all cases except `system_timer_cancellation` | package/future/await cancellation integration case |
| `cooperative_shutdown_test.cpp` | all scenarios except `future_chain` | future-chain shutdown integration |

Keep current `task_test.cpp` in STLab because it uses `stlab/test/model.hpp`,
which itself depends on `await`. Standalone task tests use the new contract
cases. Keep `portable_shared_smoke_test.cpp` and `emscripten_cooperative_test.cpp`
in STLab because they depend on await/futures.

Move the waiter regression to execution-private test coverage; remove its
relative include of STLab `src\concurrency\detail\waiter_state.hpp` from STLab.
Do not expose that internal header to STLab or add a dependency on execution's
source-tree layout. New tests should use public contracts; the existing ABI and
waiter regressions are preserved, not copied as a model for new behavior tests.

For `cooperative_shutdown_test.cpp`, preserve every scenario name and event
sequence. Give execution its own copy of the harness needed for its scenarios;
retain a STLab-owned host helper for the future scenario and Emscripten integration.
These are private test helpers, not duplicated installed public headers.

- [ ] **Step 4: Run focused native/shared/cooperative coverage.**

Native:

```powershell
cmake --build --preset=test
ctest --preset=test -R 'task|executor|timer|shutdown'
```

Add execution presets `test-cpp17`, `test-portable`, `test-shared`,
`test-shared-portable`, `test-portable-main`, `test-asan`,
`test-emscripten`, and `test-emscripten-threadless`, based on `test`.
Use the transferred toolchain for Emscripten presets. Add matching build/test
presets so the commands are unambiguous.

Shared Windows must cover native and portable, core smoke, and guard failure.
Resource-allocation override tests stay restricted to configurations where
their current allocator interception is valid. Cooperative event-loop tests
must run under the Node emulator; no blocking doctest harness there.

- [ ] **Step 5: Commit execution-owned tests.**

```powershell
git commit -m "test: verify standalone execution contracts and lifecycle" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
git rev-parse HEAD
```

Use this actual execution SHA in Task 5's local development dependency pin.

## Task 5: Replace STLab's local core with its CPM dependency

**Files:**
- Modify STLab `CMakeLists.txt`, `include\stlab\CMakeLists.txt`, `src\CMakeLists.txt`.
- Modify STLab `include\stlab\config.hpp.in`, `cmake\StlabUtil.cmake`.
- Delete STLab's extracted public/runtime files using the exact Task 3 inventory.
- Modify STLab `test\CMakeLists.txt`, `executor_test.cpp`, `system_timer_test.cpp`, `cooperative_shutdown_test.cpp`, host helper.
- Remove transferred standalone test files, retaining helpers still used by integration tests.
- Create STLab `test\execution_dependency_test.cpp`.

**Interfaces:**
- Consumes CPM package `stlab-execution` and target `stlab::execution`.
- Produces existing `stlab::stlab`, build compatibility `stlab-core`/
  `stlab::stlab-core`, and installed compatibility `stlab::stlab-core`.
- `stlab/config.hpp` includes execution config and retains STLab version/coroutine macros.

- [ ] **Step 1: Add a failing dependency-ownership regression.**

Register a test executable that includes both configurations and both layers:

```cpp
#include <stlab/config.hpp>
#include <stlab/execution/config.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/await.hpp>
#include <stlab/pre_exit.hpp>
int main() {
    auto value = stlab::async(stlab::default_executor, [] { return 42; });
    const auto result = stlab::await(std::move(value));
    stlab::pre_exit();
    return result == 42 ? 0 : 1;
}
```

Link only `stlab::stlab`. Add a CMake assertion that its public dependency is
`stlab::execution` and that `stlab-core` is INTERFACE, not a compiled runtime.
Run the test target before wiring CPM: expect missing execution config or failed
target/dependency assertion, not a scheduler behavior failure.

- [ ] **Step 2: Replace local core creation and platform setup.**

Fetch execution after STLab's `project` and version setup using its exact Task 4
commit SHA and local override. Fetch toolkit using the exact Task 2 SHA while
developing. Remove STLab's local core creation, runtime source selection, and
execution backend discovery. Keep STLab's coroutine and unrelated setup.

```cmake
target_link_libraries(stlab PUBLIC
  $<BUILD_INTERFACE:stlab::execution>
  $<INSTALL_INTERFACE:stlab::execution>)
add_library(stlab-core INTERFACE)
add_library(stlab::stlab-core ALIAS stlab-core)
target_link_libraries(stlab-core INTERFACE
  $<BUILD_INTERFACE:stlab::execution>
  $<INSTALL_INTERFACE:stlab::execution>)
```

Export the compatibility INTERFACE target in `stlabTargets`; it has no sources
or headers. Remove the old compiled-core install rule. During the local SHA-pin
stage, map `stlab::execution` to `stlab-execution` without a guessed version;
Task 7 adds the approved release constraint.

Give STLab's `_cpp_library_setup_install` its explicit `INSTALL_OPTION STLAB_INSTALL`;
execution uses `STLAB_EXECUTION_INSTALL`. Neither package should acquire the
other package's install choice through namespace defaults.

- [ ] **Step 3: Remove duplicate header/config ownership and complete mixed-test splits.**

Remove extracted headers from STLab's header file set and repository. Keep
STLab's generated config file set, adding:

```cpp
#include <stlab/execution/config.hpp>
```

Remove from STLab config only macros now owned by execution. Keep its release
version macros and coroutine settings; do not redefine common lower-layer macros.
Remove redundant execution generation/detection from `StlabUtil.cmake`.
Retain only STLab-specific behavior or remove the module if it has no remaining
consumers; verify all references before deletion.

Finish the Task 4 mixed-test splits and CMake removals in this same coherent
change. Retained integration tests link `stlab::testing`/`stlab::stlab`.
Keep the future-chain cooperative scenario in STLab's CTest and link STLab,
not the old "core-only" target. Retain termination helpers needed by await
integration. Do not delete a shared test helper while an integration test uses it.

- [ ] **Step 4: Run red-to-green and related component suites.**

```powershell
cmake --preset=debug-cpp20 '-DCPM_cpp-library_SOURCE=D:\repos\github.com\stlab\cpp-library\.claude\worktrees\execution-support' '-DCPM_stlab-execution_SOURCE=D:\repos\github.com\stlab\stlab-execution'
cmake --build --preset=debug-cpp20 --target stlab.test.execution_dependency stlab.test.future stlab.test.channel stlab.test.executor stlab.test.serial_queue stlab.test.system_timer stlab.test.task
ctest --preset=debug-cpp20 -R 'execution_dependency|future|channel|executor|serial_queue|system_timer|task'
```

Repeat under `debug-cpp17`. Confirm moved runtime files compile only in execution,
and STLab's private test includes no execution implementation header.
Repeat retained cooperative future-chain/await tests under threadless Emscripten.

- [ ] **Step 5: Commit STLab integration.**

```powershell
git commit -m "refactor: consume standalone execution through CPM" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

## Task 6: Package installation and downstream compatibility

**Files:**
- Create STLab `test\package\CMakeLists.txt`, `main.cpp`, `legacy.cpp`.
- Create execution `test\package\CMakeLists.txt`, `main.cpp`.
- Create STLab `test\verify_execution_packages.cmake`.
- Modify execution/STLab presets and relevant test CMake registration.

**Interfaces:**
- Installed `find_package(stlab-execution)` exposes `stlab::execution`.
- Installed `find_package(stlab)` transitively finds execution and exposes
  `stlab::stlab` plus compatibility `stlab::stlab-core`.
- Execution-only consumer has no STLab package dependency.

- [ ] **Step 1: Add failing installed consumers.**

The execution consumer includes `task.hpp`, `default_executor.hpp`, `system_timer.hpp`,
and `pre_exit.hpp`, then schedules a timer fulfilling `std::promise<int>`.
Require readiness within five seconds, result 42, and call `pre_exit`.

```cmake
cmake_minimum_required(VERSION 3.24)
project(execution-consumer LANGUAGES CXX)
find_package(stlab-execution REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE stlab::execution)
```

STLab's consumer uses Task 5's async/await program, links `stlab::stlab`, and adds
another executable using only executor/task APIs linked to `stlab::stlab-core`.
On Windows add a post-build `$<TARGET_RUNTIME_DLLS:consumer>` copy for each
runtime test target. Add public-header compile checks against installed includes.

The script requires source directories and a named verification root, then
invokes configure/build/install/consumer commands with explicit result checks.
Use the projects' install presets, not an independent ad hoc library build.

- [ ] **Step 2: Verify red before correcting remaining package export defects.**

Run both installed consumers against the first extraction's install output.
A missing compatibility target, dependency, or generated header should fail
explicitly. If already green, retain the coverage and record that no package
behavior defect was introduced; do not manufacture a failure.

- [ ] **Step 3: Exercise independent and combined installation.**

Install execution alone into one named prefix and build its consumer with no
STLab prefix. Then install STLab and execution into a second prefix.
For the no-tag execution repository, its independent install configure uses
`-DCPP_LIBRARY_VERSION=1.0.0`; STLab's local SHA-pin config has no minimum
execution version yet. No version override is applied globally to STLab's
other dependencies.

```powershell
cmake --preset=install '-DCPP_LIBRARY_VERSION=1.0.0' '-DCPM_cpp-library_SOURCE=D:\repos\github.com\stlab\cpp-library\.claude\worktrees\execution-support'
cmake --build --preset=install
```

Run that command in execution, then install to the script's resolved prefix.
Run STLab's install preset with both local source overrides and independent
`STLAB_INSTALL=ON`/`STLAB_EXECUTION_INSTALL=ON` for combined installation.

Verify:
- Execution config contains no `find_dependency(stlab ...)`.
- STLab config contains `find_dependency(stlab-execution ...)` before targets.
- Source-build and installed names are exactly `stlab::execution`.
- Each package's installed header list has an empty intersection with the other's.
- Config paths and versions are generated from the correct project.
- Legacy target resolves the same execution dependency, not another binary.
- STLab ON/execution OFF does not install execution; downstream must supply it.
- STLab OFF/execution ON installs only execution and its config.
- Shared Windows consumers run with all required DLLs and no internal C++ runtime references.

Correct only tightly coupled export/file-set/install defects found by these cases.
Extend toolkit regression tests if the root cause is toolkit state isolation.

- [ ] **Step 4: Add source consumption with an installed execution package.**

Configure STLab with `CPM_USE_LOCAL_PACKAGES=ON` and the execution install prefix.
Ensure CPM selects the installed execution target rather than creating a second
runtime. Build/run Task 5's integration consumer and the legacy target consumer.
Do not allow source-tree include paths to mask missing installed headers.

- [ ] **Step 5: Commit the package round-trip coverage.**

```powershell
git commit -m "test: cover execution package and legacy target consumers" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

Commit execution's consumer fixture separately in its repository with the same
message and co-author trailer.

## Task 7: Documentation, platform CI, and publishable pins

**Files:**
- Execution `README.md`, `docs\doxygen\mainpage.dox`, `docs\doxygen\execution_groups.hpp`, `.github\workflows\ci.yml`, `CMakePresets.json`.
- STLab `README.md`, `CHANGES.md`, `docs\doxygen\mainpage.dox`, `docs\doxygen\stlab_groups.hpp`, `cmake\StlabDocs.cmake`.
- STLab `.github\workflows\stlab.yml`, `.github\matrix.json`, `CMakeLists.txt`.
- Toolkit README/workflow changes already committed in Tasks 1/2.
- Create STLab dated handoff `docs\superpowers\2026-10-01-execution-library-extraction-handoff.md`.

**Interfaces:**
- Documents both standalone and transitive consumption.
- Both repositories have platform ownership and package round-trip CI.
- Production dependency pins require approved published versions in delivery order.

- [ ] **Step 1: Add executable documentation examples and CI coverage.**

Execution consumption:

```cmake
CPMAddPackage("gh:stlab/stlab-execution@1.0.0")
target_link_libraries(app PRIVATE stlab::execution)
```

Treat 1.0.0 as the proposed first execution release, not an existing published
release. If the authorized release uses another version, update this example,
the dependency pin, and the minimum install requirement together.

Document unchanged include paths, standalone ownership, rebuild requirement,
separate configs, scheduler options, and canonical/legacy shared option behavior.
Show installed `find_package(stlab-execution REQUIRED)` and STLab's unchanged
`stlab::stlab` consumption.

Move execution API group documentation and add execution-specific namespace
macro expansion to its Doxygen configuration. STLab documents its retained
abstractions and links to execution documentation; remove references to groups
that its own Doxygen input no longer defines, rather than leaving broken refs.
Do not pull execution implementation sources back into STLab's documentation build.

Copy the existing platform matrix coverage to execution's workflow, adapting
targets/presets. Required execution configurations are Linux GCC/Clang,
macOS native/portable, Windows native/shared-native/shared-portable,
portable main, Qt6 main, Emscripten pthread/threadless, C++17/20, and macOS
native/portable TSan+UBSan. Preserve supported Qt5 selection/config checks.
Keep STLab's matrix as integration coverage, including shared execution modes.
Use the canonical shared option in new execution jobs; retain at least one
STLab compatibility job exercising `STLAB_CORE_SHARED=ON`.

Add package round-trip jobs on Linux and Windows, including Windows shared
runtime execution. Run toolkit fixture tests in its own workflow.

- [ ] **Step 2: Verify public branch guards and ownership.**

Search extracted and retained scheduling headers for `STLAB_TASK_SYSTEM`,
`STLAB_CORE_SHARED`, `_WIN32`, and includes of STLab config in execution.
Compare with the baseline: do not add scheduler/shared/platform branches to
public wrappers. Preserve the established timer/thread-naming exceptions.
Verify the installed header lists have no duplicate paths.

Inspect Windows execution exports: all existing `.def` C entry points must
remain present. Verify shared client imports do not reference implementation
internals; preserve the deliberately exported storage-guard contract.

- [ ] **Step 3: Run validation before completion claims.**

Run toolkit Task 2 baseline. Run execution `test`, `test-cpp17`,
`test-portable`, shared-native/shared-portable, portable-main, ASan, and package
round trips where the local host supports them. Run STLab's full C++20 suite
after its targeted integration selections and the C++17 compatibility selection.
Build docs with each repository's `docs` preset and run existing header lint/
clang-tidy selections for moved code.

For Emscripten, use installed SDK/Node and both approved presets; preserve
configuration rejection tests and cooperative future-chain integration.
Assign unavailable macOS native and sanitizer checks to CI, and report them
as pending until real CI evidence arrives. Do not substitute a compilation
result for a runtime or race check.

- [ ] **Step 4: Review implementation and resolve in-scope findings.**

Use the applicable review workflow. Check public include ownership, namespace
ambiguity/ADL, task relocation/export ABI, shutdown behavior, install metadata,
and consumer target compatibility. Fix small in-scope findings; report substantial
design conflicts rather than silently relaxing the approved requirements.

Write the dated handoff with completed work, actual command/results, unavailable
platform checks, and publication blockers. Do not claim independent releases
are published while using local CPM overrides.

- [ ] **Step 5: Commit local deliverables; publish only with authorization.**

Commit docs/CI changes separately per repository:

```powershell
git commit -m "docs: document execution ownership and platform coverage" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

Obtain authorization before remote repository creation/releases. Publish toolkit
first, replace execution's toolkit SHA with the approved toolkit release, and
verify without local source overrides. Publish execution next; replace STLab's
SHA dependency with its release pin and add the same approved minimum version to
installed dependency resolution. Finally verify STLab without overrides before
publishing STLab.

The local implementation can be reviewed before publication. Its final handoff
must distinguish working local extraction from publishable dependency integration.

## Plan self-review coverage

| Spec requirement | Plan tasks |
| --- | --- |
| Component/header ownership and acyclic dependencies | 3, 4, 5 |
| Configuration/version separation and detail ambiguity | 3, 5 |
| Existing source APIs and v2 ABI/storage guards | 3, 4, 5, 7 |
| Explicit target type and independent install state | 1, 2 |
| Static/shared selection and legacy controls/targets | 3, 5, 6 |
| Standalone/mixed test ownership | 4, 5 |
| CPM and installed consumer compatibility | 5, 6 |
| Platform/sanitizer/docs ownership | 4, 7 |
| License notices, isolated work, release ordering | 3, 7 |

Self-review before handoff: ensure the dependency SHAs are resolved during
execution rather than guessed; every code-changing task has a concrete contract
test and verification command; no scheduler behavior is redesigned; source
compatibility is not misrepresented as prebuilt C++ ABI compatibility.
