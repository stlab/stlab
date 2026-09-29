# Copilot instructions for STLab

## Repository overview

STLab is a C++ library in the `stlab` namespace providing concurrency primitives: futures, channels, executors, serial queues, and related utilities. The public API lives under `include/stlab/`; the implementation is concentrated in a small number of source files under `src/`. Tests are in `test/`, documentation under `docs/`, and build configuration is driven by CMake presets in `CMakePresets.json`.

This is a header-heavy library with platform-specific scheduling support. Most functionality is implemented as reusable concurrency primitives rather than application-level frameworks.

## Build, test, and lint

Use the project CMake presets rather than ad hoc build commands.

```bash
# Configure + build the default debug C++20 configuration
cmake --preset=debug-cpp20
cmake --build --preset=debug-cpp20

# Run the full test suite for that preset
ctest --preset=debug-cpp20

# Run one test binary directly
./build/debug-cpp20/test/stlab.test.future

# Filter doctest cases in a single executable
./build/debug-cpp20/test/stlab.test.future -tc="future_test_*"

# Run only matching CTest entries
ctest --preset=debug-cpp20 -R future
```

Common presets:

- `debug-cpp20` — default development configuration
- `debug-cpp17` — C++17 compatibility build
- `debug-sanitizer` — TSan + UBSan
- `debug-portable` — portable task system
- `debug-asan` — address sanitizer build
- `docs` — Doxygen API reference build
- `clang-tidy` / `clang-tidy-win64` — static analysis
- `install` — release install configuration

Formatting and linting are configured with `.clang-format` and `.clang-tidy`. The project expects the repository's standard formatting and the header lint checks used by the CI setup.

## High-level architecture

The library is organized around a small set of core concurrency concepts:

- `include/stlab/concurrency/` holds the main concurrency API:
  - `future.hpp` — `stlab::future<T>` and `stlab::package()`; lazy, value-semantic futures with chaining and coroutine support.
  - `channel.hpp` — `stlab::sender<T>` / `stlab::receiver<T>` for reactive pipelines.
  - `executor_base.hpp` / `default_executor.hpp` — executor abstractions and task dispatch.
  - `serial_queue.hpp` — serial queue built on executors.
  - `main_executor.hpp` — main-thread executor.
  - `task.hpp` — move-only callable wrapper.
  - `system_timer.hpp` — timer-based scheduling.
- Non-concurrency pieces such as `forest.hpp`, `forest_algorithms.hpp`, `copy_on_write.hpp`, and `pre_exit.hpp` are more general library utilities, but they still fit the same value-semantics and platform-aware patterns.
- `src/` contains the implementation glue; the library is intentionally compact and centered on a few patterns rather than many unrelated subsystems.
- `test/` contains component-level doctest executables such as `stlab.test.future`, `stlab.test.channel`, and `stlab.test.executor`.

The big-picture design is that higher-level concurrency abstractions are built on a small set of scheduler and executor primitives, with platform differences abstracted behind those interfaces.

## Key repository conventions

- The project uses CMake + Ninja presets in `CMakePresets.json`; prefer those when configuring or building.
- Public API and behavior are documented with Doxygen contract comments in `///` style adjacent to declarations. These comments are treated as the authoritative API contract.
- Function contracts are intentionally simple and concise; they usually include a summary and any necessary preconditions/postconditions/complexity notes.
- Tests should validate observable behavior from the public interface, not implementation details. This repository treats tests as specification checks, not white-box implementation coverage.
- The library targets C++17/20/23 and is designed to work across Linux, macOS, Windows, and Emscripten; scheduler and executor selection is intentionally platform-aware.
- Keep changes consistent with the surrounding header-only/public-API style: avoid introducing application-level patterns or ad hoc build scripts when the existing CMake/preset structure already covers the need.
- Code formatting follows `.clang-format` conventions: 100-column limit, 4-space indentation, left-aligned pointer declarators, sorted includes/using declarations.

## Documentation and API expectations

- Doxygen comments in `include/stlab/**/*.hpp` are the canonical API documentation.
- Use the repository's contract style when adding or changing declarations.
- When documenting behavior, prefer the public contract over implementation-specific details.

## Typical workflow

When making a change:

1. Build the relevant preset (`debug-cpp20` unless the change specifically targets C++17 compatibility or sanitizer coverage).
2. Run the smallest related test target or CTest filter.
3. Prefer a focused validation path over broad rebuilds; this repository's test binaries are component-oriented and easy to target.

This keeps the feedback loop tight while matching the library's build/test layout.
