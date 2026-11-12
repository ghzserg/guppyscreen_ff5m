#!/usr/bin/env python3
"""Export LVGL C icons as PNG files, to look at them.

    assets/icons_to_png.py OUT_DIR assets/material/*.c

Reads LV_IMG_CF_INDEXED_8BIT icons (see icons_to_indexed.py) and
LV_IMG_CF_TRUE_COLOR_ALPHA ones (their LV_COLOR_DEPTH == 32 block).
Icons with at most 256 distinct RGBA values (fully transparent pixels count
as one) are written as indexed PNGs (palette + tRNS, alpha per entry) at the
smallest bit depth that fits: 1, 2, 4 or 8 bits. Others as RGBA PNGs.
Prints one line per icon: name, distinct colours, PNG format, PNG bytes.
Most icons are white on transparent: view them on a dark background.
"""
import os
import re
import struct
import sys
import zlib


def chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)


def write_png(path, w, h, ctype, depth, rows, extra=b''):
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, depth, ctype, 0, 0, 0)) + extra
    data += chunk(b'IDAT', zlib.compress(b''.join(b'\0' + r for r in rows), 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(data)
    return len(data)


def pack(idx, depth):
    if depth == 8:
        return bytes(idx)
    per = 8 // depth
    out = bytearray()
    for i in range(0, len(idx), per):
        v = 0
        for j in range(per):
            v <<= depth
            if i + j < len(idx):
                v |= idx[i + j]
        out.append(v)
    return bytes(out)


def hex_bytes(text):
    return bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', text))


def read_icon(s):
    """(w, h, pixels as (R, G, B, A) tuples) or None."""
    w = re.search(r'\.header\.w = (\d+)', s)
    h = re.search(r'\.header\.h = (\d+)', s)
    if not (w and h):
        return None
    w, h = int(w.group(1)), int(h.group(1))
    if 'LV_IMG_CF_INDEXED_8BIT' in s:
        arr = re.search(r'_map\[\] = \{(.*?)\};', s, re.S)
        b = hex_bytes(arr.group(1))
        if len(b) < 1024 + w * h:
            return None
        pal = [(b[i * 4 + 2], b[i * 4 + 1], b[i * 4], b[i * 4 + 3]) for i in range(256)]  # stored B, G, R, A
        return w, h, [pal[i] for i in b[1024:1024 + w * h]]
    blk = re.search(r'#if LV_COLOR_DEPTH == 32(.*?)#endif', s, re.S)
    if not blk:
        return None
    b = hex_bytes(blk.group(1))
    if len(b) < w * h * 4:
        return None
    return w, h, [(b[i * 4 + 2], b[i * 4 + 1], b[i * 4], b[i * 4 + 3]) for i in range(w * h)]


out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for src in sys.argv[2:]:
    icon = read_icon(open(src).read())
    if not icon:
        print('skip', src)
        continue
    w, h, px = icon
    px = [(0, 0, 0, 0) if p[3] == 0 else p for p in px]
    name = os.path.splitext(os.path.basename(src))[0]
    path = os.path.join(out, name + '.png')
    colours = sorted(set(px), key=lambda c: (c[3], c))  # transparent first, then a fixed order
    n = len(colours)
    if n <= 256:
        depth = next(d for d in (1, 2, 4, 8) if n <= (1 << d))
        lut = {c: i for i, c in enumerate(colours)}
        extra = chunk(b'PLTE', b''.join(bytes(c[:3]) for c in colours)) + chunk(b'tRNS', bytes(c[3] for c in colours))
        rows = [pack([lut[p] for p in px[y * w:(y + 1) * w]], depth) for y in range(h)]
        size = write_png(path, w, h, 3, depth, rows, extra)
        fmt = f'indexed {depth}-bit'
    else:
        rows = [b''.join(bytes(p) for p in px[y * w:(y + 1) * w]) for y in range(h)]
        size = write_png(path, w, h, 6, 8, rows)
        fmt = 'RGBA'
    print(f'{name} {n} {fmt} {size}')
