# Software Technology Lab (STLab) Library Source Code Repository

ASL libraries will be migrated here in the `stlab` namespace, new libraries will be created here.

**STLab** is a set of C++ libraries (namespace `stlab`) under the [Boost Software License 1.0](https://www.boost.org/LICENSE_1_0.txt). This repository provides headers, CMake packages, tests, and API reference. Versioned releases are on [GitHub Releases](https://github.com/stlab/stlab/releases).

# Using the STLab Libraries

The recommended approach to using the libraries is to use [CPM](https://github.com/cpm-cmake/CPM.cmake) and add the following to your `CMakeLists.txt`:

```cmake
CPMAddPackage("gh:stlab/stlab@2.1.2")
```

(replace `2.1.2` with the [desired version](https://github.com/stlab/stlab/releases)).

## Branch states

- **`main`:** [![Build and Tests](https://github.com/stlab/stlab/actions/workflows/stlab.yml/badge.svg)](https://github.com/stlab/stlab/actions/workflows/stlab.yml)

## Content

### [Concurrency](https://stlab.cc/doxygen/group__stlab__concurrency.html)

This library provides futures and channels, high-level abstractions for implementing algorithms that ease the use of multiple CPU cores while minimizing contention. This library solves several problems of the C++11 and C++17 TS futures.

## Documentation

The complete documentation is available on the [STLab home page](http://stlab.cc).

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
- **Building** the library requires CMake 3.23 or later
- **Testing or developing** the library requires Boost.Test >= 1.74.0

## Building

STLab is a standard CMake project. See the [running CMake](https://cmake.org/runningcmake) tutorial
for an introduction to this tool.

### Preparation

1. Create a build directory outside this library's source tree. In this guide, we'll use a sibling
   directory called `BUILD`.

1. Install a version of CMake >= 3.23. If you are on Debian or Ubuntu Linux you may need to use
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

STlab specific configuration options:

- `-DSTLAB_MAIN_EXECUTOR=`[`qt5`, `qt6`, `libdispatch`, `emscripten`, `portable`, `none`] to select the main executor to use. Default is platform dependent; `portable` (an stlab-owned queue serviced by `stlab::main_executor_run()`) is opt-in. Windows has no process main queue, so the default there is `none` unless Qt is found.
- `-DSTLAB_TASK_POOL_MAXIMUM=`[`integer`] Define the maximum number threads in the task pool. Default of zero implies a pool size of std::thread::hardware_concurrency. Non-zero implies STLAB_TASK_SYSTEM=portable.
- `-DSTLAB_NO_STD_COROUTINES=`[`ON`, **`OFF`**] to suppress usage of standard coroutines. Useful for non-conforming compilers.
- `-DSTLAB_THREAD_SYSTEM=`[`win32`, `pthread`, `pthread-emscripten`, `pthread-apple`, `none`] to select the thread system to use. Default is platform dependent.
- `-DSTLAB_TASK_SYSTEM=`[`portable`, `libdispatch`, `windows`, `emscripten`] to select the task system to use. Default is platform dependent; `emscripten` is the cooperative, threadless backend.
- `-DSTLAB_EMSCRIPTEN_PTHREADS=`[**`ON`**, `OFF`] controls Emscripten pthread support. `OFF` selects `STLAB_THREAD_SYSTEM=none`, `STLAB_TASK_SYSTEM=emscripten`, and `STLAB_MAIN_EXECUTOR=emscripten`. Conflicting explicit selections and a nonzero task-pool maximum are rejected. The compiler's pthread flags must match this option.

### Emscripten cooperative execution and timers

With pthreads disabled, high/default/low executors all submit asynchronously to the
main runtime event loop. Tasks must return to that loop to permit other tasks, timers,
and coroutine continuations to run; synchronous busy polling prevents progress.
`await()` accepts an already-ready future but terminates on a non-ready future.
`await_for()` ignores its timeout and immediately returns the supplied future,
preserving a pending result for later polling or continuation attachment.

`system_timer` accepts either a duration or a supported
`std::chrono::steady_clock::time_point` deadline. Nonpositive delays and past deadlines
schedule asynchronously without delay. Timer scheduling and state live in `stlab-core`
behind its versioned C ABI. Resource failures are reported as client-side
`std::bad_alloc` or `std::system_error`; C++ exceptions do not cross the ABI.

Emscripten timers use `emscripten_set_timeout()` on the main runtime thread, including
submissions from pthreads. Native backends retain their platform timer execution
placement. `pre_exit()` cancels pending timers, destroys their captures, and waits
for callbacks executing on other threads before draining the default executor.
The timer and default executor share one teardown handler, registered on their first
use. Application shutdown handlers registered afterward run before core teardown in
the usual reverse-registration order; handlers that release running callbacks must
use that ordering. Do not call `pre_exit()` from a native timer callback
that shutdown would need to join. An Emscripten timer callback may call `pre_exit()`,
but must return before final runtime shutdown. Schedule `emscripten_force_exit()` in
a separate non-`noexcept` callback and link with `-sEXIT_RUNTIME=1`.

The main executor stays available after core shutdown, including the portable main
backend. A native shutdown task can retire producers and then enqueue an exit fence:

```cpp
stlab::main_executor([]() noexcept {
    stlab::pre_exit();
    stlab::main_executor([]() noexcept { std::exit(EXIT_SUCCESS); });
});
```

The exit task follows main-queue work submitted by the retired timers and default
executor. Producers must not synchronously wait for main-queue progress while
`pre_exit()` occupies the main thread. This is a FIFO fence, not a transitive drain:
earlier main tasks can still enqueue additional work behind the exit task.
On Emscripten, the final main task schedules the non-`noexcept` force-exit callback
described above instead of calling `std::exit()` inside the executor task.

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
cmake --preset=install
cmake --build --preset=install
cmake --install build/install

# Install to custom prefix
cmake --install build/install --prefix /opt/mylib
```

The `install` preset enables `CPM_USE_LOCAL_PACKAGES`, which verifies your generated Config.cmake works correctly. See the [CPM.cmake documentation](https://github.com/cpm-cmake/CPM.cmake#cpm_use_local_packages) for more about using installed packages.

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

## Generating Documentation

For generating the documentation, see the [README.md](docs/README.md) in the `docs` directory.

```

```
