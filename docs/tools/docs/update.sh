#!/usr/bin/env bash
# Install/update Jekyll gems. Preserves Gemfile.lock by default (use --lock to refresh it).
set -euo pipefail

REFRESH_LOCK=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    -l | --lock)
      REFRESH_LOCK=1
      shift
      ;;
    *)
      shift
      ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
cd "${REPO_ROOT}/docs"

if [[ "${REFRESH_LOCK}" -eq 1 ]]; then
  bundle lock --update
else
  bundle install
fi
