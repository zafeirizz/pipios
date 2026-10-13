#!/usr/bin/env bash
# Kill any leftover AuroraOS QEMU instances (safe pattern: bracket avoids self-match).
for p in $(pgrep -f 'sysroot/usr/bin/qemu-system-x86_[6]4'); do
  kill "$p" 2>/dev/null; sleep 0.2; kill -9 "$p" 2>/dev/null
done
sleep 0.5
exit 0
