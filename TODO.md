# STLab to-do / follow-ups

## Doxygen on Windows

`stlab_setup_docs()` in [`cmake/StlabDocs.cmake`](cmake/StlabDocs.cmake) sets `$ErrorActionPreference='Continue'` for the `docs` target on Windows. If builds still fail, run Doxygen on the generated `Doxyfile` under your build directory (e.g. `build/docs/Doxyfile`).
