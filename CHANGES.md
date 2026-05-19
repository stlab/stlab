See [the release notes](https://github.com/stlab/stlab/releases/) for recent releases.

## v2.1.0 - 2025 - August 27
  - See [the release notes](https://github.com/stlab/stlab/releases/tag/v2.1.0) for details.

## v2.0.2 - 2025 - June 27
  - See [the release notes](https://github.com/stlab/stlab/releases/tag/v2.0.2) for details.

## v2.0.0 - 2025 - June 27
  - See [the release notes](https://github.com/stlab/stlab/releases/tag/v2.0.0) for details.

## v1.7.1 - 2022 - September 24
  - [#496] Add missing files to CMakeLists.txt


## v1.7.0 - 2022 - September 2
  - Breaking Changes
    - The build setup now requires executing CMake to generate a config.hpp file for the build. This was done to avoid ODR/API/ABI breakage from having the library self-configure differently when used by multiple sub-projects. See the [build instructions](https://github.com/stlab/stlab/runs/7903922028?check_suite_focus=true).
    - To avoid having canceled or detached tasks execute concurrently with exit, leading to undefined behavior, a [`pre_exit()`](https://stlab.cc/libraries/pre_exit.hpp) call was added which must be invoked if using the [`default_executor`](https://stlab.cc/libraries/concurrency/default_executor.hpp/) prior to returning from `main()` or invoking `exit()`. If `quick_exit()` is used, `pre_exit()` is not necessary.
  - Changes & Additions
    - The documentation site lives in `stlab/stlab/docs` (Jekyll blog and release notes). API reference is generated with Doxygen from comments in `include/stlab/**/*.hpp`.
    - `blocking_get()` and associated functions have been renamed [`await()`](https://stlab.cc/libraries/concurrency/await.hpp/) and the corresponding `blocking_get()` API is deprecated.
  - Fixed issues
    - [#408](https://github.com/stlab/stlab/issues/408) The prior version of the portable `blocking_get()` would steal tasks while blocking. This can lead to deadlocks if the task stolen is the one being waited on. The portable default executor now behaves more like the platform default executors and will add additional threads (to a limit) to the thread pool if there are no available threads and `await()` is used.
    - [#406](https://github.com/stlab/stlab/issues/406) Added support for a `main_exector` for WASM when compiled with threads enabled.
    - [#405](https://github.com/stlab/stlab/issues/405) Added official WASM support and WASM builds are now part of our CI.
    - A number of bugs were fixed with `future<>::recover()` when used with move-only types.

## v1.6.2 - 2021 - March 25
 - Fixed issues
   - [#371]:(https://github.com/stlab/stlab/issues/371)Missing include in concurrency/utility.hpp
   - The _hold for detach was being done on the executor instead of the continuation. The result was a (silenced) broken exception getting thrown if detach() is called on a ready exception.
   This would trigger an exception which is caught internally in a packaged task and dropped.
   ```cpp
    stlab::make_ready_future(stlab::immediate_executor).detach();
   ```
   - Fixed an issue with the serial queue using a "temporary" executor which was copied into the future shared state.
   - Added stealing to blocking_get<void>
   - Also cleaned up blocking_get implementation.
   - blocking_get_for() added
   - Fix for make_exceptional_future<void> and improvements blocking_get_for added
   - deprecated blocking_get() with timeout
   - Fixing bugs and naming our threads
   - Fixed issue with backoff
   - min() duration on a clock apparently is not a minimum length duration as documented, but is a negative duration... instead we us a single tick.
   - Update utility.hpp

## v1.6.1 - 2021 - February 10
 - Fixed issues
   - [#363]:(https://github.com/stlab/stlab/issues/363) Wrong version in the 1.6.0 release

## v1.6.0 - 2021 - February 9
 - Backport the library to support C++14 and later
 - Better auto-configuration - no need for compiler flags.
 - Supported for threaded wasm using the portable tasking system (auto-configured).

## v1.5.6 - 2021 - February 1
 - Fixed issues
   - [#352]:(https://github.com/stlab/stlab/issues/352) Non portable 'warning' preprocessor command
   - [#354]:(https://github.com/stlab/stlab/issues/354) Visual Studio 2017 fails to compiler async with std::function as argument

## v1.5.5 - 2021 - January 29
- Fixed issues
    - [#317](https://github.com/stlab/stlab/issues/346): Recovery continuations not invoked on broken promises
- Better conan.io support (Many thanks to Fernando Pelliccioni)
- Fix several minor scripting and configuration issues (Many thanks to Fernando Pelliccioni)
- Fix in SerialQueue UnitTest that could lead to dead lock on systems with few cores

## v1.5.3 - 2020 - September 16
- Fixed issues
    - [#312](https://github.com/stlab/stlab/issues/312): default_executor implementation for Windows has a memory leak
    - [#305](https://github.com/stlab/stlab/issues/305) stlab/concurrency/future.hpp won't compile with C++20
    - [#303](https://github.com/stlab/stlab/issues/) range based stlab::when_any data race

- Enhancement
    - Adding Forest container and algorithms

## v1.5.2 - 2020 - February 05
- Fixed issues
    - [#292](https://github.com/stlab/stlab/issues/292): What happened to our mutable lambdas?
    - [#287](https://github.com/stlab/stlab/issues/287): future's `then` doesn't fully support mutable lambdas yet

## v1.5.1 - 2019 - October 29
- Fixed issues
    - [#285](https://github.com/stlab/stlab/issues/285): steady_clock.now()
    - [#262](https://github.com/stlab/stlab/issues/282): time_point_to_FILETIME() to duration_to_FILETIME()

## v1.5.0 - 2019 - August 17
- Fixed issues
    - [#264](https://github.com/stlab/stlab/issues/264): `future::error()` interface is odd
    - [#260](https://github.com/stlab/stlab/issues/260): set_exception() should be a const member.
    - [#248](https://github.com/stlab/stlab/issues/248): Lifetime of receivers returned by operator|
    - [#243](https://github.com/stlab/stlab/issues/243): Vector of move only type passed to continuation fails to compile.

- Enhancements
    - [#269](https://github.com/stlab/stlab/issues/269): Extend the default_executor with high and low priority
    - [#262](https://github.com/stlab/stlab/issues/262): `system_timer` interface is cumbersome. Now it accepts a
      duration from now. The previously available interface with a time_point is marked as deprecated and will be removed in a future release.

## v1.4.1 - 2019 - February - 08
- Fixed Issues
    - [#234](https://github.com/stlab/stlab/issues/234): version is incorrect in stlabConfig.cmake
    - [#229](https://github.com/stlab/stlab/issues/229): SIGBUS in future destructor
    - [#220](https://github.com/stlab/stlab/issues/220): make_ready_future for move-only types

## v1.4.0 - 2018 - December - 12
- Fixed Issues
    - [#208](https://github.com/stlab/stlab/issues/208): g++8.2.0 compiling error
    - [#206](https://github.com/stlab/stlab/issues/206): channels does not allow processes as lvalues, only as rvalue
    - [#204](https://github.com/stlab/stlab/issues/204): when_all does not support move-only types
    - [#192](https://github.com/stlab/stlab/issues/192): Issues with executor_t and when_all()
    - [#188](https://github.com/stlab/stlab/issues/188): question: channels and slow processes

- Enhancements
    - [#212](https://github.com/stlab/stlab/issues/212): Join of future with move only type is missing
    - [#199](https://github.com/stlab/stlab/issues/199): add operator|() as continuable operator
    - It is now possible to create with channel<void>() a receiver<void> (not a pair<sender<void>, receiver<void>>)

- Library Changes
    - Now C++17 is required to use the channels (This change was necessary, because the code became really messy to dispatch between copyable, move-only and void types)

- Additional Comments
    - It is necessary to patch std::variant for the combination of clang version <= 7 and stdlibc++. For further details see https://stackoverflow.com/questions/46506387/getstring-for-variants-fail-under-clang-but-not-g and ./travis/patches/patch_std_variant.sh. This is because the clang bug https://bugs.llvm.org/show_bug.cgi?id=31852

## v1.3.3 - 2018 - October - 25

- Fixed Issues
   - [#154](https://github.com/stlab/stlab/issues/154) : Compilation error with gcc
   - Race condition in timed blocking_get

- Enhancements
   - The library can now be included as cmake dependency (Many thanks to Austin McCartney)

## v1.3.2 - 2018 - July - 28

- Fixed Issues
   - [#169](https://github.com/stlab/stlab/issues/169) : when_all() appears to ignore the passed-in executor

## v1.3.1 - 2018 - July - 21

- Fixed Issues
   - [#169](https://github.com/stlab/stlab/issues/169) : when_all() appears to ignore the passed-in executor
   - [#168](https://github.com/stlab/stlab/issues/168) : Main Executor requires "widgets" to be dragged into Qt build
   - [#167](https://github.com/stlab/stlab/issues/167) : Main Executor has potential segfault
   - [#166](https://github.com/stlab/stlab/issues/166) : Multiple task_system and system_timer are being created

## v1.3.0 - 2018 - July - 15

- Fixed Issues
   - [#160](https://github.com/stlab/stlab/issues/160) : main_executor for lib dispatch: `operator()` missing const qualifier
   - [#156](https://github.com/stlab/stlab/issues/156) : `set_error()` does not get called
   - [#134](https://github.com/stlab/stlab/issues/134) : Using `future<>::detach()` is a possible memory leak
   - [#107](https://github.com/stlab/stlab/issues/107) : Some channel functions have problematic names
- Library Breaking Changes
   - Concurrency Library
       - The functionality of channel's `zip` has changed. Now it behaves comparable to the ranges TS zip. The previous functionality is replaced with `zip_with`.
       - The `merge` and `join` functions are now deprecated and are replaced with `merge_channel<unordered_t>`and `merge_channel<round_robin_t>`.

## v1.2.0 - 15 June 2018

- Fixed Issues
   - [#146](https://github.com/stlab/stlab/issues/146) : when_all() does not compile with move-only objects
   - [#142](https://github.com/stlab/stlab/issues/142) : boost/variant.hpp: No such file or directory
   - [#119](https://github.com/stlab/stlab/issues/119) : blocking_get randomly does not return
- Library Additions
   - Concurrency Library
       - Adding usage of std::optional, std::variant or std::experimental::optional when compiling with C++17 mode. So the dependency to boost.optional is optional.
       - Adding co-routine support for futures. (Only for VS 2017 and clang 5.0)
       - Adding main_executor for Qt support

## v1.1.1 - 13 December 2017

- Fixed Issues
   - [#113](https://github.com/stlab/stlab/issues/113) : Reduction does not appear to work for failed futures; Many thanks to Frans-Willem!
   - [#115](https://github.com/stlab/stlab/issues/115) : Version number is forgotten in version.hpp?

## v1.1 - 28 November 2017

- Fixed Issues
   - [#94](https://github.com/stlab/stlab/issues/94) : Use of moved executor in futures.hpp:1234
   - [#93](https://github.com/stlab/stlab/issues/93) : Stack overflow with exception handling in futures
   - [#85](https://github.com/stlab/stlab/issues/85) : (non-default) Executors and `make_ready_future`
   - [#80](https://github.com/stlab/stlab/issues/80) : `stlab::async` will not accept non-const reference parameters
   - [#79](https://github.com/stlab/stlab/issues/79) : `when_all` does not support continuing futures that return `void`
   - [#69](https://github.com/stlab/stlab/issues/69) : Support move-only tasks
   - [#59](https://github.com/stlab/stlab/issues/59) : `task_system` and `notification_queue` doesn't work with move only lambda
   - [#26](https://github.com/stlab/stlab/issues/26) : channel TODO get process state under mutex?
   - [#18](https://github.com/stlab/stlab/issues/18) : future TODO `get_try()`
- Library Additions
    - Concurrency library
        - [serial queue](stlab/concurrency/serial_queue.hpp)
        - [task](stlab/concurrency/task.hpp)
        - Added feature requests [#95](https://github.com/stlab/stlab/issues/95) (flatten futures) and [#38](https://github.com/stlab/stlab/issues/38) (reduction on `future<void>`). Reduction of futures is now supported. So a `future<future<T>>` is reduced to `future<T>`. Currently
this is restricted to copyable types `T`.
    - [copy on write](stlab/copy_on_write.hpp)
    - [scope](stlab/scope.hpp)
    - [Test library](stlab/test/model.hpp)
        - annotate
        - regular
        - move-only
- Build & Test Improvements
    - CMake overhaul
    - Extended Travis CI support
    - AppVeyor support
- Breaking Changes
    - With [Issue #85](https://github.com/stlab/stlab/issues/85) the interface of `make_ready_future` and
`make_exceptional_future` now require an executor as a second parameter.

## v1.0 - 10 June 2017

- Added the concurrency library:
    - futures
    - executors
    - channels
