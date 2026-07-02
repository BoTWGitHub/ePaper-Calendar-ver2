#!/bin/bash
# ============================================================================
# generate_fonts.sh — Convert Noto Sans TC to LVGL bitmap fonts
# 
# Usage:
#   1. Download NotoSansTC-Regular.ttf from Google Fonts:
#      https://fonts.google.com/noto/specimen/Noto+Sans+TC
#   2. Place it in this directory (components/fonts/)
#   3. Run: bash generate_fonts.sh
#
# Requirements: Node.js (npx will auto-install lv_font_conv)
# ============================================================================

set -e

FONT_FILE="NotoSansTC-Regular.ttf"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ ! -f "$SCRIPT_DIR/$FONT_FILE" ]; then
    echo "❌ Error: $FONT_FILE not found in $SCRIPT_DIR"
    echo ""
    echo "Please download Noto Sans TC from:"
    echo "  https://fonts.google.com/noto/specimen/Noto+Sans+TC"
    echo ""
    echo "Then place NotoSansTC-Regular.ttf in:"
    echo "  $SCRIPT_DIR/"
    exit 1
fi

# Unicode ranges to include:
#   0x20-0x7E     ASCII printable
#   0x2000-0x206F General punctuation (em-dash, ellipsis, etc.)
#   0x3000-0x303F CJK symbols & punctuation (。，「」etc.)
#   0x4E00-0x9FFF CJK Unified Ideographs (all common Chinese characters)
#   0xFF01-0xFF5E Fullwidth ASCII variants
RANGE="0x20-0x7E,0x2000-0x206F,0x3000-0x303F,0x4E00-0x9FFF,0xFF01-0xFF5E"

echo "🔤 Generating font_noto_tc_20.c (20px, 1bpp, compressed)..."
npx -y lv_font_conv \
    --font "$SCRIPT_DIR/$FONT_FILE" \
    --size 20 \
    --bpp 1 \
    --format lvgl \
    --range "$RANGE" \
    --lv-include "lvgl.h" \
    -o "$SCRIPT_DIR/font_noto_tc_20.c"

echo "🔤 Generating font_noto_tc_24.c (24px, 1bpp, compressed)..."
npx -y lv_font_conv \
    --font "$SCRIPT_DIR/$FONT_FILE" \
    --size 24 \
    --bpp 1 \
    --format lvgl \
    --range "$RANGE" \
    --lv-include "lvgl.h" \
    -o "$SCRIPT_DIR/font_noto_tc_24.c"

echo ""
echo "✅ Done! Generated font files:"
ls -lh "$SCRIPT_DIR"/font_noto_tc_*.c
echo ""
echo "⚠️  Check total size. If the firmware exceeds 2MB app partition,"
echo "   consider adjusting partitions.csv or reducing the range."
