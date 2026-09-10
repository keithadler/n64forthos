#!/usr/bin/env python3
"""Read the console back out of the framebuffer.

The kernel draws text as exact 8x16 glyph bitmaps, so the screen can be
decoded to characters again by matching each cell against the same font the
kernel used.  That turns "does the console say the right thing" into a string
comparison instead of an eyeball.
"""
import os
import re

FONT_H = os.path.join(os.path.dirname(__file__), "..", "src", "font.h")
CELL_W, CELL_H = 8, 16
FIRST = 0x20


def load_font(path=FONT_H):
    rows = re.findall(r"\{((?:0x[0-9a-f]{2},){15}0x[0-9a-f]{2})\}", open(path).read())
    glyphs = {}
    for i, row in enumerate(rows):
        bits = tuple(int(b, 16) for b in row.split(","))
        glyphs.setdefault(bits, chr(FIRST + i))
    return glyphs


def decode(px, glyphs=None):
    """px is a list of rows of (r, g, b).  Returns a list of text lines."""
    glyphs = glyphs or load_font()
    h, w = len(px), len(px[0])
    out = []
    for row in range(h // CELL_H):
        line = []
        for col in range(w // CELL_W):
            counts = {}
            cell = []
            for y in range(CELL_H):
                for x in range(CELL_W):
                    p = px[row * CELL_H + y][col * CELL_W + x]
                    counts[p] = counts.get(p, 0) + 1
                    cell.append(p)
            bg = max(counts, key=counts.get)
            bits = []
            for y in range(CELL_H):
                b = 0
                for x in range(CELL_W):
                    if cell[y * CELL_W + x] != bg:
                        b |= 0x80 >> x
                bits.append(b)
            line.append(glyphs.get(tuple(bits), "�"))
        out.append("".join(line).rstrip())
    return out


def find(lines, needle):
    return [i for i, line in enumerate(lines) if needle in line]
