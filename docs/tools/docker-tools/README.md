# Using the docker image

## Setup

### Install Docker

If you don't already have Docker installed, [install Docker](https://docs.docker.com/get-docker/).

### Building the docker image

To build the docker image, first, update the VERSION variable below (please use semantic versioning). Add a [release note](#release-notes).

Specify the ruby version to match the latest stable - https://www.ruby-lang.org/en/downloads/

macOS and Linux:

```bash
VERSION="1.0.9"
VOLUME="stlab.libraries"
RUBY_VERSION="3.4.4"
```

Windows:

```powershell
$VERSION="1.0.9"
$VOLUME="stlab.libraries"
$RUBY_VERSION="3.4.4"

$PSDefaultParameterValues = @{'Out-File:Encoding' = 'Ascii'}
```

Update the Docker and ruby version in the following files:

```bash
echo $VERSION > ./docs/tools/docker-tools/VERSION
echo $RUBY_VERSION > ./docs/.ruby-version
```

Build the image, no-cache is used so the latest tools are installed

```bash
docker build --build-arg RUBY_VERSION=$RUBY_VERSION --file ./docs/tools/docker-tools/Dockerfile --tag $VOLUME . --no-cache
```

## Running the Docker image with remote theme

To run the docker image, execute the following.

macOS and Linux (Bash):

```bash
docker run --mount type=bind,source="$(pwd)",target=/mnt/host --tty --interactive --publish 3000-3001:3000-3001 $VOLUME bash
```

Windows (PowerShell) — use `$PWD` and quote the whole `--mount` argument so the path is passed correctly:

```powershell
docker run --mount "type=bind,source=$PWD,target=/mnt/host" --tty --interactive --publish 3000-3001:3000-3001 $VOLUME bash
```

### Running the Docker image with local theme

Edit Gemfile and _config.yml to use a local copy of the theme. See `[local-them]` in the files for details.

```bash
code ./docs/Gemfile
code ./docs/_config.yml
```

```
docker run --mount type=bind,source="$(pwd)",target=/mnt/host \
    --mount type=bind,source=`readlink -f ../../adobe/hyde-theme`,target=/mnt/themes \
    --tty --interactive --publish 3000-3001:3000-3001 \
    $VOLUME bash
```

## Preparing the docs

This should leave you at a bash prompt that looks like this:

```
app@fc7590a63ba3:~$
```

The hex number is the docker image container ID and may be different. Going foreword I refer to this as the _docker_ prompt to distinguish it from the _local_ prompt.

```
cd /mnt/host
git config --global --add safe.directory /mnt/host
./docs/tools/docs/update.sh    # bundle install only; use --lock to refresh Gemfile.lock
```

## Build the documentation site

To build or rebuild the **full** site (Jekyll blog/pages **and** Doxygen API under `/doxygen/`, same as [stlab.cc](https://stlab.cc) and CI), from the docker prompt:

```bash
cd /mnt/host
./docs/tools/docs/build-site.sh
```

`prepare.sh` is a thin wrapper around `build-site.sh`. Useful flags:

- `--skip-doxygen` — Jekyll only (fast markdown/theme edits)
- `--skip-jekyll` — rebuild API docs and recopy into `docs/_site/doxygen/` after editing `include/stlab/**/*.hpp`
- `--refresh-releases` — refresh `docs/_data/releases.json` from the GitHub API before building

All commands assume the repository is bind-mounted at `/mnt/host` (see `docker run` above).

## Run a local server for the site

From the docker prompt:

```bash
cd /mnt/host
./docs/tools/docs/start.sh
```

`start.sh` runs a full `build-site.sh` once, then Jekyll `--watch` plus browser-sync on `docs/_site/`. Open `http://localhost:3000` (API docs at `http://localhost:3000/doxygen/`).

**Note:** `docs/doxygen/` (only `stlab_groups.hpp`) is excluded from Jekyll. API HTML is copied into `_site/doxygen/` by `build-site.sh`; `start.sh` keeps it across `jekyll --watch` rebuilds (`keep_files` + `sync-doxygen.sh` guard). After editing header Doxygen comments, run `./docs/tools/docs/build-site.sh --skip-jekyll` to refresh API HTML.

## Tips

If you want to open another terminal on the running image use:

```
docker ps
docker exec -it <container id> bash
```

### Release Notes

- 1.0.0 - Initial release for Jekyll
- 1.0.1 - Updating tool set
- 1.0.2 — Jekyll site updates (historical; API docs are now Doxygen-only in the main repo).
- 1.0.3 — Updating Jekyll to 4.2.0 and moving to GitHub Actions.
- 1.0.4 - Updating docs for new header directory structure. The gem installs are no longer baked into the image, this was causing too many issues.
- 1.0.5 - Updating to Ruby 3.4.4 and Jekyll 5.1.0.
- 1.0.6 - CMake, Ninja, and Doxygen for unified `build-site.sh` (Jekyll + API docs in one output tree).
- 1.0.7 - Use `C.UTF-8` for all locale variables (fixes bash `setlocale` warnings on `en_US.UTF-8`).
- 1.0.8 - Graphviz (`dot`) for Doxygen diagrams; use `bundle install` in `update.sh` (no longer deletes `Gemfile.lock` by default).
- 1.0.9 - Remove Graphviz (`dot`); dependency graphs rely on Doxygen defaults until cpp-library sets `HAVE_DOT = NO`.

### Troubleshooting `build-site.sh`

- **Jekyll SCSS `Expected $args to contain a key`:** Ensure `adobe_hyde.header_image` is set in [`docs/_config.yml`](../../_config.yml) (the theme’s `root.scss` must pass two maps to `map.merge`). Then run `./docs/tools/docs/update.sh` and rebuild.
