# Documentation in this repository



## API reference (authoritative)



- Edit **Doxygen comments** in `include/stlab/**/*.hpp` (`@file`, `@defgroup`, `///`, etc.).

- Build locally: `cmake --preset=doxygen` then `cmake --build --preset=doxygen` → `build/doxygen/html/`.

- Module overviews for Doxygen-only symbols: `docs/doxygen/stlab_groups.hpp` (not compiled).



## Website (stlab.cc)



- **Jekyll** (`docs/`): blog posts, release table, about page.

- **Doxygen HTML**: merged under `/doxygen/` on the published site.



### Full local preview (matches CI)



From the repository root:



```bash

./docs/tools/docs/build-site.sh

```



Requires Ruby ([`docs/.ruby-version`](.ruby-version)), Bundler, CMake, Ninja, and Doxygen. Output: `docs/_site/` (open `docs/_site/doxygen/index.html` or serve the whole `_site` tree).



**Docker:** bind-mount the repo and run the same script inside the container; image `1.0.6+` includes CMake, Ninja, and Doxygen. See [`tools/docker-tools/README.md`](tools/docker-tools/README.md).



**CI:** [`.github/workflows/jekyll.yml`](../.github/workflows/jekyll.yml) runs `build-site.sh --refresh-releases` on pushes to `main`, when a GitHub Release is published, and on manual workflow dispatch.



### Maintainer scripts



| Script | Purpose |

|--------|---------|

| [`tools/docs/build-site.sh`](tools/docs/build-site.sh) | Jekyll + Doxygen → `docs/_site/` |

| [`tools/docs/prepare.sh`](tools/docs/prepare.sh) | Wrapper for `build-site.sh` |

| [`tools/docs/start.sh`](tools/docs/start.sh) | Full build, then Jekyll watch + browser-sync |

| [`about.sh`](about.sh) | Refresh `_data/releases.json` from GitHub API |



After editing headers during `start.sh`, run `build-site.sh --skip-jekyll` to refresh API HTML.



## Historical note



Documentation was previously maintained with [Hyde](https://github.com/adobe/hyde) under `docs/libraries/` and, after Hyde 2.0, `docs/include/stlab/`. That tree was removed once comments were migrated into headers (see [`REMAINING_HYDE_MIGRATION_PLAN.md`](REMAINING_HYDE_MIGRATION_PLAN.md)). A snapshot inventory remains in [`hyde_source_inventory.md`](hyde_source_inventory.md).



The Jekyll site still uses the [Adobe Hyde theme](https://github.com/adobe/hyde-theme) (`remote_theme` in `_config.yml`). Pages such as About and Tips may use `hyde:` front matter (`tab`, `icon`) for navigation—that is theme metadata, not the retired Hyde doc pipeline. `_config.yml` keeps `adobe_hyde.header_image` so the theme’s SCSS builds (not the retired Hyde YAML doc tree).

