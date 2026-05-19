# stlab documentation site

Published at [stlab.cc](https://stlab.cc). This directory builds the **blog, release notes, and static pages** with Jekyll. **API reference** comes from **Doxygen** (merged into the site under `/doxygen/` in CI).

Pull requests for typos, examples, and other improvements are welcome. For library issues, use the [stlab repository](https://github.com/stlab/stlab).

## Local Jekyll site

From this directory:

```bash
bundle install
bundle exec jekyll serve
```

For Docker-based tooling used by maintainers, see [`tools/docker-tools/README.md`](../tools/docker-tools/README.md).

## API reference (Doxygen)

From the repository root:

```bash
cmake --preset=doxygen
cmake --build --preset=doxygen
```

Open `build/doxygen/html/index.html`. The same build runs in [`.github/workflows/jekyll.yml`](../.github/workflows/jekyll.yml) and copies output to `docs/_site/doxygen/`.

Authoritative prose lives in `include/stlab/**/*.hpp` as Doxygen comments. The old Hyde YAML/Markdown mirror under `docs/include/stlab/` has been removed; see [`include/README.md`](include/README.md).
