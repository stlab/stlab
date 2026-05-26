#!/usr/bin/env bash
# Copy built Doxygen HTML into docs/_site/doxygen/ (used by build-site.sh and start.sh watch).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
cd "${REPO_ROOT}"

DOXYGEN_HTML=""
if [[ -d build/docs/html ]]; then
  DOXYGEN_HTML="build/docs/html"
elif [[ -d build/doxygen/html ]]; then
  DOXYGEN_HTML="build/doxygen/html"
fi

if [[ -z "${DOXYGEN_HTML}" ]]; then
  echo "error: build/docs/html not found (run cmake --preset=docs && cmake --build --preset=docs)" >&2
  exit 1
fi

mkdir -p docs/_site
rm -rf docs/_site/doxygen
mkdir -p docs/_site/doxygen
cp -r "${DOXYGEN_HTML}/." docs/_site/doxygen/
