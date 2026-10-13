# AuroraOS build environment - source this file
# Records the tool locations installed by tools/setup-env.sh
export AURORA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export SYSROOT="$HOME/sysroot"
export QEMU_DATA="$HOME/qemu-data"
export PATH="$SYSROOT/usr/bin:$SYSROOT/usr/sbin:$AURORA_ROOT/tools/bin:$PATH"
export LD_LIBRARY_PATH="$SYSROOT/usr/lib/x86_64-linux-gnu:$SYSROOT/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QEMU_BIN="$SYSROOT/usr/bin/qemu-system-x86_64"
export QEMU_ARGS="-L $QEMU_DATA"
export QEMU_MODULE_DIR="$SYSROOT/usr/lib/x86_64-linux-gnu/qemu"
