# Software Technology Lab (STLab) Library Source Code Repository

ASL libraries will be migrated here in the `stlab` namespace, new libraries will be created here.

**STLab** is a set of C++ libraries (namespace `stlab`) under the [Boost Software License 1.0](https://www.boost.org/LICENSE_1_0.txt). This repository provides headers, CMake packages, tests, and API reference. Versioned releases are on [GitHub Releases](https://github.com/stlab/stlab/releases).

# Using the STLab Libraries

The recommended approach to using the libraries is to use [CPM](https://github.com/cpm-cmake/CPM.cmake) and add the following to your `CMakeLists.txt`:

```cmake
CPMAddPackage("gh:stlab/stlab@2.1.2")
target_link_libraries(app PRIVATE stlab::stlab)
```

(replace `2.1.2` with the [desired version](https://github.com/stlab/stlab/releases)).

## Branch states

- **`main`:** [![Build and Tests](https://github.com/stlab/stlab/actions/workflows/stlab.yml/badge.svg)](https://github.com/stlab/stlab/actions/workflows/stlab.yml)

## Content

### [Concurrency](https://stlab.cc/doxygen/group__stlab__concurrency.html)

This library provides futures and channels, high-level abstractions for implementing algorithms that ease the use of multiple CPU cores while minimizing contention. This library solves several problems of the C++11 and C++17 TS futures.

## Documentation

Documentation for STLab-owned APIs is available on the [STLab home page](http://stlab.cc).

API reference (Doxygen, including [doxygen-awesome-css](https://github.com/jothepro/doxygen-awesome-css)) is built with `-DBUILD_DOCS=ON` or the CMake preset `docs` (`cmake --preset=docs` then `cmake --build --preset=docs`). Output is under `build/docs/html` (preset `doxygen` is an alias with output in `build/doxygen/html`). On GitHub Pages, it is published under `/doxygen/` next to the Jekyll blog site. See [`docs/DOCUMENTATION.md`](docs/DOCUMENTATION.md).

Formulas in comments use Doxygen’s math markup (e.g. `\f$O(n)\f$` inline, `\f[` / `\f]` for display); HTML output enables **MathJax 3** so no LaTeX toolchain is required locally or in CI.

Release changelogs are listed in [CHANGES.md](CHANGES.md).

## Tested on

- Linux with GCC 11
- Linux with Clang 14
- MacOS 11 with Apple-clang 13.0.0
- Windows with Visual Studio 16

## Requirements

- A standards-compliant C++17, C++20, or C++23 compiler
- **Building** the library requires CMake 3.24 or later; this repository's
  schema-8 presets require CMake 3.28 or later (execution's schema-5 presets use 3.24).
- **Testing or developing** uses Ninja and doctest, fetched by CMake.

## Building

STLab is a standard CMake project. See the [running CMake](https://cmake.org/runningcmake) tutorial
for an introduction to this tool.

STLab publicly depends on [stlab-execution](https://github.com/stlab/stlab-execution).
That package owns tasks, executors, timers, `pre_exit`, their existing public header paths,
and the compiled execution runtime. Link `stlab::stlab` to obtain both layers; standalone
execution clients link `stlab::execution`. The legacy `stlab-core` and `stlab::stlab-core`
targets are INTERFACE compatibility targets referring to execution, not a second binary.
Consumers must rebuild; existing v2 C entry points and source spellings are preserved.
Futures, channels, await helpers, ready futures, serial queues, and general utilities
remain STLab-owned. Independent inline namespaces preserve source-level names,
not the C++ ABI of previously built clients.

`stlab/config.hpp` includes `stlab/execution/config.hpp`. STLab owns only its release
version/namespace and coroutine configuration; execution owns backend selection, export,
and common feature macros. The execution SHA pin is a development dependency available
on a public review branch, not a release version; cpp-library uses release 5.5.0. Local CPM source
overrides must use `:PATH` cache types on Windows.

**Release blocker:** approved release pins and the matching installed dependency
requirement still need toolkit → execution → STLab publication. The development
commits are remotely available for review and CI. The historical release example above
does not represent a published extraction release. No execution minimum version
is guessed. Current local validation uses generic developer overrides:

```powershell
cmake --preset=debug-cpp20 -DCPM_cpp-library_SOURCE:PATH=<toolkit-checkout> -DCPM_stlab-execution_SOURCE:PATH=<execution-checkout>
cmake --build --preset=debug-cpp20
ctest --preset=debug-cpp20
```

Source CPM consumers continue linking `stlab::stlab`. Installed consumers use:

```cmake
find_package(stlab CONFIG REQUIRED)
target_link_libraries(app PRIVATE stlab::stlab)
```

Set `CMAKE_PREFIX_PATH` to the prefix containing both packages, or supply an
independently installed execution prefix. Execution's README and local Doxygen
build contain the lower-level contracts. The repository is public, but there is no
published execution documentation site yet.

### Preparation

1. Create a build directory outside this library's source tree. In this guide, we'll use a sibling
   directory called `BUILD`.

1. Install CMake >= 3.28 for this repository's presets (direct configuration requires
   >= 3.24). If you are on Debian or Ubuntu Linux you may need to use
   `snap` to find one that's new enough.

1. If you are using MSVC, you may need to set environment variables appropriately for your target
   architecture by invoking `VCVARSALL.BAT` with an appropriate option.

### Configure

Run CMake in the root directory of this project, setting `./build` as your build directory. The
basis of your command will be

```

cmake -S . -B ../BUILD -DCMAKE_BUILD_TYPE=# SEE BELOW

```

but there are other options you may need to append in order to be successful. Among them:

- `-DCMAKE_BUILD_TYPE=`[**`Release`**|`Debug`] to build the given configuration (required unless you're using Visual Studio or another multi-config generator).
- `-DCMAKE_CXX_STANDARD=`[`17`|**`20`**|`23`] to build with compliance to the given C++ standard.
- `-DBUILD_TESTING=`[`ON`, `OFF`] turn off if you intend to build, but not test, this library.

STLab and execution configuration options (backend options are resolved only by execution):

- `-DSTLAB_MAIN_EXECUTOR=`[`qt5`, `qt6`, `libdispatch`, `emscripten`, `portable`, `none`] to select the main executor to use. Default is platform dependent; `portable` (an stlab-owned queue serviced by `stlab::main_executor_run()`) is opt-in. Windows has no process main queue, so the default there is `none` unless Qt is found.
- `-DSTLAB_TASK_POOL_MAXIMUM=`[`integer`] Define the maximum number threads in the task pool. Default of zero implies a pool size of std::thread::hardware_concurrency. Non-zero implies STLAB_TASK_SYSTEM=portable.
- `-DSTLAB_NO_STD_COROUTINES=`[`ON`, **`OFF`**] to suppress usage of standard coroutines. Useful for non-conforming compilers.
- `-DSTLAB_THREAD_SYSTEM=`[`win32`, `pthread`, `pthread-emscripten`, `pthread-apple`, `none`] to select the thread system to use. Default is platform dependent.
- `-DSTLAB_TASK_SYSTEM=`[`portable`, `libdispatch`, `windows`, `emscripten`] to select the task system to use. Default is platform dependent; `emscripten` is the cooperative, threadless backend.
- `-DSTLAB_EMSCRIPTEN_PTHREADS=`[**`ON`**, `OFF`] controls Emscripten pthread support. `OFF` selects `STLAB_THREAD_SYSTEM=none`, `STLAB_TASK_SYSTEM=emscripten`, and `STLAB_MAIN_EXECUTOR=emscripten`. Conflicting explicit selections and a nonzero task-pool maximum are rejected. The compiler's pthread flags must match this option.
- `-DBUILD_SHARED_LIBS=`[`ON`, **`OFF`**] selects shared or static libraries when building
  from source, including execution on Windows. Execution derives its DLL import/export
  configuration from its actual library type; no execution-specific linkage option is needed.
  The former `STLAB_EXECUTION_SHARED` and `STLAB_CORE_SHARED` CMake options are no longer used.
- `-DSTLAB_INSTALL=`[`ON`, `OFF`] controls STLab installation, independently of
  `STLAB_EXECUTION_INSTALL`. Enable both when installing both packages from this source build.

An installed execution package retains the linkage selected by the developer who built it.
Client libraries link its exported `stlab::execution` target without choosing or changing
that linkage; a client's `BUILD_SHARED_LIBS` only controls libraries it builds from source.
To use static STLab with shared execution, build/install execution with `BUILD_SHARED_LIBS=ON`,
then configure STLab with `BUILD_SHARED_LIBS=OFF`, `CPM_USE_LOCAL_PACKAGES=ON`, and
`CMAKE_PREFIX_PATH` pointing to that installation. STLab and its clients automatically receive
execution's installed configuration and runtime dependency. Windows clients using shared
execution must deploy `execution.dll`; scheduling and process-shared state cross its versioned C ABI.

### Emscripten cooperative execution and timers

With pthreads disabled, high/default/low executors all submit asynchronously to the
main runtime event loop. Tasks must return to that loop to permit other tasks, timers,
and coroutine continuations to run; synchronous busy polling prevents progress.
`await()` accepts an already-ready future but terminates on a non-ready future.
`await_for()` ignores its timeout and immediately returns the supplied future,
preserving a pending result for later polling or continuation attachment.

Timer, main-executor, and shutdown contracts and standalone tests belong to
[stlab-execution](https://github.com/stlab/stlab-execution). STLab retains futures/await
integration coverage, including future continuations completing before cooperative shutdown's
main-queue exit fence. Do not exit immediately on return from threadless `pre_exit()`;
retirement is asynchronous.

The local `cmake/Platform/Emscripten-STLab.cmake` remains a minimal compatibility bootstrap:
CMake needs its SDK/compiler setup before `project()` and CPM can fetch execution. It preserves
the SDK-selected flags and emulator without duplicating backend detection or runtime ownership.

We also suggest the installation of [Ninja](https://ninja-build.org/) and its use by adding
`-GNinja` to your cmake command line… but ninja is not required.

A typical invocation might look like this:

```

cmake -S . -B ../BUILD -GNinja -DCMAKE_CXX_STANDARD=17 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF

```

If you organize the build directory into subdirectories you can support multiple configurations.

```

rm -rf ../builds/portable
cmake -S . -B ../builds/portable -GXcode -DCMAKE_CXX_STANDARD=17 -DBUILD_TESTING=ON -DSTLAB_TASK_SYSTEM=portable -DCMAKE_OSX_DEPLOYMENT_TARGET=macosx14.4

```

### Build

If your configuration command was successful, go to your build directory (`cd ../BUILD`) and invoke:

```

cmake --build .

```

#### Installation (optional)

Installation is optional and typically not required when using CPM. If you need to install the library (e.g., for system-wide deployment or use with a package manager):

```bash
# Build and install to default system location
cmake --preset=install -DSTLAB_INSTALL=ON -DSTLAB_EXECUTION_INSTALL=ON
cmake --build --preset=install
cmake --install build/install

# Install to custom prefix
cmake --install build/install --prefix /opt/mylib
```

The `install` preset enables `CPM_USE_LOCAL_PACKAGES`, which verifies your generated Config.cmake works correctly. See the [CPM.cmake documentation](https://github.com/cpm-cmake/CPM.cmake#cpm_use_local_packages) for more about using installed packages.

The installed `stlabConfig.cmake` resolves `stlab-execution` before loading `stlabTargets`.
It exports `stlab::stlab` and the INTERFACE `stlab::stlab-core` compatibility target. STLab and
execution have disjoint installed header sets and independent package versions.

## Testing

Running the tests in the `BUILD` directory is as simple as invoking

```

ctest -C Debug

```

or

```

ctest -C Release

```

depending on which configuration (`CMAKE_BUILD_TYPE) you choose to build.

The focused native source-build configuration regression runs with:

```powershell
ctest --preset=debug-cpp20 -R "^stlab.config.execution_coexistence$" --output-on-failure
```

It independently builds execution in C++17/20 and STLab in C++17/20 with
`STLAB_NO_STD_COROUTINES=ON/OFF`, then compiles both configuration include orders
and the future API with warnings as errors. Only STLab defines the coroutine
macro; execution's headers also compile in C++17 regardless of its build standard.
This check uses generated source-build headers, not installed-consumer fixtures.

Native package round trips use separate, fresh install-preset children:

```powershell
cmake --preset=test-packages
ctest --preset=test-packages
```

Use `test-packages-shared` or `test-packages-portable-shared` for shared execution.
To test local dependency changes, supply the same `:PATH` CPM source overrides.
The verifier checks independent install controls, disjoint installed header file sets,
installed consumers, and source STLab using an imported installed execution package.
Children disable `BUILD_TESTING`; logs and package evidence remain under
`build/<preset>/package-test/execution-packages`. On Windows use an x64 developer
environment; runtime DLLs are copied beside each consumer before it runs.
The shared test/fixture helper supports CMake 3.24 and skips empty static DLL lists
(the repository's preset schema still requires CMake 3.28).
Runtime imports use the C ABI; the existing decorated task-storage data guard is
retained to enforce link-time ABI mismatch rejection.

## Generating Documentation

For generating the documentation, see the [README.md](docs/README.md) in the `docs` directory.

```

```
