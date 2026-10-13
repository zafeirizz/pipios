#!/usr/bin/env bash
# Run QEMU headless and feed monitor commands from stdin; prints monitor replies.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
./tools/qstop.sh
rm -f build/serial.log build/mon.sock
AURORA_HTTP_FWD=$((23000 + $$ % 900)) ./tools/qemu.sh &
QPID=$!
sleep "${1:-5}"
shift
for c in "$@"; do printf '%s\n' "$c"; sleep 0.2; done | timeout 10 socat - unix-connect:build/mon.sock 2>/dev/null \
  | sed 's/\x1b\[[0-9;]*[A-Za-z]//g;s/\r//g'
kill "$QPID" 2>/dev/null; sleep 0.3; kill -9 "$QPID" 2>/dev/null
wait "$QPID" 2>/dev/null
exit 0
