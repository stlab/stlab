#!/usr/bin/env bash
# Copy built Doxygen HTML into docs/_site/doxygen/ (used by build-site.sh and start.sh watch).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
cd "${REPO_ROOT}"

if [[ ! -d build/doxygen/html ]]; then
  echo "error: build/doxygen/html not found; run ./docs/tools/docs/build-site.sh first" >&2
  exit 1
fi

mkdir -p docs/_site
rm -rf docs/_site/doxygen
mkdir -p docs/_site/doxygen
cp -r build/doxygen/html/. docs/_site/doxygen/
