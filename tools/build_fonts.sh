#!/usr/bin/env bash
#
# Regenerates the monospace terminal fonts in src/ui/fonts/.
#
# The message view is a character-cell grid, so the font has to be monospace and
# has to carry the whole of code page 437 - box drawing, block elements and
# shading blocks are what ASCII/ANSI art is actually made of.
#
# Requires: npm install -g lv_font_conv
#
set -euo pipefail

cd "$(dirname "$0")/.."
OUT=src/ui/fonts

# A monospace TTF with box-drawing and block-element coverage. Menlo (macOS)
# covers all of CP437; Andale Mono does not, so it is only a last resort.
FONT="${ACID_FONT:-/System/Library/Fonts/Menlo.ttc}"
[ -f "$FONT" ] || FONT="/System/Library/Fonts/Supplemental/Andale Mono.ttf"
[ -f "$FONT" ] || { echo "no monospace font found; set ACID_FONT=/path/to.ttf" >&2; exit 1; }

# lv_font_conv (opentype.js) cannot read TrueType collections, so pull the
# regular face out into a standalone TTF first.
case "$FONT" in
  *.ttc)
    EXTRACTED="$(mktemp -d)/face.ttf"
    python3 - "$FONT" "$EXTRACTED" <<'PYEOF'
import sys
from fontTools.ttLib import TTCollection
src, dst = sys.argv[1], sys.argv[2]
faces = TTCollection(src).fonts
# Prefer the Regular face; fall back to the first one in the collection.
face = next((f for f in faces if 'Regular' in (f['name'].getDebugName(4) or '')), faces[0])
face.save(dst)
PYEOF
    FONT="$EXTRACTED"
    ;;
esac

echo "source font: $FONT"

# Code page 437, expressed as the Unicode code points it maps to.
RANGES=(
  -r 0x20-0x7E        # ASCII
  -r 0xA0-0xFF        # Latin-1 supplement (accented chars, guillemets, +-, etc)
  -r 0x0192           # f with hook
  -r 0x0393,0x0398,0x03A3,0x03A6,0x03A9          # Gamma Theta Sigma Phi Omega
  -r 0x03B1,0x03B2,0x03B4,0x03B5,0x03C0,0x03C3,0x03C4,0x03C6  # alpha beta delta eps pi sigma tau phi
  -r 0x2022           # bullet
  -r 0x203C           # double exclamation
  -r 0x207F           # superscript n
  -r 0x20A7           # peseta
  -r 0x2190-0x2195    # arrows
  -r 0x21A8           # up-down arrow with base
  -r 0x2219,0x221A,0x221E,0x221F,0x2229,0x2248,0x2261,0x2264,0x2265
  -r 0x2302           # house
  -r 0x2310           # reversed not sign
  -r 0x2320,0x2321    # integral top/bottom
  -r 0x2500-0x257F    # box drawing
  -r 0x2580-0x259F    # block elements + shading
  -r 0x25A0,0x25AC,0x25B2,0x25BA,0x25BC,0x25C4,0x25CB,0x25D8,0x25D9
  -r 0x263A,0x263B,0x263C
  -r 0x2640,0x2642
  -r 0x266A,0x266B
  -r 0x2660,0x2663,0x2665,0x2666
)

# Menlo advance is 0.60205em, so only a few sizes land on a whole pixel.
# A fractional advance would put seams in box-drawing art, so stick to these.
gen() {
  local size="$1" name="$2"
  echo "  -> $name (${size}px)"
  lv_font_conv \
    --font "$FONT" \
    --size "$size" \
    "${RANGES[@]}" \
    --format lvgl \
    --bpp 4 \
    --no-compress \
    --force-fast-kern-format \
    --lv-include lvgl.h \
    -o "$OUT/$name.c"
}

mkdir -p "$OUT"
gen 10 acid_mono_10   # 6x14 cells -> 53x13 grid
gen 15 acid_mono_15   # 9x20 cells -> 35x9  grid

echo "done"
