#!/usr/bin/env python3
"""Rewrite LVGL C icons from true colour + alpha to 8-bit indexed.

    assets/icons_to_indexed.py assets/material/*.c

The icons are 64x64 glyphs with at most ~200 distinct (colour, alpha)
values, so LV_IMG_CF_INDEXED_8BIT keeps them exactly: a 256-entry
lv_color32_t palette (bytes B, G, R, A, alpha per entry) followed by one
index byte per pixel. 5 KB per icon instead of 16 KB of ARGB8888, and the
same data for every LV_COLOR_DEPTH.

LVGL's built-in decoder draws these line by line and without zoom;
patches/0007 lets it hand the data to the whole-image path of patch 0006
instead.

The pixels are taken from the file's LV_COLOR_DEPTH == 32 block. Files
already indexed are skipped; an icon with more than 256 values is an error.
"""
import re
import sys

HEADER = '''/* LV_IMG_CF_INDEXED_8BIT: 256 x lv_color32_t palette (B, G, R, A), then one
 * index per pixel. Converted from ARGB8888 by assets/icons_to_indexed.py. */
'''


def convert(path):
    s = open(path).read()
    if 'LV_IMG_CF_INDEXED_8BIT' in s:
        return 'already indexed'
    arr = re.search(r'^const [^\n]*uint8_t (\w+)_map\[\] = \{\n', s, re.M)
    dsc = re.search(r'^const lv_img_dsc_t (\w+) = \{(.*?)\};', s, re.M | re.S)
    blk = re.search(r'#if LV_COLOR_DEPTH == 32\n(.*?)#endif', s, re.S)
    if not (arr and dsc and blk):
        raise SystemExit(f'{path}: unexpected layout')
    body = dsc.group(2)
    if 'LV_IMG_CF_TRUE_COLOR_ALPHA' not in body:
        raise SystemExit(f'{path}: not LV_IMG_CF_TRUE_COLOR_ALPHA')
    w = int(re.search(r'\.header\.w = (\d+)', body).group(1))
    h = int(re.search(r'\.header\.h = (\d+)', body).group(1))
    b = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', blk.group(1)))
    if len(b) != w * h * 4:
        raise SystemExit(f'{path}: {len(b)} bytes, expected {w * h * 4}')

    px = []
    for i in range(w * h):
        B, G, R, A = b[i * 4:i * 4 + 4]
        px.append((0, 0, 0, 0) if A == 0 else (B, G, R, A))  # hidden colour of transparent pixels is irrelevant
    palette = sorted(set(px), key=lambda c: (c[3], c))
    if len(palette) > 256:
        raise SystemExit(f'{path}: {len(palette)} colours, more than 256')
    index = {c: i for i, c in enumerate(palette)}
    palette += [(0, 0, 0, 0)] * (256 - len(palette))

    def rows(data, per_row):
        return ''.join('  ' + ''.join(f'0x{v:02x}, ' for v in data[i:i + per_row]) + '\n'
                       for i in range(0, len(data), per_row))

    pal = bytes(v for c in palette for v in c)
    idx = bytes(index[p] for p in px)
    name = arr.group(1)
    new_arr = (HEADER + arr.group(0)
               + '  /*Palette*/\n' + rows(pal, 32)
               + '  /*Pixels*/\n' + rows(idx, w)
               + '};\n')
    new_dsc = (f'const lv_img_dsc_t {dsc.group(1)} = {{\n'
               f'  .header.cf = LV_IMG_CF_INDEXED_8BIT,\n'
               f'  .header.always_zero = 0,\n'
               f'  .header.reserved = 0,\n'
               f'  .header.w = {w},\n'
               f'  .header.h = {h},\n'
               f'  .data_size = {len(pal) + len(idx)},\n'
               f'  .data = {name}_map,\n'
               f'}};')
    start = arr.start()
    end = dsc.end()
    between = s[s.index('};', blk.end()) + 2:dsc.start()]
    out = s[:start] + new_arr + between + new_dsc + s[end:]
    open(path, 'w').write(out)
    return f'{len(set(px))} colours'


for p in sys.argv[1:]:
    print(p, convert(p))
