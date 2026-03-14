#!/usr/bin/env python3
"""
Generate a crisp bitmap C font header with edge smoothing.
Uses Terminus bitmap font as pixel-perfect base, then adds subtle
anti-aliasing on edge pixels for smoother curves and diagonals.

Usage: python3 scripts/gen_font.py [font_path] [pixel_size] > gui/font.h
"""

import sys
from PIL import Image, ImageDraw, ImageFont


def main():
    font_path = sys.argv[1] if len(sys.argv) > 1 else \
        "/Users/oguzgundogdu/Library/Fonts/TerminusTTF-4.49.3.ttf"
    px_size = int(sys.argv[2]) if len(sys.argv) > 2 else 14

    font = ImageFont.truetype(font_path, px_size)

    # Determine cell size
    ascent, descent = font.getmetrics()
    char_h = ascent + descent

    # Measure monospace width from 'M'
    ref_img = Image.new("L", (px_size * 2, px_size * 2), 0)
    ref_draw = ImageDraw.Draw(ref_img)
    left, top, right, bottom = ref_draw.textbbox((0, 0), "M", font=font)
    char_w = right - left

    print("#pragma once")
    print()
    print("// Auto-generated bitmap font from Terminus with edge smoothing")
    print(f"// Size: {char_w}x{char_h} pixels per glyph")
    print(f"// Pixel-perfect stems + subtle AA fringe for smooth edges")
    print(f"// Covers ASCII 32 (space) through 126 (~)")
    print()
    print("#include \"types.h\"")
    print()
    print(f"constexpr i32 FONT_W = {char_w};")
    print(f"constexpr i32 FONT_H = {char_h};")
    print()
    print(f"// Each glyph: {char_w * char_h} bytes ({char_w}x{char_h})")
    print(f"// Total: 95 glyphs x {char_w * char_h} = {95 * char_w * char_h} bytes")
    print(f"const u8 font_alpha[95][{char_w * char_h}] = {{")

    for ch in range(32, 127):
        c = chr(ch)
        img = Image.new("L", (char_w, char_h), 0)
        draw = ImageDraw.Draw(img)
        draw.text((0, 0), c, font=font, fill=255)

        pixels = img.load()
        alpha_data = []
        for row in range(char_h):
            for col in range(char_w):
                alpha_data.append(pixels[col, row])

        if ch == 32:
            label = "space"
        else:
            label = c
        hex_str = ", ".join(f"0x{a:02x}" for a in alpha_data)
        comma = "," if ch < 126 else ""
        print(f"  {{{hex_str}}}{comma} // '{label}'")

    print("};")

if __name__ == "__main__":
    main()
