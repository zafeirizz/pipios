#!/usr/bin/env bash
# Boot smoke test: waits for MARKER on the guest serial log.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
MARKER="${1:-AURORA-M1-PASS}"
TIMEOUT="${2:-30}"
export AURORA_HTTP_FWD=$((18000 + $$ % 1000))
rm -f build/serial.log build/mon.sock
bash tools/qemu.sh &
QPID=$!
cleanup() {
  kill "$QPID" 2>/dev/null
  sleep 0.3
  kill -9 "$QPID" 2>/dev/null
  wait "$QPID" 2>/dev/null
}
trap cleanup EXIT
t=0
while [ $t -lt $((TIMEOUT * 2)) ]; do
  if [ -f build/serial.log ] && grep -q "$MARKER" build/serial.log 2>/dev/null; then
    echo "BOOT TEST PASS ($MARKER)"
    exit 0
  fi
  if ! kill -0 "$QPID" 2>/dev/null; then
    echo "BOOT TEST FAIL: QEMU exited early"
    cat build/serial.log 2>/dev/null
    exit 1
  fi
  sleep 0.5
  t=$((t + 1))
done
echo "BOOT TEST FAIL: timeout waiting for $MARKER"
echo "--- serial log ---"
cat build/serial.log 2>/dev/null
exit 1
