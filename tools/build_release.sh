#!/usr/bin/env bash
# Builds and packages an nDash release for all supported boards (or one,
# with --board NAME). Thin wrapper: activates ESP-IDF, then delegates the
# actual work to build_release.py.
#
# NOTE: ESP-IDF's own export.sh refuses to run under Git-Bash/MSYS on
# Windows ("MSys/Mingw is not supported") - use build_release.bat there
# instead (native cmd.exe).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -z "${IDF_PATH:-}" ]; then
  echo "IDF_PATH is not set. Source ESP-IDF's export.sh first, e.g.:" >&2
  echo "  . \$HOME/esp/esp-idf/export.sh" >&2
  exit 1
fi

if ! command -v idf.py >/dev/null 2>&1; then
  echo "idf.py not found on PATH even though IDF_PATH is set - re-source export.sh" >&2
  exit 1
fi

python3 "$SCRIPT_DIR/build_release.py" "$@"
