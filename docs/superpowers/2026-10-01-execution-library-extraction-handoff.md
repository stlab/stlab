# Execution extraction — local Task 7 handoff, 2026-10-01

Local validation spans 2026-10-01/02; the filename retains the approved plan date.

**Linkage policy update, 2026-10-06:** the shared-option and alias-reconciliation
decisions recorded below are historical and superseded. `BUILD_SHARED_LIBS` is
the only source-build linkage selection; execution derives its export configuration
from the actual target type. Imported execution targets retain their installed
linkage regardless of client settings. STLab CI and package consumers now exercise
the standard selection and independent installed-runtime combinations. See the
updated extraction design and root README for the current contract.

## Delivery boundary and actual commits

All seven local implementation tasks were accepted by the controller. Final
cross-repository review passed with no Critical or Important findings; publication
remains blocked as described below. During local Task 7 implementation no push,
tag, remote repository creation, release, branch cleanup, or ledger deletion
was performed. The later authorized PR operations are recorded below.

- Toolkit: `cd552af39ee4c71fe4342dfba4517c70dddba0b3`, unchanged in Task 7.
- Execution: `8d23dd07161782ac3d756b2883a7c77c952faa6c`, local docs/CI commit.
- STLab predecessor: `7c51c4f6882ca91ad124ea19f73c3cbeb09044ec`.
  This handoff belongs to its following Task 7 commit; use `git log` for that
  commit's identity rather than recording a self-referential SHA.
- STLab's production execution dependency now pins the actual execution commit
  above. Both dependency SHAs are unpublished and are explicit publication blockers.

## Finished implementation and ownership

Execution independently owns task, executor, timer, thread-naming, and pre-exit
APIs, their compiled backends, configuration, tests, installation, and canonical
Doxygen groups. STLab retains futures, channels, await/ready-future helpers,
serial queues, and general utilities. Canonical `stlab/` include paths and
public `stlab::` spellings are preserved with no forwarding header copies.

Rebuild all C++ consumers. The execution-specific inline namespace, independent
versioning, and binary filename do not promise compatibility with old prebuilt
C++ clients. The existing v2 C runtime ABI and link-time task-storage data guard
remain. STLab's legacy core targets are INTERFACE links to one execution runtime,
not another binary. Only STLab owns coroutine policy.

The README examples distinguish current typed local CPM overrides, installed
consumption, and a **proposed future** execution 1.0.0 release example. There is
no published execution documentation site and no guessed release floor.
STLab Doxygen does not import execution headers or implementation sources.

Execution CI reuses STLab's JSON matrix/platform workflow mechanics and actual
presets: 14 platform builds, two macOS TSan+UBSan variants, four package jobs,
clang-tidy, docs, and matrix generation (23 job instances). Platforms include
GCC/Clang, C++17/20, macOS native/portable, Windows static/native and canonical
shared native/portable, portable main, Qt6/Qt5 main, and both Emscripten runtimes.
STLab has 13 integration builds, both existing macOS sanitizer jobs, four package
jobs, docs, and matrix generation (21 instances). Its native Windows shared
job retains `STLAB_CORE_SHARED=ON`; portable/shared uses the canonical option.
Emscripten configure/build/test retain the activated SDK environment.

These are prepared jobs, **not CI passes**. At local Task 7 delivery the dependency
SHAs were unavailable remotely; the subsequently authorized branch pushes allow
hosted CI to run. Toolkit's Task 1/2 fixture workflow remains unchanged.

## Final validation and evidence

Definitive commands, outputs, counts, and final snapshot checks are recorded in
the ignored artifact
`.superpowers/sdd/2026-10-01-execution-library-extraction/task-7-report.md`.
Expanded native commands and complete logs are under `build/task7-validation`;
Linux/Node binaries and package prefixes remain off the mounted source filesystem
under `/home/sparent/stlab-build/*-task7`.

Windows execution: native C++20/C++17, portable, shared native/portable,
portable main, and ASan full builds/suites; three standalone package presets.
STLab: full debug C++20 all-target build/CTest, related C++17 selection,
and static/shared-native/shared-portable package and runtime integration.
Toolkit: exact Task 2 nested/type/dependency/provider/version script baseline.
Windows execution suites passed 116/116 entries plus three package tests;
Windows STLab passed 14/14 full C++20, 7/7 related C++17, and 15/15 package/
integration entries. GNU execution's four native configurations passed 60/60;
GNU STLab static/shared package/integration passed 12/12, with fresh installs.
Linux and SDK 6.0.10 / Node 24.19.0 runtime evidence and final counts are
reported in the task artifact; no earlier-task tests are counted as new evidence.
Both execution Node suites passed (pthread 18/18, threadless 23/23), including
configuration rejection and the actual eight-case core timer harness.
STLab Node pthread integration passed 5/5; threadless future-chain/await/coroutine
and nonblocking-wait rejection passed 5/5. Final matrix: **278/278 top-level
CTest entries across 25 configurations**, with 61 package consumer processes
inside the package tests. Refresh/verbose repeats are not added to that total.

Both docs presets generated actual HTML with existing Doxygen 1.18.0.
Execution contains all ten canonical API group pages and its own namespace
PREDEFINED expansion; STLab documents only its retained groups. On Windows,
STLab docs use the existing preset with the host-specific thread-system override.

Existing clang-tidy selectors cover moved runtime and public-header translation
units plus retained dependency integration. They complete but are not warning-free:
inherited modernization suggestions and intentional include-only smoke translation
units remain baseline limitations, not suppressed or broadly rewritten.
The extraction-coupled missing lint configuration and smoke diagnostic/include
issues were corrected surgically. Actionlint validates both workflows; its
missing executable was restored portably in the ignored build directory only.

Public scheduling guards match the pre-extraction baseline. Execution does not
include STLab config. Installed file sets are disjoint (22 STLab / 10 execution).
Both Windows shared DLLs expose exactly the ten required C entry points plus the
preserved decorated **data** guard. Package verifiers enforce C-only runtime
function imports; that explicit data exception does not permit internal C++
runtime function imports.

## Ledger rulings, deliberate costs, and compatibility boundaries

1. **Fresh x64 validation cache:** the original debug cache selected Hostx86/x86
   despite x64 vcvars libraries. Preserve it; use a fresh or consistent VS18 x64
   cache with the same preset. Cost: additional configure/build time, no source
   or system-configuration change.
2. **Typed local overrides:** Windows CPM overrides use `:PATH`, not raw
   backslash STRING values, so FetchContent code receives normalized paths.
   Cost if wrong: configuration failure; no production developer path workaround.
3. **Changed shared spelling wins:** contradictory fresh canonical/legacy
   inputs fail; on reconfiguration the changed spelling wins over its stale
   cached counterpart, even when that counterpart is supplied redundantly.
   Both caches reconcile without changing parent `BUILD_SHARED_LIBS`.
   Cost: CMake cannot distinguish redundant input from unchanged cache state;
   pre-fix contradictory caches without resolution history need reconciliation.
4. **Owned timer-test state:** accepted callbacks can outlive failed assertions.
   Migrated tests therefore retain callback-owned promises/counters rather than
   borrowed stack state. Cost: test-only allocations/fixture differences; no
   production allocation or runtime behavior change.
5. **Exclusive coroutine ownership:** execution neither defines nor resolves
   STLab coroutine policy. Mismatched generated-config/include-order regressions
   verify the independent C++17/20 boundary. No masking of STLab's option.
6. **Task-storage guard exception:** preserving the approved link-time data
   guard means one decorated C++ data import remains. Runtime calls exclusively
   cross the versioned C ABI. Removing that guard or requiring literally C-only
   data imports would require another design decision, not silent relaxation.
7. **Deliberate Emscripten bootstrap duplication:** STLab keeps minimal SDK
   setup because CMake needs the compiler before `project()`/CPM fetch. Backend
   discovery and implementation remain in execution. Cost: maintaining that
   small bootstrap consistently, not a second scheduler.
8. **Independent test DLL helpers:** each repository owns its CMake-3.24-safe
   empty-list-safe copy helper; it is not an installed/shared test dependency.
   Actual minimum-version evidence remains in Task 6; unchanged helper tests
   are not falsely counted as new Task 7 runs.
9. **Preset floors remain explicit:** execution schema 5 requires 3.24; STLab's
   schema 8 requires 3.28, although direct package configuration supports 3.24.
   No floor was raised or hidden to bypass validation.
10. **Short package validation paths:** a deep initial Task 7 root triggered
    Windows RC2136 during CMake's compiler probe. Reuse short scoped x64 roots
    rather than changing fixtures, flags, or global long-path policy. Cost:
    retained failed-run evidence plus one rerun.
11. **Installed-source development bridge:** CPM infers version zero from a SHA,
    rejecting independently installed major versions. STLab performs unversioned
    CONFIG GLOBAL lookup only when local-package consumption is enabled without
    an explicit execution source override, then registers the actual found
    version. Cost: a development-pin compatibility bridge, not a guessed release
    floor; publication must assign and verify the actual version requirement.

## Pending checks and publication order

Controller verification after final review repeated the full STLab C++20 suite
(14/14), execution header/timer selection (2/2), and toolkit nested-install/type
fixtures (10/10 and 11/11). These repeats are not added to the matrix totals.
Review acceptance is recorded in the ignored `task-7-final-review.md` artifact.
An untracked execution-root `compile_commands.json` appeared during review with
an unidentified producer; it was left untouched. Tracked changes are committed.

macOS native/libdispatch, native/portable TSan+UBSan, and Qt5/Qt6 runtime checks
remain **CI pending**, not local passes. Compilation is not a substitute for
runtime, lifecycle, or race evidence. Lint baseline warnings remain explicit.
Historical version/no-tag and utility toolkit-version comparison warnings remain;
no fake toolkit semantic version or remote-fetch success is claimed.

Publication needs separate authorization:

1. Publish toolkit support first. Replace execution's unpublished toolkit SHA
   with the actual approved release; verify execution without source overrides.
2. Publish execution next. Its 1.0.0 README example is only a proposed version.
3. Replace STLab's SHA with the actual execution release and the same approved
   minimum installed dependency requirement; verify STLab without overrides.
4. Only then publish STLab.

## Authorized PR follow-up

The user selected pushing/opening PRs and separately authorized creation of public
`stlab/stlab-execution`. Toolkit support is reviewed in `stlab/cpp-library#27`;
the full execution library is reviewed in `stlab/stlab-execution#1`.
Execution has an empty `main` bootstrap and a full-library `execution-library`
snapshot branch. Original implementation history remains on `extract-execution`;
the initial review snapshot has an identical tree, without rewritten commits.
Both development dependency SHAs are now remotely reachable. They are not released
versions, and no tag or release was authorized or created.

The historical remote-fetch validation below used the execution development pin
`261602164de790e072f7b09b07698b0148363f4d`; its review branch snapshot is
`68419c4c42a726935cbecebc684ea7940e0e1a34`, with an identical tree.
A fresh Windows C++20 configuration without either CPM source override fetched
toolkit `cd552af` and execution `2616021` from their GitHub remotes; dependency,
future, and timer integration passed 3/3. Cache and Git identities were checked.
Expected no-release-tag/toolkit-development-version warnings remain.

These SHAs record that validation checkpoint, not the current dependency selection.
The root `CMakeLists.txt` is authoritative for the current execution pin; the
2026-10-06 linkage update selected `b71e7fc85e4bd8312e66095cb79cdde2aba2f0ad`.

Initial hosted toolkit CI passed. Execution's hosted native/portable macOS,
both macOS TSan+UBSan variants, Qt5/Qt6, lint/docs, and package jobs passed on
the review snapshot. Other execution jobs and STLab hosted checks were still
pending at this follow-up; this is not a claim of a completed overall CI matrix.

Local extraction is approved. Preserve branches, caches, reports, and the
progress ledger while the PRs and release gates are resolved.
