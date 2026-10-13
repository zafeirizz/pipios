#!/usr/bin/env bash
# Input test: injects PS/2 key + mouse events through the QEMU monitor and
# checks the guest echoed them on serial.
#
# Note: QEMU's monitor queues relative mouse events lazily -- a queued
# mouse_move is only delivered to the guest with the *next* mouse event,
# so a no-op mouse_button 0 is sent after the move to flush it.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
export AURORA_HTTP_FWD=$((19000 + $$ % 900))
rm -f build/serial.log build/mon.sock
bash tools/qemu.sh &
QPID=$!
cleanup() { kill "$QPID" 2>/dev/null; sleep 0.3; kill -9 "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null; }
trap cleanup EXIT

# Wait for the desktop main loop to be running. Keys injected before it starts
# are buffered by the keyboard driver but not echoed, so wait for the M6 marker
# (printed immediately before the loop) rather than the M3 input-driver one.
MARKER="${AURORA_INPUT_MARKER:-AURORA-M6-PASS}"
t=0
until grep -q "$MARKER" build/serial.log 2>/dev/null; do
  [ $t -ge 80 ] && { echo "INPUT TEST FAIL: no $MARKER marker"; cat build/serial.log; exit 1; }
  kill -0 "$QPID" 2>/dev/null || { echo "INPUT TEST FAIL: qemu died"; exit 1; }
  sleep 0.5; t=$((t+1))
done
sleep 1

{
  for k in h e l l o spc t h e r e; do echo "sendkey $k"; sleep 0.05; done
} | timeout 15 socat - unix-connect:build/mon.sock >/dev/null 2>&1
sleep 1
{
  # QEMU holds a relative move queued until the next mouse event arrives,
  # so send several moves and then button transitions to flush them.
  for i in 1 2 3 4 5 6; do echo mouse_move 5 3; sleep 0.1; done
  echo mouse_button 1; sleep 0.3
  echo mouse_button 0; sleep 0.1
} | timeout 15 socat - unix-connect:build/mon.sock >/dev/null 2>&1
sleep 0.5

# The loop consumes one key per iteration and repaints the desktop between
# keys, so poll for the serial echo instead of expecting it inside a fixed sleep.
t=0
until grep -q 'hello there' build/serial.log 2>/dev/null || [ $t -ge 40 ]; do
  sleep 0.5; t=$((t+1))
done

ok=1
grep -q 'hello there' build/serial.log || { echo "INPUT TEST FAIL: keyboard echo missing"; ok=0; }
grep -q '\[mouse\] .*buttons=1' build/serial.log || { echo "INPUT TEST FAIL: mouse event missing"; ok=0; }
if [ $ok = 1 ]; then echo "INPUT TEST PASS"; else echo "--- serial ---"; cat build/serial.log; exit 1; fi
