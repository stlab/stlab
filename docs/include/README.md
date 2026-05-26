# API reference (removed Hyde/Jekyll mirror)

The per-symbol Markdown tree that lived under `docs/include/stlab/` was generated for the legacy Hyde/Jekyll site. That content has been retired.

**Authoritative API documentation** is now:

- **Doxygen** — build with `cmake --preset=docs` and `cmake --build --preset=docs` (HTML under `build/docs/html/`). On [stlab.cc](https://stlab.cc) it is published at `/doxygen/`.
- **Source headers** — `include/stlab/**/*.hpp` with Doxygen comments (`@file`, `@brief`, `@details`, `///`).

See [`../README.md`](../README.md) for the Jekyll blog/release site and Doxygen build steps.

A historical Hyde inventory snapshot is kept at [`../hyde_source_inventory.md`](../hyde_source_inventory.md).
