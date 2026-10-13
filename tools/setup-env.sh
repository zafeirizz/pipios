#!/usr/bin/env bash
# Reproducible user-local toolchain installer for AuroraOS (no root required).
# Installs: nasm, qemu-system-x86_64 (+firmware), dosfstools, mtools, console fonts.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APT_OPTS=(-o Dir::State::Lists="$HOME/apt/lists" -o Dir::Cache="$HOME/apt/cache" -o Debug::NoLocking=1)
mkdir -p "$HOME/apt/lists/partial" "$HOME/apt/cache/archives/partial"
apt-get update "${APT_OPTS[@]}" >/dev/null 2>&1 || true
PKGS=(nasm qemu-system-x86 dosfstools mtools console-data)
DL="$HOME/dl/debs"; mkdir -p "$DL"; cd "$DL"
for p in "${PKGS[@]}"; do apt-get "${APT_OPTS[@]}" download "$p" >/dev/null 2>&1 || true; done
SYSROOT="$HOME/sysroot"; mkdir -p "$SYSROOT"
for d in *.deb; do dpkg-deb -x "$d" "$SYSROOT/" 2>/dev/null || true; done
# QEMU firmware data dir (seabios + vgabios + qemu data)
QD="$HOME/qemu-data"; mkdir -p "$QD"
for f in "$SYSROOT"/usr/share/qemu/*; do [ -e "$f" ] && ln -sf "$f" "$QD/"; done
for f in "$SYSROOT"/usr/share/seabios/bios*.bin "$SYSROOT"/usr/share/seabios/vgabios*.bin; do [ -e "$f" ] && ln -sf "$f" "$QD/"; done
if [ -d "$SYSROOT/usr/lib/ipxe/qemu" ]; then
  for f in "$SYSROOT"/usr/lib/ipxe/qemu/*; do [ -e "$f" ] && ln -sf "$f" "$QD/"; done
fi
# Wrapper scripts
TB="$SCRIPT_DIR/bin"; mkdir -p "$TB"
cat > "$TB/qemu-system-x86_64" <<W
#!/bin/sh
export LD_LIBRARY_PATH="$SYSROOT/usr/lib/x86_64-linux-gnu:$SYSROOT/lib/x86_64-linux-gnu:\${LD_LIBRARY_PATH:-}"
export QEMU_MODULE_DIR="$SYSROOT/usr/lib/x86_64-linux-gnu/qemu"
exec "$SYSROOT/usr/bin/qemu-system-x86_64" -L "$QD" "\$@"
W
chmod +x "$TB/qemu-system-x86_64"
"$SYSROOT/usr/bin/nasm" -v
"$TB/qemu-system-x86_64" --version | head -1
"$SYSROOT/usr/sbin/mkfs.fat" --help 2>&1 | head -1
echo "setup-env: OK"
