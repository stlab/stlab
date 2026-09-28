# Task Relocation ABI Guard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make accidental structural incompatibility between executor clients and `stlab-core` fail at compile or link/load time without coupling the ABI to `sizeof(task<void() noexcept>)`.

**Architecture:** Lift the relocation operation table and model-storage dimensions into explicitly versioned v2 declarations in `task.hpp`. Export one C++-decorated static data-member specialization keyed by storage size and alignment, and require its address as an ignored argument to every executor submission call so ordinary symbol resolution enforces compatibility. Add compile-time layout canaries and an unconditional CTest that succeeds only when a deliberately mismatched guard fails to link for the expected symbol.

**Tech Stack:** C++17, CMake 3.24+, Ninja/MSVC/Apple Clang/GCC/Clang/Emscripten, doctest, CTest.

**Spec:** `docs/superpowers/specs/2026-09-25-task-relocation-abi-guard-design.md`

## Global Constraints

- Keep the existing `stlab_v2_` executor API version; do not add a manually maintained task-ABI revision.
- Do not make `sizeof(task<void() noexcept>)` part of the relocation ABI.
- Preserve allocation-free executor submission and the existing task relocation semantics.
- The only C++-decorated symbol intentionally exported from Windows `stlab-core` is the storage-keyed ABI guard.
- The guard address is passed to submission solely to retain symbol linkage; the implementation must not compare, load, or branch on it.
- The negative mismatch-link test runs in every supported platform configuration.
- Every new class, struct, and function declaration must have adjacent `///` contract documentation.
- Preserve C++17 compatibility and pass the existing C++20, ASan, shared-core, portable shared-core, macOS libdispatch, and clang-tidy configurations.

## File Structure

- `include/stlab/config.hpp.in`
  - Define the narrowly scoped `STLAB_CORE_API` import/export annotation from `STLAB_CORE_SHARED()` and `STLAB_CORE_BUILD`.
- `include/stlab/concurrency/task.hpp`
  - Own the public v2 relocation operation table, versioned storage constants, structural canaries, storage guard template, and the source-compatible `task_::concept_t` alias.
- `include/stlab/concurrency/default_executor.hpp`
  - Declare the guard argument on the three C submission functions and pass the current guard address from inline executor wrappers.
- `src/concurrency/executor_abi.cpp`
  - Define/export the current guard specialization and accept but ignore the guard argument.
- `src/CMakeLists.txt`
  - Mark only the `stlab-core` compilation with `STLAB_CORE_BUILD`.
- `test/task_test.cpp`
  - Verify the public v2 operation-table types, alias, storage constants, and model storage behavior.
- `test/executor_test.cpp`
  - Update direct ABI submissions to provide the current guard and retain exactly-once/contention coverage.
- `test/core_shared_smoke_test.cpp`
  - Reference the guard through the raw shared-core submission API, proving import/export linkage.
- `test/task_abi_mismatch_test.cpp`
  - Deliberately reference a guard specialization with the wrong storage size.
- `test/expect_task_abi_mismatch.cmake`
  - Build the excluded mismatch target, require link failure, and require diagnostics naming `task_storage_abi_guard`.
- `test/CMakeLists.txt`
  - Define the excluded mismatch executable and unconditional CTest driver.

---

### Task 1: Freeze the Versioned Task Relocation Contract

**Files:**
- Modify: `include/stlab/concurrency/task.hpp:35-230`
- Modify: `test/task_test.cpp:320-370`

**Interfaces:**
- Consumes: Existing `task_::concept_t`, `task_::small_size`, `_model`, relocation constructor, and relocation accessors.
- Produces:
  - `stlab::v2::stlab_v2_task_concept`
  - `stlab::v2::stlab_v2_task_storage_size`
  - `stlab::v2::stlab_v2_task_storage_alignment`
  - `task_<NoExcept, R, Args...>::concept_t` as an alias to `stlab_v2_task_concept`

- [ ] **Step 1: Add compile-time tests for the desired public contract**

Add `<type_traits>` if it is not already present in `test/task_test.cpp`, then add these assertions near the relocation tests:

```cpp
using noexcept_task = task<void() noexcept>;

static_assert(std::is_same_v<noexcept_task::concept_t, stlab_v2_task_concept>);
static_assert(std::is_standard_layout_v<stlab_v2_task_concept>);
static_assert(stlab_v2_task_storage_size != 0);
static_assert(stlab_v2_task_storage_alignment == alignof(std::max_align_t));
static_assert(std::is_same_v<decltype(stlab_v2_task_concept::dtor),
                             void (*)(void*) noexcept>);
static_assert(std::is_same_v<decltype(stlab_v2_task_concept::move_ctor),
                             void (*)(void*, void*) noexcept>);
static_assert(std::is_same_v<decltype(stlab_v2_task_concept::target_type),
                             const std::type_info& (*)() noexcept>);
static_assert(std::is_same_v<decltype(stlab_v2_task_concept::pointer),
                             void* (*)(void*) noexcept>);
static_assert(std::is_same_v<decltype(stlab_v2_task_concept::const_pointer),
                             const void* (*)(const void*) noexcept>);
```

Add a test proving the published storage dimensions still accept the existing inline relocation model:

```cpp
TEST_CASE("task relocation storage contract matches live task storage") {
    task<void() noexcept> source{[]() noexcept {}};

    REQUIRE(reinterpret_cast<std::uintptr_t>(source.relocation_source()) %
                stlab_v2_task_storage_alignment ==
            0);
    REQUIRE(stlab_v2_task_storage_size >= sizeof(void*));
}
```

- [ ] **Step 2: Run the task target and verify the contract test fails to compile**

Run:

```powershell
cmake --build build\pr-debug-cpp20 --target stlab.test.task --parallel
```

Expected: compilation fails because `stlab_v2_task_concept`,
`stlab_v2_task_storage_size`, and `stlab_v2_task_storage_alignment` are not declared.

- [ ] **Step 3: Declare the versioned operation table and storage constants**

In `task.hpp`, before `task_`, add:

```cpp
/// Versioned operation table used to relocate a task target across the shared executor ABI.
struct stlab_v2_task_concept {
    using dtor_t = void (*)(void*) noexcept;
    using move_ctor_t = void (*)(void*, void*) noexcept;
    using target_type_t = const std::type_info& (*)() noexcept;
    using pointer_t = void* (*)(void*) noexcept;
    using const_pointer_t = const void* (*)(const void*) noexcept;

    dtor_t dtor;
    move_ctor_t move_ctor;
    target_type_t target_type;
    pointer_t pointer;
    const_pointer_t const_pointer;
};

/// Number of bytes reserved for a v2 task's relocatable inline model.
inline constexpr std::size_t stlab_v2_task_storage_size =
    std::max(alignof(std::max_align_t) * 2, sizeof(void*) * 8) -
    std::max(alignof(std::max_align_t), sizeof(void*) * 2);

/// Alignment required by a v2 task's relocatable inline model.
inline constexpr std::size_t stlab_v2_task_storage_alignment = alignof(std::max_align_t);
```

Add a private constexpr alignment helper adjacent to the structural assertions:

```cpp
/// Rounds `offset` up to the next address satisfying `alignment`.
constexpr auto stlab_v2_align_offset(std::size_t offset, std::size_t alignment) noexcept
    -> std::size_t {
    return (offset + alignment - 1) / alignment * alignment;
}
```

Enumerate the v2 member sequence independently:

```cpp
inline constexpr auto stlab_v2_task_concept_move_ctor_offset =
    stlab_v2_align_offset(sizeof(stlab_v2_task_concept::dtor_t),
                          alignof(stlab_v2_task_concept::move_ctor_t));
inline constexpr auto stlab_v2_task_concept_target_type_offset =
    stlab_v2_align_offset(stlab_v2_task_concept_move_ctor_offset +
                              sizeof(stlab_v2_task_concept::move_ctor_t),
                          alignof(stlab_v2_task_concept::target_type_t));
inline constexpr auto stlab_v2_task_concept_pointer_offset =
    stlab_v2_align_offset(stlab_v2_task_concept_target_type_offset +
                              sizeof(stlab_v2_task_concept::target_type_t),
                          alignof(stlab_v2_task_concept::pointer_t));
inline constexpr auto stlab_v2_task_concept_const_pointer_offset =
    stlab_v2_align_offset(stlab_v2_task_concept_pointer_offset +
                              sizeof(stlab_v2_task_concept::pointer_t),
                          alignof(stlab_v2_task_concept::const_pointer_t));
inline constexpr auto stlab_v2_task_concept_alignment =
    std::max({alignof(stlab_v2_task_concept::dtor_t),
              alignof(stlab_v2_task_concept::move_ctor_t),
              alignof(stlab_v2_task_concept::target_type_t),
              alignof(stlab_v2_task_concept::pointer_t),
              alignof(stlab_v2_task_concept::const_pointer_t)});
```

Add compile-time canaries:

```cpp
static_assert(std::is_standard_layout_v<stlab_v2_task_concept>);
static_assert(alignof(stlab_v2_task_concept) == stlab_v2_task_concept_alignment);
static_assert(std::is_same_v<decltype(stlab_v2_task_concept::dtor),
                             stlab_v2_task_concept::dtor_t>);
static_assert(std::is_same_v<decltype(stlab_v2_task_concept::move_ctor),
                             stlab_v2_task_concept::move_ctor_t>);
static_assert(std::is_same_v<decltype(stlab_v2_task_concept::target_type),
                             stlab_v2_task_concept::target_type_t>);
static_assert(std::is_same_v<decltype(stlab_v2_task_concept::pointer),
                             stlab_v2_task_concept::pointer_t>);
static_assert(std::is_same_v<decltype(stlab_v2_task_concept::const_pointer),
                             stlab_v2_task_concept::const_pointer_t>);
static_assert(offsetof(stlab_v2_task_concept, dtor) == 0);
static_assert(offsetof(stlab_v2_task_concept, move_ctor) ==
              stlab_v2_task_concept_move_ctor_offset);
static_assert(offsetof(stlab_v2_task_concept, target_type) ==
              stlab_v2_task_concept_target_type_offset);
static_assert(offsetof(stlab_v2_task_concept, pointer) ==
              stlab_v2_task_concept_pointer_offset);
static_assert(offsetof(stlab_v2_task_concept, const_pointer) ==
              stlab_v2_task_concept_const_pointer_offset);
static_assert(sizeof(stlab_v2_task_concept) ==
              stlab_v2_align_offset(stlab_v2_task_concept_const_pointer_offset +
                                        sizeof(stlab_v2_task_concept::const_pointer_t),
                                    alignof(stlab_v2_task_concept)));
```

These assertions must remain adjacent to the v2 declaration with a comment stating that an
incompatible operation-table change requires a new versioned type rather than updating the
assertions to accept a reordered v2 type.

- [ ] **Step 4: Make `task_` consume the versioned contract**

Replace the nested struct with:

```cpp
using concept_t = stlab_v2_task_concept;
```

Remove `max_align` and `small_size`. Change model selection and storage to:

```cpp
alignas(stlab_v2_task_storage_alignment)
    std::array<unsigned char, stlab_v2_task_storage_size> _model;
```

```cpp
using model_t =
    std::conditional_t<(sizeof(small_t) <= stlab_v2_task_storage_size) &&
                           (alignof(small_t) <= stlab_v2_task_storage_alignment),
                       small_t, large_t>;
```

Do not alter `task_` member ordering or relocation semantics in this task.

- [ ] **Step 5: Run task tests in C++17 and C++20**

Run:

```powershell
cmake --build build\debug-cpp17 --target stlab.test.task --parallel
ctest --test-dir build\debug-cpp17 -R "^stlab.test.task$" --output-on-failure
cmake --build build\pr-debug-cpp20 --target stlab.test.task --parallel
ctest --test-dir build\pr-debug-cpp20 -R "^stlab.test.task$" --output-on-failure
```

Expected: both task test executions pass with no compiler warnings.

- [ ] **Step 6: Commit the versioned relocation contract**

```powershell
git add include\stlab\concurrency\task.hpp test\task_test.cpp
git commit -m "refactor: version task relocation contract" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 2: Enforce Storage Compatibility Through Submission Linkage

**Files:**
- Modify: `include/stlab/config.hpp.in:20-40`
- Modify: `include/stlab/concurrency/task.hpp:35-100`
- Modify: `include/stlab/concurrency/default_executor.hpp:40-135`
- Modify: `src/CMakeLists.txt:1-8`
- Modify: `src/concurrency/executor_abi.cpp:40-60,690-730`
- Modify: `test/executor_test.cpp:20-245`
- Modify: `test/core_shared_smoke_test.cpp:40-60`

**Interfaces:**
- Consumes:
  - `stlab_v2_task_storage_size`
  - `stlab_v2_task_storage_alignment`
  - `stlab_v2_task_concept`
- Produces:
  - `STLAB_CORE_API`
  - `detail::task_storage_abi_guard<Size, Alignment>::value`
  - `detail::current_task_storage_abi_guard`
  - Four-argument guarded `stlab_v2_{default,high,low}_executor_submit`

- [ ] **Step 1: Update positive callers first so the build fails on the missing guard API**

In `executor_test.cpp`, define:

```cpp
/// Returns the linker guard for the current task relocation storage ABI.
auto current_task_abi_guard() noexcept -> const unsigned char* {
    return &stlab::detail::current_task_storage_abi_guard::value;
}
```

Add `current_task_abi_guard()` as the first argument to every direct
`stlab_v2_{default,high,low}_executor_submit` call.

Update `core_shared_smoke_test.cpp` similarly:

```cpp
stlab_v2_default_executor_submit(
    &stlab::detail::current_task_storage_abi_guard::value, t.relocation_concept(),
    t.relocation_invoke(), t.relocation_source());
```

- [ ] **Step 2: Run executor targets and verify the new calls fail to compile**

Run:

```powershell
cmake --build build\pr-debug-cpp20 --target stlab.test.executor --parallel
cmake --build build\pr-shared-core --target stlab.test.core_shared_smoke --parallel
```

Expected: compilation fails because `current_task_storage_abi_guard` is absent and the submission
functions still accept three arguments.

- [ ] **Step 3: Add the scoped core import/export macro**

In `config.hpp.in`, add:

```cpp
#if defined(_WIN32) && STLAB_CORE_SHARED()
#if defined(STLAB_CORE_BUILD)
#define STLAB_CORE_API __declspec(dllexport)
#else
#define STLAB_CORE_API __declspec(dllimport)
#endif
#else
#define STLAB_CORE_API
#endif
```

In `src/CMakeLists.txt`, extend the private definitions:

```cmake
target_compile_definitions(stlab-core PRIVATE
  STLAB_CORE_BUILD
  $<$<CXX_COMPILER_ID:MSVC>:NOMINMAX>)
```

Do not add `STLAB_CORE_BUILD` to `stlab`, tests, or consumers.

- [ ] **Step 4: Declare the storage-keyed guard**

In `task.hpp`, after the storage constants, add:

```cpp
namespace detail {

/// Linker guard keyed by the v2 task relocation storage contract.
template <std::size_t Size, std::size_t Alignment>
struct task_storage_abi_guard {
    STLAB_CORE_API static const unsigned char value;
};

/// Guard specialization required by the current v2 task relocation storage contract.
using current_task_storage_abi_guard =
    task_storage_abi_guard<stlab_v2_task_storage_size, stlab_v2_task_storage_alignment>;

} // namespace detail
```

Keep this declaration in the versioned inline namespace so an eventual API-major change receives a
distinct decorated namespace as well as distinct storage template arguments.

- [ ] **Step 5: Define the one exported guard specialization**

In `executor_abi.cpp`, before the anonymous namespace and after entering `stlab`'s version namespace
and `detail`, add:

```cpp
template <>
STLAB_CORE_API const unsigned char
    task_storage_abi_guard<stlab_v2_task_storage_size,
                           stlab_v2_task_storage_alignment>::value = 0;
```

Verify the compiler accepts the import/export annotation on the explicit static data-member
specialization in static, Windows DLL, GCC/Clang shared, and Emscripten configurations. Do not
export the primary template or any mismatched specialization.

- [ ] **Step 6: Add the mandatory ignored guard argument**

Change each public declaration in `default_executor.hpp` to:

```cpp
extern "C" void stlab_v2_default_executor_submit(
    const unsigned char* task_abi_guard, const stlab_v2_task_concept* vtable,
    stlab_v2_task_proc invoke, void* source) noexcept;
```

Apply the same leading parameter to high and low submissions. Update the contract:

```cpp
/// - Precondition: `task_abi_guard` points to
///   `detail::current_task_storage_abi_guard::value`.
```

Use `stlab_v2_task_concept` directly; remove `stlab_v2_task_concept_t`.

In `submit_executor_proc`, obtain:

```cpp
const auto* task_abi_guard = &current_task_storage_abi_guard::value;
```

Pass it as the first argument in every priority branch.

Change each definition in `executor_abi.cpp` to accept but not name the first parameter:

```cpp
extern "C" void stlab_v2_default_executor_submit(
    const unsigned char* /*task_abi_guard*/, const stlab_v2_task_concept* vtable,
    stlab_v2_task_proc invoke, void* source) noexcept {
```

Do not add an assertion, comparison, or dereference of the guard.

- [ ] **Step 7: Run positive static and shared-core tests**

Run:

```powershell
cmake --build build\pr-debug-cpp20 --target stlab.test.executor stlab.test.task --parallel
ctest --test-dir build\pr-debug-cpp20 -R "stlab.test.(executor|task)" --output-on-failure
cmake --build build\pr-shared-core --target stlab.test.executor stlab.test.core_shared_smoke --parallel
ctest --test-dir build\pr-shared-core -R "stlab.test.(executor|core_shared_smoke)" --output-on-failure
cmake --build build\pr-shared-core-portable --target stlab.test.executor stlab.test.core_shared_smoke stlab.test.portable_shared_smoke --parallel
ctest --test-dir build\pr-shared-core-portable -R "stlab.test.(executor|core_shared_smoke|portable_shared_smoke)" --output-on-failure
```

Expected: all selected tests pass. On Windows shared-core builds, successful linking of
`stlab.test.core_shared_smoke` proves the decorated guard is exported and importable despite not
being listed in `src/stlab.def`.

- [ ] **Step 8: Inspect the Windows shared DLL export**

Run from an x64 Visual Studio developer environment:

```powershell
dumpbin /exports build\pr-shared-core\stlab-core.dll |
    Select-String "task_storage_abi_guard"
```

Expected: exactly one decorated export containing `task_storage_abi_guard` and the current size and
alignment template arguments.

Also verify the restricted C exports remain:

```powershell
dumpbin /exports build\pr-shared-core\stlab-core.dll |
    Select-String "stlab_v2_|stlab_pre_exit|stlab_at_pre_exit"
```

Expected: the existing C symbols are present; no unrelated C++ implementation symbols are
exported.

- [ ] **Step 9: Commit guarded executor submission**

```powershell
git add include\stlab\config.hpp.in include\stlab\concurrency\task.hpp `
    include\stlab\concurrency\default_executor.hpp src\CMakeLists.txt `
    src\concurrency\executor_abi.cpp test\executor_test.cpp `
    test\core_shared_smoke_test.cpp
git commit -m "feat: guard task relocation ABI linkage" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 3: Prove Mismatched Storage Cannot Link

**Files:**
- Create: `test/task_abi_mismatch_test.cpp`
- Create: `test/expect_task_abi_mismatch.cmake`
- Modify: `test/CMakeLists.txt:45-100`

**Interfaces:**
- Consumes:
  - `detail::task_storage_abi_guard<Size, Alignment>::value`
  - `stlab_v2_task_storage_size`
  - `stlab_v2_task_storage_alignment`
  - `stlab-core`
- Produces:
  - Excluded target `stlab.test.task_abi_mismatch`
  - CTest `stlab.test.task_abi_mismatch_link`

- [ ] **Step 1: Write the deliberately incompatible consumer**

Create `test/task_abi_mismatch_test.cpp`:

```cpp
/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include <stlab/concurrency/task.hpp>

/// References an intentionally unavailable task storage ABI guard specialization.
int main() {
    using mismatched_guard =
        stlab::detail::task_storage_abi_guard<stlab::stlab_v2_task_storage_size + 1,
                                              stlab::stlab_v2_task_storage_alignment>;
    auto* volatile guard = &mismatched_guard::value;
    return guard == nullptr;
}
```

The volatile local makes the source intent obvious, but correctness must rely on the unresolved
external symbol, not volatility.

- [ ] **Step 2: Add the excluded target without the expected-failure wrapper**

In `test/CMakeLists.txt`, add:

```cmake
add_executable(stlab.test.task_abi_mismatch EXCLUDE_FROM_ALL
  task_abi_mismatch_test.cpp)
target_link_libraries(stlab.test.task_abi_mismatch PRIVATE stlab-core)
```

- [ ] **Step 3: Build the mismatch target and verify it fails for the guard symbol**

Run:

```powershell
cmake --build build\pr-debug-cpp20 --target stlab.test.task_abi_mismatch --parallel
```

Expected: the link fails, and the diagnostic contains `task_storage_abi_guard`. If it compiles or
fails for an unrelated include/configuration reason, fix the test setup before proceeding.

- [ ] **Step 4: Write a strict expected-link-failure CMake driver**

Create `test/expect_task_abi_mismatch.cmake`:

```cmake
if(NOT DEFINED build_dir)
  message(FATAL_ERROR "build_dir is required")
endif()

set(command "${CMAKE_COMMAND}" --build "${build_dir}" --target stlab.test.task_abi_mismatch)
if(DEFINED config AND NOT config STREQUAL "")
  list(APPEND command --config "${config}")
endif()

execute_process(
  COMMAND ${command}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)

string(CONCAT output "${stdout}" "\n" "${stderr}")

if(result EQUAL 0)
  message(FATAL_ERROR "Mismatched task storage ABI linked successfully:\n${output}")
endif()

if(NOT output MATCHES "task_storage_abi_guard")
  message(FATAL_ERROR
    "Mismatch target failed for an unexpected reason; guard symbol was absent:\n${output}")
endif()
```

This script exits successfully only when the real platform linker rejects the expected guard
symbol.

- [ ] **Step 5: Register the unconditional CTest**

Add:

```cmake
add_test(
  NAME stlab.test.task_abi_mismatch_link
  COMMAND ${CMAKE_COMMAND}
    -Dbuild_dir=${PROJECT_BINARY_DIR}
    -Dconfig=$<CONFIG>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_task_abi_mismatch.cmake)
```

Do not wrap this test in `WIN32`, `STLAB_CORE_SHARED`, task-system, compiler, or native-platform
conditionals.

- [ ] **Step 6: Run the negative test in static, shared, and portable shared configurations**

Run:

```powershell
ctest --test-dir build\pr-debug-cpp20 -R "^stlab.test.task_abi_mismatch_link$" -V
ctest --test-dir build\pr-shared-core -R "^stlab.test.task_abi_mismatch_link$" -V
ctest --test-dir build\pr-shared-core-portable -R "^stlab.test.task_abi_mismatch_link$" -V
```

Expected: CTest reports PASS in all three configurations while the nested target build log shows
an unresolved `task_storage_abi_guard` specialization.

- [ ] **Step 7: Verify a matching guard still links**

Temporarily change the mismatch source locally from
`stlab_v2_task_storage_size + 1` to `stlab_v2_task_storage_size`, then run:

```powershell
cmake --build build\pr-debug-cpp20 --target stlab.test.task_abi_mismatch --parallel
```

Expected: the target links successfully. Revert only this temporary one-token test mutation and
rerun:

```powershell
ctest --test-dir build\pr-debug-cpp20 -R "^stlab.test.task_abi_mismatch_link$" -V
```

Expected: PASS, again showing the intended unresolved mismatched symbol. This red/green reversal
proves the negative test is testing storage-key mismatch rather than a generic target failure.

- [ ] **Step 8: Commit mismatch-link coverage**

```powershell
git add test\CMakeLists.txt test\task_abi_mismatch_test.cpp `
    test\expect_task_abi_mismatch.cmake
git commit -m "test: reject mismatched task relocation storage" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 4: Run the Cross-Platform ABI Verification Gate

**Files:**
- Modify only if validation exposes a directly related portability defect.

**Interfaces:**
- Consumes: Completed versioned task contract, guarded submission API, and mismatch-link CTest.
- Produces: Verified implementation ready to update PR #604.

- [ ] **Step 1: Run formatting and diff checks**

Run:

```powershell
clang-format -i include\stlab\concurrency\task.hpp `
    include\stlab\concurrency\default_executor.hpp src\concurrency\executor_abi.cpp `
    test\task_test.cpp test\executor_test.cpp test\core_shared_smoke_test.cpp `
    test\task_abi_mismatch_test.cpp
git --no-pager diff --check
```

Expected: no formatting or whitespace errors.

- [ ] **Step 2: Run clang-tidy**

Run:

```powershell
cmake --preset=clang-tidy-win64
cmake --build --preset=clang-tidy-win64
```

Expected: build succeeds with zero clang-tidy warnings.

- [ ] **Step 3: Run C++17, C++20, and ASan suites**

Run:

```powershell
cmake --build --preset=debug-cpp17
ctest --test-dir build\debug-cpp17 --output-on-failure
```

From an x64 Visual Studio developer environment:

```powershell
cmake --build build\pr-debug-cpp20 --parallel
ctest --test-dir build\pr-debug-cpp20 --output-on-failure
cmake --build --preset=debug-asan
ctest --preset=debug-asan --output-on-failure
```

Expected: all suites pass, including `stlab.test.task_abi_mismatch_link`.

- [ ] **Step 4: Run native and portable shared-core suites**

From the same x64 developer environment:

```powershell
cmake --build build\pr-shared-core --parallel
ctest --test-dir build\pr-shared-core --output-on-failure
cmake --build build\pr-shared-core-portable --parallel
ctest --test-dir build\pr-shared-core-portable --output-on-failure
```

Expected: all tests pass, including shared smoke tests and the mismatch-link test.

- [ ] **Step 5: Push and verify non-Windows CI**

Push the implementation branch:

```powershell
git push
```

Watch PR #604:

```powershell
$runId = gh run list --branch windows-dll-executor-abi --limit 1 `
    --json databaseId --jq '.[0].databaseId'
gh run watch $runId --exit-status
gh pr checks 604
```

Expected: Linux GCC, Linux Clang, macOS Apple Clang, macOS TSan, macOS portable TSan,
Emscripten, Windows static, Windows shared, Windows portable shared, Doxygen, and matrix generation
all pass. Confirm the unconditional mismatch-link CTest executes successfully in each build/test
job rather than being filtered out.

- [ ] **Step 6: Resolve any directly related portability failure in its owning task**

If Step 5 fails, do not make a catch-all portability commit. Identify whether the failure belongs
to the versioned contract (Task 1), guard/export integration (Task 2), or negative-link harness
(Task 3), add a reproducing test in that task's files, apply the minimal fix, rerun Task 4 from
Step 1, and amend neither existing commit. Create a new commit using the owning task's commit
subject with a `fix:` prefix, then push and re-run Step 5.

If Step 5 passes, create no additional commit.
