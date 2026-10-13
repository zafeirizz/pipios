#!/usr/bin/env bash
# Boot the OS, wait for a serial marker, capture a screendump, then exit.
#   usage: screendump.sh <MARKER> <OUT.ppm> [timeout_s] [monitor-commands...]
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
MARKER="${1:?marker needed}"; OUT="${2:?out ppm needed}"; TIMEOUT="${3:-90}"
shift 3 || true
export AURORA_HTTP_FWD=$((21000 + $$ % 900))
rm -f build/serial.log build/mon.sock "$OUT"

bash tools/qemu.sh & QPID=$!
cleanup() { kill "$QPID" 2>/dev/null; sleep 0.3; kill -9 "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null; }
trap cleanup EXIT

t=0
until grep -q "$MARKER" build/serial.log 2>/dev/null; do
  [ $t -ge "$TIMEOUT" ] && { echo "SCREENDUMP FAIL: no $MARKER"; exit 1; }
  kill -0 "$QPID" 2>/dev/null || { echo "SCREENDUMP FAIL: qemu died"; exit 1; }
  sleep 1; t=$((t+1))
done
sleep 1

{ for cmd in "$@"; do printf '%s\n' "$cmd"; sleep 0.4; done
  printf 'screendump %s\n' "$OUT"; sleep 1; printf 'quit -f\n'; } \
  | timeout 20 socat - unix-connect:build/mon.sock >/dev/null 2>&1

for _ in 1 2 3 4 5 6; do [ -s "$OUT" ] && break; sleep 0.5; done
[ -s "$OUT" ] && echo "SCREENDUMP OK: $OUT" || { echo "SCREENDUMP FAIL: no output"; exit 1; }
