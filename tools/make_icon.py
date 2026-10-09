#!/usr/bin/env python3
"""Draws the rewrite's own program icon (a lander craft over a planet; not the original's artwork)
and writes src/win/lander.ico (256 px as PNG, 48/32/16 px as 32-bit bitmaps).

usage: python3 tools/make_icon.py"""
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "src", "win", "lander.ico")


def inside_poly(x, y, pts):
    c = False
    j = len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            c = not c
        j = i
    return c


def thick_line(x0, y0, x1, y1, w):
    dx, dy = x1 - x0, y1 - y0
    n = (dx * dx + dy * dy) ** 0.5
    ox, oy = -dy / n * w / 2, dx / n * w / 2
    return [(x0 + ox, y0 + oy), (x1 + ox, y1 + oy), (x1 - ox, y1 - oy), (x0 - ox, y0 - oy)]


# the picture on a 256 x 256 canvas, back to front: (kind, shape, colour)
CYAN, CYAN_HI, METAL, DARK = (0, 190, 225), (110, 235, 255), (165, 180, 200), (12, 28, 48)
SHAPES = [
    ("circle", (128, 560, 350), (34, 50, 78)),             # the planet
    ("circle", (128, 560, 342), (22, 34, 56)),
    ("poly", thick_line(64, 140, 40, 196, 10), METAL),       # legs and feet
    ("poly", thick_line(192, 140, 216, 196, 10), METAL),
    ("poly", thick_line(24, 198, 58, 198, 9), METAL),
    ("poly", thick_line(198, 198, 232, 198, 9), METAL),
    ("poly", [(108, 150), (148, 150), (126 + 2, 236), (128 - 2, 236)], (255, 140, 30)),   # flame
    ("poly", [(116, 150), (140, 150), (128, 206)], (255, 225, 110)),
    ("poly", [(104, 132), (152, 132), (160, 156), (96, 156)], (120, 130, 150)),           # nozzle
    ("poly", [(84, 52), (172, 52), (204, 98), (180, 142), (76, 142), (52, 98)], CYAN),    # body
    ("poly", [(84, 52), (172, 52), (204, 98), (52, 98)], CYAN_HI),
    ("poly", [(102, 70), (154, 70), (162, 92), (94, 92)], DARK),                          # window
]


def colour_at(x, y):
    r, g, b = 10 + 8 * y / 256, 16 + 14 * y / 256, 34 + 30 * (1 - y / 256)   # the background
    for kind, s, col in SHAPES:
        if kind == "poly":
            hit = inside_poly(x, y, s)
        else:
            cx, cy, rad = s
            hit = (x - cx) ** 2 + (y - cy) ** 2 <= rad * rad
        if hit:
            r, g, b = col
    return r, g, b


def in_rounded(x, y, r=44):  # the icon's rounded square
    cx = min(max(x, r), 256 - r)
    cy = min(max(y, r), 256 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def render(size, ss=4):
    rows = []
    k = 256 / size
    for py in range(size):
        row = []
        for px in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    x = (px + (sx + 0.5) / ss) * k
                    y = (py + (sy + 0.5) / ss) * k
                    if in_rounded(x, y):
                        r, g, b = colour_at(x, y)
                        acc[0] += r
                        acc[1] += g
                        acc[2] += b
                        acc[3] += 255
            n = ss * ss
            a = acc[3] / n
            if a > 0:   # straight (not premultiplied) colour
                row.append((int(acc[0] / n * 255 / a + 0.5), int(acc[1] / n * 255 / a + 0.5),
                            int(acc[2] / n * 255 / a + 0.5), int(a + 0.5)))
            else:
                row.append((0, 0, 0, 0))
        rows.append(row)
    return rows


def png(rows):
    size = len(rows)
    raw = b"".join(b"\0" + bytes(c for px in row for c in px) for row in rows)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def bmp(rows):  # an icon's DIB: 32-bit BGRA bottom-up, then the (unused) AND mask
    size = len(rows)
    head = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = b"".join(bytes((b, g, r, a)) for row in reversed(rows) for (r, g, b, a) in row)
    mask = b"\0" * (((size + 31) // 32) * 4 * size)
    return head + pixels + mask


def main():
    images = [(256, png(render(256, 3)))] + [(s, bmp(render(s))) for s in (48, 32, 16)]
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for size, data in images:
        out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    out += b"".join(data for _, data in images)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(out)
    print("written", os.path.normpath(OUT), len(out), "bytes")


if __name__ == "__main__":
    main()
