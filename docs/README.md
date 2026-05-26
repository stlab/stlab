# stlab documentation site

Published at [stlab.cc](https://stlab.cc). This directory builds the **blog, release notes, and static pages** with Jekyll. **API reference** comes from **Doxygen** (merged into the site under `/doxygen/` in CI and local full builds).

Pull requests for typos, examples, and other improvements are welcome. For library issues, use the [stlab repository](https://github.com/stlab/stlab).

## Full site (Jekyll + Doxygen)

From the **repository root**, one script matches CI and production layout (`docs/_site/` including `docs/_site/doxygen/`):

```bash
./docs/tools/docs/build-site.sh
```

**Prerequisites (host):** Ruby 3.4.x ([`.ruby-version`](.ruby-version)), Bundler, CMake 3.28+, Ninja, Doxygen. On Windows, use the same tools in PowerShell or Git Bash, or use [Docker](tools/docker-tools/README.md) below.

Options:

- `--skip-doxygen` — Jekyll only (markdown/theme)
- `--skip-jekyll` — rebuild API docs and copy into `_site/doxygen/` after editing headers
- `--refresh-releases` — update `docs/_data/releases.json` from GitHub before building

Serve the built tree:

```bash
cd docs && bundle exec jekyll serve --skip-initial-build
```

Or static file server:

```bash
npx --yes serve _site -p 3000
```

Live preview with auto-reload (maintainers): from repo root, `./docs/tools/docs/start.sh` (Docker or host with browser-sync). See [`tools/docker-tools/README.md`](tools/docker-tools/README.md).

## Jekyll only (no API section)

From this directory:

```bash
bundle install
bundle exec jekyll serve
```

Home page links to `/doxygen/` will not work until you run a full `build-site.sh`. The folder `docs/doxygen/` is Doxyfile input only (not copied by Jekyll).

## API reference (Doxygen only)

From the repository root:

```bash
cmake --preset=docs
cmake --build --preset=docs
```

Open `build/docs/html/index.html` (preset `doxygen` writes to `build/doxygen/html`). CI merges this into `docs/_site/doxygen/` via [`build-site.sh`](tools/docs/build-site.sh) (see [`.github/workflows/jekyll.yml`](../.github/workflows/jekyll.yml)).

Authoritative prose lives in `include/stlab/**/*.hpp` as Doxygen comments. The old Hyde YAML/Markdown mirror under `docs/include/stlab/` has been removed; see [`include/README.md`](include/README.md).

See also [`DOCUMENTATION.md`](DOCUMENTATION.md).
