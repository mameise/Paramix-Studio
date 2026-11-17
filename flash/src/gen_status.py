#!/usr/bin/env python3
"""Regenerate English flash status bitmaps using the project's 5x7 font.

Run from any directory. Only the four 128x16 status arrays in assets.h
are replaced; the background artwork and display initialization stay intact.
"""

import re
from pathlib import Path


HERE = Path(__file__).resolve().parent
FONT = HERE.parents[1] / "feld/src/feld/font.h"
ASSETS = HERE / "assets.h"
WIDTH, HEIGHT = 128, 16
BACKGROUND = 0x0883


def render(text, color, font):
    pixels = [BACKGROUND] * (WIDTH * HEIGHT)
    # Match the original status baseline and nine-pixel letter height.
    left = (WIDTH - (len(text) * 8 - 1)) // 2
    for index, char in enumerate(text):
        glyph = font[ord(char) - 32]
        for y in range(9):
            row = glyph[y * 7 // 9]
            for x in range(7):
                if row & (16 >> (x * 5 // 7)):
                    pixels[(y + 5) * WIDTH + left + index * 8 + x] = color
    return pixels


def main():
    font_bytes = [int(value, 16) for value in re.findall(r"0x[0-9a-fA-F]+", FONT.read_text())]
    assert len(font_bytes) == 96 * 7, "Unexpected font dimensions"
    font = [font_bytes[index:index + 7] for index in range(0, len(font_bytes), 7)]
    source = ASSETS.read_text()
    for old_name, name, label, color in (
        ("st_bereit", "st_ready", "READY", 0x7EF1),
        ("st_lesen", "st_reading", "READING", 0xEF7E),
        ("st_schreiben", "st_writing", "WRITING", 0xF5AB),
        ("st_fertig", "st_done", "DONE", 0x7EF1),
    ):
        pixels = render(label, color, font)
        declaration = "static const unsigned short " + name + "[2048]={" + ",".join(f"0x{pixel:04x}" for pixel in pixels) + "};"
        pattern = rf"static const unsigned short (?:{old_name}|{name})\[2048\]=\{{[^}}]*\}};"
        source, count = re.subn(pattern, lambda match: declaration, source)
        assert count == 1, f"Expected exactly one {name} bitmap"
    ASSETS.write_text(source)


if __name__ == "__main__":
    main()
