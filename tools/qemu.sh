#!/usr/bin/env bash
# AuroraOS QEMU runner.
#   AURORA_MODE=headless (default) : no display, serial -> build/serial.log,
#                                    monitor -> build/mon.sock
#   AURORA_MODE=window            : native display window, monitor on stdio
#   AURORA_MODE=vnc               : VNC display :5, monitor -> build/mon.sock
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/tools/env.sh"
mkdir -p "$ROOT/build"
IMG="${AURORA_IMG:-$ROOT/build/disk.img}"
SERIAL="${AURORA_SERIAL:-$ROOT/build/serial.log}"
MON="${AURORA_MON:-$ROOT/build/mon.sock}"
FWD="${AURORA_HTTP_FWD:-8081}"

ARGS=(-machine pc -m 256 -vga std
      -drive "file=$IMG,format=raw,if=ide,index=0,media=disk"
      -netdev "user,id=n0,hostfwd=tcp::${FWD}-:80"
      -device rtl8139,netdev=n0
      -serial "file:$SERIAL"
      -no-reboot -no-shutdown)

case "${AURORA_MODE:-headless}" in
  headless) ARGS+=(-display none -monitor "unix:$MON,server,nowait") ;;
  window)   ARGS+=(-monitor stdio) ;;
  vnc)      ARGS+=(-display vnc=:5 -monitor "unix:$MON,server,nowait") ;;
  *) echo "unknown AURORA_MODE" >&2; exit 1 ;;
esac

exec "${QEMU_BIN}" -L "${QEMU_DATA}" "${ARGS[@]}" "$@"
