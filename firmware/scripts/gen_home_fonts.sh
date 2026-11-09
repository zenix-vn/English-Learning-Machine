#!/usr/bin/env bash
# Builds the TTF subsets embedded in the firmware for the home and study screens (main/home/fonts/).
# LVGL rasterises them at run time with Tiny TTF (CONFIG_LV_USE_TINY_TTF), so no C font files are generated.
#   baloo2_extrabold.ttf  Baloo 2 ExtraBold, Latin + Vietnamese: titles, tile labels, headwords
#   baloo2_semibold.ttf   Baloo 2 SemiBold, Latin + Vietnamese: body text, hints
#   lucide_home.ttf       Lucide icons used by the screens (code points below)
# Baloo 2 is SIL Open Font License 1.1, Lucide is ISC.
# Needs python3 and internet access. Set WORK=<dir> to reuse earlier downloads.
set -euo pipefail
OUT="$(cd "$(dirname "$0")/../main/home/fonts" && pwd)"
WORK="${WORK:-$(mktemp -d)}"
cd "$WORK"

[ -x venv/bin/pyftsubset ] || { python3 -m venv venv && ./venv/bin/pip install -q fonttools; }
[ -f Baloo2.ttf ] || curl -sL -o Baloo2.ttf \
  "https://raw.githubusercontent.com/google/fonts/main/ofl/baloo2/Baloo2%5Bwght%5D.ttf"
[ -f Baloo2-ExtraBold.ttf ] || ./venv/bin/fonttools varLib.instancer Baloo2.ttf wght=800 -o Baloo2-ExtraBold.ttf -q
[ -f Baloo2-SemiBold.ttf ] || ./venv/bin/fonttools varLib.instancer Baloo2.ttf wght=600 -o Baloo2-SemiBold.ttf -q
if [ ! -f package/font/lucide.ttf ]; then
  npm pack lucide-static@0.460.0 --silent >/dev/null
  tar xzf lucide-static-0.460.0.tgz
fi

VI="U+0020-007E,U+00A0-017F,U+01A0-01B0,U+1EA0-1EF9,U+2013-2014,U+2018-2019,U+201C-201D,U+2026"
# book-open rotate-ccw search message-circle mic trophy wifi wifi-off flame settings check layout-grid list sun
# volume-2 refresh-cw arrow-left chevron-left chevron-right x arrow-down-a-z circle-check languages
ICONS="U+E063,U+E14C,U+E155,U+E11A,U+E11C,U+E377,U+E1AE,U+E1AF,U+E0D7,U+E158,U+E070,U+E105,U+E10C,U+E17C,U+E1AB,U+E149,U+E04C,U+E072,U+E073,U+E1B2,U+E41A,U+E226,U+E103"

./venv/bin/pyftsubset Baloo2-ExtraBold.ttf --unicodes="$VI" --layout-features='' --no-hinting --output-file="$OUT/baloo2_extrabold.ttf"
./venv/bin/pyftsubset Baloo2-SemiBold.ttf --unicodes="$VI" --layout-features='' --no-hinting --output-file="$OUT/baloo2_semibold.ttf"
./venv/bin/pyftsubset package/font/lucide.ttf --unicodes="$ICONS" --no-hinting --output-file="$OUT/lucide_home.ttf"
ls -la "$OUT"
