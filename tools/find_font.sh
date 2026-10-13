#!/usr/bin/env bash
# Locate a usable 8x16 PSF console font.
for p in \
  "$HOME/sysroot/usr/share/consolefonts/lat1-16.psf.gz" \
  "$HOME/sysroot/usr/share/consolefonts/altc-8x16.psf.gz" \
  /usr/share/consolefonts/lat1-16.psf.gz; do
  [ -f "$p" ] && { echo "$p"; exit 0; }
done
# last resort: any 8x16 psf
f=$(find /usr/share/consolefonts -name '*8x16*.psf*' 2>/dev/null | head -1)
[ -n "$f" ] && { echo "$f"; exit 0; }
echo "ERROR: no PSF font found (run tools/setup-env.sh)" >&2
exit 1
