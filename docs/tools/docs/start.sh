#!/usr/bin/env bash
# Live preview: full site build once, then Jekyll watch + browser-sync on docs/_site.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
cd "${REPO_ROOT}"

echo "Building full site (Jekyll + Doxygen) before starting live preview..."
"${SCRIPT_DIR}/build-site.sh"

cd docs

# Jekyll watch can still drop _site/doxygen on rebuild; keep_files helps, this restores if needed.
(
  while true; do
    sleep 2
    if [[ -f _site/index.html && ! -f _site/doxygen/index.html ]]; then
      "${SCRIPT_DIR}/sync-doxygen.sh" || true
    fi
  done
) &
DOXYGEN_GUARD_PID=$!
trap 'kill "${DOXYGEN_GUARD_PID}" 2>/dev/null || true' EXIT INT TERM

bundle exec jekyll build --watch --incremental &
sleep 10
browser-sync start --config bs-config.js
