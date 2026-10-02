# STLab execution library extraction

## Status and approved decisions

Architectural design approved in conversation on 2026-10-01. The written
specification requires user review before implementation planning.

| Decision | Approved choice |
| --- | --- |
| New repository/package | `stlab/stlab-execution` / `stlab-execution` |
| Local repository | `D:\repos\github.com\stlab\stlab-execution` |
| CMake target | `stlab::execution` |
| Build infrastructure | `cpp-library`, with narrowly scoped improvements |
| Compatibility | Existing include paths and `stlab::` public APIs remain canonical |
| Header ownership | Exactly one package owns each header; no forwarding copies |
| Binary compatibility | Recompilation is required; existing v2 C ABI is preserved |
| Dependency direction | STLab depends on execution; execution never depends on STLab |

## Purpose and non-goals

Extract the execution foundation from STLab into an independently consumable
library. STLab acquires it through CPM and exposes it as a public dependency, so
existing source consumers retain access to the execution APIs.

This is not a scheduler, timer, task, or shutdown redesign. Do not move futures
and channels into execution, refactor serial queues to eliminate their future
dependency, rename public APIs, or add a framework layer. Do not retain duplicate
implementations or build a separate runtime and wrapper package.

The selected name describes execution rather than suggesting that all
concurrency abstractions are included. A runtime-only extraction would leave
standalone C++ consumers dependent on STLab; a broader extraction would move
substantially more than the requested foundation.

## Component and header ownership

| Owner | Public headers/components |
| --- | --- |
| Execution | `concurrency/task.hpp`, `concurrency/executor_base.hpp` |
| Execution | `concurrency/default_executor.hpp`, `concurrency/main_executor.hpp`, `concurrency/immediate_executor.hpp` |
| Execution | `concurrency/system_timer.hpp`, `concurrency/set_current_thread_name.hpp`, `pre_exit.hpp` |
| STLab | Futures, channels, await helpers, ready futures, serial queues |
| STLab | Concurrency traits, tuple algorithms, utility and umbrella headers |
| STLab | General algorithms, containers, functional/memory/scope utilities, test utilities |
| STLab | `config.hpp`, `version.hpp` |

Paths in this table are relative to `include/stlab`. Existing paths remain the
canonical paths in the package that owns them. STLab obtains execution headers
transitively rather than maintaining wrappers.

Execution owns all compiled task/main-executor and timer backends, pre-exit
registration, core shutdown coordination, and their internal headers. Move
execution-only private implementation headers out of STLab as well. Platform
discovery, backend selection, core export declarations, and execution-related
CMake support belong to execution.

`pre_exit` must move with execution because timer cancellation and executor
retirement share its process lifecycle. `executor_base` depends only on tasks
and timers and belongs in the foundation. Serial queues stay in STLab because
their current public implementation depends on futures.

The dependency graph is:

```text
STLab -> stlab-execution -> platform/system libraries
STLab -> existing independent utility dependencies
```

Execution must configure, compile, test, and install without STLab sources,
headers, targets, or packages. Do not move general STLab utilities into execution
unless they are required by the approved execution boundary; the current
execution header graph does not require them.

## Configuration and independent versioning

Execution owns a new generated `stlab/execution/config.hpp`. Extracted public
and private headers include this configuration instead of `stlab/config.hpp`.
It contains execution version and inline-namespace macros, scheduler/thread/main
configuration, shared-library export configuration, and common compiler-feature
helpers needed by the lower layer.

STLab retains its generated `stlab/config.hpp`, which includes execution's
configuration and defines STLab release-version and coroutine settings.
`stlab/version.hpp` continues to report the STLab version, not execution's version.
Execution's configuration must not define STLab release-version namespace macros.
Existing execution-related configuration macros remain available through
`stlab/config.hpp` for source compatibility.

Both packages derive their release versions independently using `cpp-library`.
Execution's inline namespace uses an execution-specific name rather than sharing
STLab's release namespace. The public unqualified spellings, such as
`stlab::task` and `stlab::default_executor`, remain unchanged.

Use a distinct execution detail namespace so including both packages cannot
make `stlab::detail` ambiguous. Update references coupled to the moved internals
without exposing new implementation dependencies.

Preserve existing stable v2 C entry points, task relocation layout, operation
tables, and storage guards. Changing extracted C++ inline namespaces and library
filenames does not promise compatibility with prebuilt C++ clients. Consumers
must rebuild. Do not transport C++ exceptions or ownership-sensitive C++ objects
across the process-shared scheduling ABI.

## Build and dependency integration

Use `project(execution)` and `cpp_library_setup(NAMESPACE stlab ...)` to create
target `execution`, alias `stlab::execution`, and package `stlab-execution`.
This keeps source-build and installed target names identical. The minimum
supported language remains C++17.

STLab removes its locally compiled core and links publicly to execution fetched
through a pinned `CPMAddPackage`. Retain its existing unrelated dependencies.
Use CPM local-source overrides for development; production configuration must
not embed developer-specific paths or depend on moving branches.

Preserve existing scheduler options and defaults. Introduce
`STLAB_EXECUTION_SHARED` as the canonical execution shared-library control,
accepting `STLAB_CORE_SHARED` as a compatibility spelling. Preserve the existing
effective shared/static selection when the new option is not specified.
On a fresh configuration, conflicting supplied settings fail with a diagnostic.
On reconfiguration, the spelling changed since the last resolved configuration
wins over an unchanged cached counterpart. This preserves single-option changes
through either spelling. CMake cannot distinguish an unchanged cached value from
the same value redundantly supplied again on the command line; in that case,
the changed spelling still wins. This policy was approved during implementation
review on 2026-10-01.

Execution shared/static selection must not change the parent project's
`BUILD_SHARED_LIBS`. Preserve Windows native and portable shared-execution
support, exports, task-storage checks, and runtime DLL deployment.

Preserve legacy `stlab-core` and `stlab::stlab-core` link targets in STLab's
source-build compatibility surface. Preserve `stlab::stlab-core` for installed
STLab consumers as a compatibility target referring to execution. These targets
do not build, install, or own another core binary. Standalone execution consumers
use `stlab::execution`.

Execution owns its system dependencies and validates backend combinations.
STLab does not independently generate a conflicting scheduler configuration.
An installed STLab config resolves `stlab-execution` using `find_dependency`
before loading STLab targets. Installing both packages to one prefix must not
overwrite any header with a different package's definition.

## Required cpp-library improvements

The local toolkit repository is `D:\repos\github.com\stlab\cpp-library`.
Develop changes in an isolated worktree, without altering unrelated work.

| Improvement | Contract |
| --- | --- |
| Explicit compiled target type | Allow callers to choose static/shared without changing `BUILD_SHARED_LIBS`; retain current behavior when omitted |
| Independent install controls | Support package-specific controls without breaking existing namespace-level options/defaults |
| Package-scoped deferred install metadata | Nested setup calls cannot overwrite another package's name, version, paths, or validation state |

Use the toolkit's normal setup/install entry points rather than adding
execution-specific CMake bypasses. Keep improvements backward-compatible and
document their public options. Test them with the existing CMake script and
consumer integration infrastructure, including nested package installation.

## Behavior and error handling

Preserve existing public contracts and ABI safety requirements:

- Default/high/low and main submissions keep their existing scheduling behavior.
  Never execute queued work inline as a scheduling fallback.
- Preserve task small-buffer behavior and allocation-free ABI transport; do not
  introduce per-submission allocation or unnecessary ownership transfer.
- Preserve explicit timer resource-error reporting, failed-submission ownership,
  delay normalization, and exactly-once invocation/destruction accounting.
- Preserve native timer cancellation and executor draining, application handler
  ordering, and the main executor's existing lifecycle.
- Preserve cooperative Emscripten asynchronous retirement and completion-fence
  semantics; pthread-enabled Emscripten remains distinct.
- Preserve ABI guard rejection and violated-precondition diagnostics.

Backend selection and process-shared state remain compiled behind the versioned
ABI. Do not add platform/task-system/shared-core branching to public scheduling
wrappers. This extraction does not expand existing platform-dependent timer and
thread-naming contracts.

## Tests, documentation, and CI ownership

Move execution-only tests, examples, and API documentation with their components.
Adapt their test harness so it depends only on execution and test infrastructure.
Tests needing futures, channels, or other retained STLab components stay in
STLab, even if their current CMake link line mentions only `stlab-core`.

STLab keeps integration coverage demonstrating that the public dependency
preserves existing source usage. Each repository owns documentation and CI for
its components. Existing Doxygen contracts remain authoritative and move with
the declarations; document package consumption, configuration ownership, and the
rebuild requirement.

| Acceptance surface | Required evidence |
| --- | --- |
| Standalone execution | Each public header compiles independently without STLab; task/executor/timer/lifecycle tests link only execution |
| STLab source compatibility | Existing include paths and public names compile; future/channel/serial-queue tests preserve behavior |
| CPM consumption | Local-source override consumers configure and build without hardcoded developer paths |
| Installed consumption | Execution-only and STLab consumers work through `find_package`; target names match CPM builds |
| Nested installation | Both packages install to one prefix with separate configs and no overlapping header ownership |
| Windows shared execution | Native and portable configurations, DLL clients, task-storage guards, lifecycle, and DLL deployment |
| Other supported platforms | Linux portable; macOS native/portable sanitizer coverage; Emscripten pthread/cooperative |
| Language compatibility | C++17 and C++20 build/test coverage |
| Toolkit regressions | Existing tests plus explicit target type, independent install controls, and nested metadata isolation |

Use existing presets and the smallest relevant selections during development.
Escalate to the relevant component suites and supported CI matrix before release.
Unavailable local platforms and sanitizer execution remain explicit CI
requirements, never reported as locally passing.

## Delivery and review gates

1. Review and approve this written specification.
2. Create the implementation plan, covering toolkit work, extraction, and STLab
   integration in dependency order.
3. Implement and verify toolkit changes in isolation.
4. Create the new local execution repository at the approved path, preserving
   copyright notices and Boost Software License coverage for moved sources.
5. Verify standalone execution and STLab integration using local CPM overrides.
6. Publish pinned dependencies in order: `cpp-library`, `stlab-execution`, STLab.

Remote repository creation and release publishing are separate authorized
operations, not implied by preparing local code. Publishing the toolkit is
necessary before a standalone execution release can pin the required toolkit
support; publishing execution precedes STLab's production dependency pin.

No implementation starts before written-spec review and implementation planning.
