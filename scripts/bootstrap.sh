#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if [[ -z "${IDF_PATH:-}" ]]; then
  # shellcheck disable=SC1091
  source "${HOME}/esp/esp-idf/export.sh"
fi
idf.py set-target esp32s3
echo "Target set. Run: idf.py build"
