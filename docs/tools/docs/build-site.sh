#!/usr/bin/env bash
# Build the full stlab.cc site: Jekyll (blog/pages) + Doxygen API under docs/_site/doxygen/.
# Run from anywhere; uses repository root derived from this script's location.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
cd "${REPO_ROOT}"

REFRESH_RELEASES=0
SKIP_DOXYGEN=0
SKIP_JEKYLL=0

usage() {
  cat <<'EOF'
Usage: build-site.sh [OPTIONS]

Build Jekyll site and merge Doxygen HTML into docs/_site/doxygen/ (same output as CI).

Options:
  --refresh-releases   Fetch docs/_data/releases.json (and related) from GitHub API
  --skip-doxygen       Jekyll only (no cmake/Doxygen)
  --skip-jekyll        Doxygen build and copy only (after header/API doc edits)
  -h, --help           Show this help

Environment:
  JEKYLL_BASEURL       Passed to jekyll build (set by GitHub Pages in CI)
  JEKYLL_ENV           Defaults to production when running Jekyll
  REFRESH_RELEASE_DATA=1   Same as --refresh-releases
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --refresh-releases)
      REFRESH_RELEASES=1
      shift
      ;;
    --skip-doxygen)
      SKIP_DOXYGEN=1
      shift
      ;;
    --skip-jekyll)
      SKIP_JEKYLL=1
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ "${REFRESH_RELEASE_DATA:-0}" == "1" ]]; then
  REFRESH_RELEASES=1
fi

if [[ "${REFRESH_RELEASES}" -eq 1 ]]; then
  echo "Refreshing release data from GitHub API..."
  bash docs/about.sh
fi

if [[ "${SKIP_DOXYGEN}" -eq 0 ]]; then
  if ! command -v dot >/dev/null 2>&1; then
    echo "error: Graphviz 'dot' not found. Doxygen needs it for class/collaboration graphs." >&2
    echo "  Docker: rebuild image 1.0.8+ (includes graphviz). Host: install graphviz package." >&2
    exit 1
  fi
  echo "Building Doxygen (cmake preset doxygen)..."
  # Remove stale HTML if a prior run failed without dot (broken .map/.png references).
  if [[ -d build/doxygen/html ]]; then
    rm -rf build/doxygen/html
  fi
  cmake --preset=doxygen
  cmake --build --preset=doxygen
fi

if [[ "${SKIP_JEKYLL}" -eq 0 ]]; then
  echo "Building Jekyll site..."
  export JEKYLL_ENV="${JEKYLL_ENV:-production}"
  (
    cd docs
    bundle check >/dev/null 2>&1 || bundle install
    bundle exec jekyll build --baseurl "${JEKYLL_BASEURL:-}"
  )
fi

if [[ "${SKIP_DOXYGEN}" -eq 0 ]]; then
  echo "Merging Doxygen into docs/_site/doxygen/..."
  "${SCRIPT_DIR}/sync-doxygen.sh"
fi

echo "Site ready under docs/_site/ (API at docs/_site/doxygen/)"
