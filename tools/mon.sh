#!/usr/bin/env bash
# Send HMP monitor commands to the running headless QEMU instance.
#   tools/mon.sh "screendump build/screen.ppm"
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MON="${AURORA_MON:-$ROOT/build/mon.sock}"
for cmd in "$@"; do
  printf '%s\n' "$cmd" | socat - "unix-connect:$MON" >/dev/null
done
