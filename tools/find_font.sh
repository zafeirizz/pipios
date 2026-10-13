#!/usr/bin/env bash
# Locate a usable 8px-wide PSF console font and print its path.
#
# The kernel's font renderer draws one byte per glyph row, so glyphs are
# exactly 8 pixels wide and the font must be 8 wide. Names cannot be trusted:
# Arabic-VGA28x16.psf.gz and friends contain "8x16" but are PSF2 fonts that are
# 16 pixels wide, so every candidate is validated by parsing its header
# (tools/psf2c.py --check) before it is accepted.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHECK="$ROOT/tools/psf2c.py"

dirs=(
  "$HOME/sysroot/usr/share/consolefonts"
  "$HOME/sysroot/usr/share/kbd/consolefonts"
  /usr/share/consolefonts
  /usr/share/kbd/consolefonts
  /usr/lib/kbd/consolefonts
  /usr/local/share/consolefonts
)

# 8x16 console fonts shipped by console-setup / kbd, most preferred first
preferred=(
  lat1-16.psf.gz altc-8x16.psf.gz LatArCyrHeb-16.psf.gz Uni2-Fixed16.psf.gz
  lat9w-16.psf.gz default8x16.psf.gz Lat15-VGA16.psf.gz CyrKoi-VGA16.psf.gz
  lat2-16.psf.gz VGA8x16.psf.gz
)

usable() {          # usable <file> :: 0 if it parses and is 8 px wide
  [ -f "$1" ] || return 1
  python3 "$CHECK" --check "$1" >/dev/null 2>&1
}

for d in "${dirs[@]}"; do
  for n in "${preferred[@]}"; do
    if usable "$d/$n"; then echo "$d/$n"; exit 0; fi
  done
done

# otherwise: the first font in any font directory that really is 8 px wide
for d in "${dirs[@]}"; do
  [ -d "$d" ] || continue
  while IFS= read -r f; do
    if usable "$f"; then echo "$f"; exit 0; fi
  done < <(find "$d" -maxdepth 1 -type f \( -name '*.psf' -o -name '*.psf.gz' \) | sort)
done

echo "ERROR: no 8px-wide PSF console font found." >&2
echo "       searched: ${dirs[*]}" >&2
echo "       install one (the 'console-data' or 'kbd' package), run" >&2
echo "       tools/setup-env.sh, or set FONT_PSF=/path/to/8x16-font.psf.gz" >&2
exit 1
