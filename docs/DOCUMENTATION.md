# Documentation in this repository

## API reference (authoritative)

- Edit **Doxygen comments** in `include/stlab/**/*.hpp` (`@file`, `@defgroup`, `///`, etc.).
- Build locally: `cmake --preset=doxygen` then `cmake --build --preset=doxygen` → `build/doxygen/html/`.
- Module overviews for Doxygen-only symbols: `docs/doxygen/stlab_groups.hpp` (not compiled).

## Website (stlab.cc)

- **Jekyll** (`docs/`): blog posts, release table, about page.
- **Doxygen HTML**: produced in CI and served at `/doxygen/` on GitHub Pages.

## Historical note

Documentation was previously maintained with [Hyde](https://github.com/adobe/hyde) under `docs/libraries/` and, after Hyde 2.0, `docs/include/stlab/`. That tree was removed once comments were migrated into headers (see [`REMAINING_HYDE_MIGRATION_PLAN.md`](REMAINING_HYDE_MIGRATION_PLAN.md)). A snapshot inventory remains in [`hyde_source_inventory.md`](hyde_source_inventory.md).

The Jekyll site still uses the [Adobe Hyde theme](https://github.com/adobe/hyde-theme) (`remote_theme` in `_config.yml`). Pages such as About and Tips may use `hyde:` front matter (`tab`, `icon`) for navigation—that is theme metadata, not the retired Hyde doc pipeline.
