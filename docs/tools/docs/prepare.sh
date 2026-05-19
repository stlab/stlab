#!/usr/bin/env bash
# Full production-like site build (Jekyll + Doxygen). See build-site.sh for options.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${SCRIPT_DIR}/build-site.sh" "$@"
