#!/usr/bin/env bash
# Generates the LVGL fonts used by the offline study screen (main/offline/fonts/).
#   font_en_64.c  large Latin letters for the headword (already committed)
#   font_en_44.c  medium Latin letters for long headwords and the OK badge
#   font_vi_28.c  Vietnamese + IPA + punctuation for meanings and titles
#   font_vi_22.c  the same characters, small, for chips, examples and hints
# Needs Node.js and internet access (downloads lv_font_conv and Noto Sans, SIL Open Font License 1.1).
set -euo pipefail
cd "$(dirname "$0")/../main/offline/fonts"
TTF=NotoSans-Regular.ttf
[ -f "$TTF" ] || curl -sL -o "$TTF" \
  https://github.com/notofonts/notofonts.github.io/raw/main/fonts/NotoSans/full/ttf/NotoSans-Regular.ttf

RANGES="0x20-0x7E,0xA0-0x17F,0x1A0-0x1B0,0x250-0x2FF,0x1EA0-0x1EF9,0x2013-0x2014,0x2018-0x2019,0x201C-0x201D,0x2022,0x2026"
npx --yes lv_font_conv@1.5.3 --font "$TTF" --size 28 --bpp 4 --range "$RANGES" \
  --format lvgl --lv-include lvgl.h --no-compress --no-kerning --lv-font-name font_vi_28 -o font_vi_28.c
npx --yes lv_font_conv@1.5.3 --font "$TTF" --size 22 --bpp 4 --range "$RANGES" \
  --format lvgl --lv-include lvgl.h --no-compress --no-kerning --lv-font-name font_vi_22 -o font_vi_22.c
npx --yes lv_font_conv@1.5.3 --font "$TTF" --size 44 --bpp 4 --range "0x20-0x7E" \
  --format lvgl --lv-include lvgl.h --no-compress --no-kerning --lv-font-name font_en_44 -o font_en_44.c
npx --yes lv_font_conv@1.5.3 --font "$TTF" --size 64 --bpp 4 --range "0x20-0x7E" \
  --format lvgl --lv-include lvgl.h --no-compress --no-kerning --lv-font-name font_en_64 -o font_en_64.c
rm -f "$TTF"
ls -la font_*.c
