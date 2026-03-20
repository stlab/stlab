# STLab to-do / follow-ups

## cpp-library: Doxygen `docs` target on Windows

The `docs` custom target from `_cpp_library_setup_docs()` (in [stlab/cpp-library](https://github.com/stlab/cpp-library)) runs Doxygen under PowerShell with stderr merged (`2>&1`). On Windows, Doxygen warnings on stderr can surface as **NativeCommandError** and cause `cmake --build … --target docs` to fail even when `doxygen.exe` exits 0.

**Follow-up:** adjust cpp-library (e.g. `cmake/cpp-library-docs.cmake`) so the Windows path does not treat Doxygen’s stderr as a terminating PowerShell error—options include running via `cmd /c`, setting `$ErrorActionPreference = 'Continue'`, or invoking Doxygen without merging stderr into the error stream.

Until then, on Windows you can run Doxygen directly on the generated `Doxyfile` under your build directory (e.g. `build/doxygen/Doxyfile`).
