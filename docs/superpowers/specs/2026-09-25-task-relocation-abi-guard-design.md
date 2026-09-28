# Task Relocation ABI Guard Design

- Status: Approved for implementation planning
- Date: 2026-09-25
- Extends: [Windows DLL-Safe Executor ABI Design](2026-09-22-windows-dll-executor-abi-design.md)

## Problem

The versioned executor submission API relocates a target out of
`task<void() noexcept>` by passing an operation table and the address of the task's model storage
to `stlab-core`. The current API aliases its operation-table type directly to
`task<void() noexcept>::concept_t`, making a private nested implementation type part of the
versioned ABI without clearly marking that constraint.

The executor ABI does not depend on the complete layout or size of `task<void() noexcept>`. It
depends on only:

1. The layout and semantics of the target operation table.
2. The size and alignment of the model storage into which `move_ctor` constructs the relocated
   target.

An accidental incompatible change must fail at compile or link/load time. The protection must not
depend on manually incrementing a second ABI revision beyond the existing `stlab_v2_` version, and
it must not add a runtime compatibility check.

## Goals

1. Make the operation table an explicitly versioned ABI type rather than a nested `task_`
   implementation type.
2. Make both sides of executor submission use one versioned model-storage contract.
3. Automatically produce an incompatible linker symbol when the model-storage size or alignment
   differs.
4. Ensure every executor submission creates a retained reference to the linker guard without a
   separate runtime branch or function call.
5. Detect accidental operation-table layout changes at compile time.
6. Keep `task_` free to change its overall size, member ordering, and non-relocation implementation
   details.

## Non-goals

- Preserving binary compatibility after an intentional incompatible v2 operation-table change.
  Such a change requires a new versioned ABI type and submission API.
- Detecting semantic implementation changes that preserve the operation-table signatures, layout,
  storage size, and storage alignment.
- Making `sizeof(task<void() noexcept>)` part of the executor ABI.
- Negotiating ABI versions at runtime.

## Versioned Operation Table

Move the target operation table out of `task_` and declare it as an explicitly versioned type in
the `stlab::v2` inline namespace:

```cpp
struct stlab_v2_task_concept {
    void (*dtor)(void*) noexcept;
    void (*move_ctor)(void*, void*) noexcept;
    const std::type_info& (*target_type)() noexcept;
    void* (*pointer)(void*) noexcept;
    const void* (*const_pointer)(const void*) noexcept;
};
```

`task_::concept_t` remains as a source-compatible alias:

```cpp
using concept_t = stlab_v2_task_concept;
```

The ABI submission declarations use `stlab_v2_task_concept` directly. Naming the type independently
from `task_` makes its stability requirement visible and allows `task_` internals to evolve around
it.

Compile-time canaries verify that the type is standard-layout and that its size, alignment, member
offsets, and member function-pointer types remain fixed. The expected layout is expressed in
terms of the target platform's function-pointer sizes and alignments so the checks remain valid on
supported 32-bit and 64-bit targets, but it must independently enumerate the v2 member sequence.
An intentional incompatible change must introduce a new versioned operation-table type rather than
updating the v2 assertions.

## Versioned Model Storage

Declare versioned constants for the relocation model storage:

```cpp
inline constexpr std::size_t stlab_v2_task_storage_size = /* existing small_size formula */;
inline constexpr std::size_t stlab_v2_task_storage_alignment = alignof(std::max_align_t);
```

`task_::_model` uses these constants for its array size and alignment. The core's destination task
storage therefore follows the same public versioned contract without depending on
`sizeof(task<void() noexcept>)`.

The constants may evaluate differently on different architectures. That is intentional: compatible
components for the same target architecture produce the same guard symbol, while incompatible
storage contracts do not.

## Automatic Link Guard

Declare an internal C++ template keyed by the versioned storage constants:

```cpp
template <std::size_t Size, std::size_t Alignment>
struct task_storage_abi_guard {
    static const unsigned char value;
};

using current_task_storage_abi_guard =
    task_storage_abi_guard<stlab_v2_task_storage_size, stlab_v2_task_storage_alignment>;
```

`stlab-core` defines and exports only the specialization for the storage contract with which it was
built. The symbol is an intentional exception to the otherwise `extern "C"` DLL export policy. Its
C++ decorated name contains the size and alignment automatically, so no secondary manual ABI
revision is required.

Each `stlab_v2_*_executor_submit` function gains a leading guard-address argument. The inline C++
executor wrapper passes:

```cpp
&current_task_storage_abi_guard::value
```

The implementation does not read or compare the argument. Passing it to an external function makes
the reference observable and prevents compilers, COMDAT elimination, or dead stripping from
discarding it. Compatibility is enforced entirely by ordinary static-linker or dynamic-loader
symbol resolution:

- Matching headers and `stlab-core` resolve the same decorated guard symbol.
- New headers with an old static or import library fail to link when storage properties differ.
- An old shared-library client loaded with an incompatible `stlab-core` fails to resolve its guard
  import.
- Rebuilding all components against the changed storage contract succeeds without renaming the
  existing `stlab_v2_` submission symbols.

The additional pointer argument is the retention mechanism, not a runtime check. It adds no
comparison, branch, allocation, or extra call.

## Export and Installation

The versioned operation-table type, storage constants, guard declaration, and import/export macro
live in the public task/executor headers needed by clients.

On Windows shared-core builds, the instantiated guard symbol is exported through a narrowly scoped
`STLAB_CORE_API`-style import/export annotation. It is the sole intentional C++-decorated exception
to the `.def`-listed C ABI. The implementation must preserve the existing restriction that
unrelated C++ symbols are not exported.

Static builds define the same guard specialization in `stlab-core`; consumers receive the same
link-time mismatch protection.

## Validation

1. **Operation-table compile-time canaries**
   - Verify standard-layout.
   - Verify fixed size and alignment.
   - Verify every member offset.
   - Verify every member has the exact v2 function-pointer type.

2. **Positive submission coverage**
   - Existing executor tests submit through all three inline wrappers.
   - Windows shared-core smoke tests prove the guard is exported and imported.
   - Static builds prove the guard specialization is available from `stlab-core`.

3. **Unconditional negative link test**
   - Build a small consumer that references
     `task_storage_abi_guard<stlab_v2_task_storage_size + 1,
     stlab_v2_task_storage_alignment>::value`.
   - Treat the expected unresolved-symbol result as test success.
   - Run this test unconditionally on every supported platform configuration.

4. **Dead-strip protection**
   - Positive shared/static consumer tests inspect or exercise the submission wrapper, ensuring the
     guard reference survives optimized linking.
   - Where practical, platform tooling (`dumpbin`, `nm`, or `otool`) confirms the consumer imports
     or references the expected guard symbol.

## Compatibility Rule

Changes to `task_` do not affect the executor ABI unless they modify:

- `stlab_v2_task_concept`;
- `stlab_v2_task_storage_size`;
- `stlab_v2_task_storage_alignment`; or
- the documented relocation semantics of those operations.

An intentional incompatible change to the operation table or relocation semantics requires a new
versioned ABI type and corresponding API. A storage-size or alignment change is automatically
detected by the decorated guard symbol and requires rebuilding all participating components.
