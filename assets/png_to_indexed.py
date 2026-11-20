#!/usr/bin/env python3
"""Write an LVGL C icon (LV_IMG_CF_INDEXED_8BIT) from a PNG.

    assets/png_to_indexed.py [--colour] <in.png> <name> > assets/material/<name>.c

Same format as assets/icons_to_indexed.py produces: a 256-entry
lv_color32_t palette (B, G, R, A) followed by one index byte per pixel.
Every pixel is drawn white with the PNG's alpha, like the other icons
(Material Symbols PNGs come in a single grey such as #E3E3E3). With
--colour (8-bit RGBA only) the PNG's colours stay: a black mark on a white
icon then shows on any background.

Only 8-bit RGBA, grey+alpha or palette (with tRNS), non-interlaced PNGs:
what Google's Material Symbols downloads give. No PIL needed.
"""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise SystemExit(f'{path}: not a PNG')
    pos, idat, ihdr, trns = 8, b'', None, b''
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b'IHDR':
            ihdr = struct.unpack('>IIBBBBB', chunk)
        elif kind == b'IDAT':
            idat += chunk
        elif kind == b'tRNS':
            trns = chunk
    w, h, depth, ctype, _, _, interlace = ihdr
    channels = {6: 4, 4: 2, 3: 1}.get(ctype)
    if depth != 8 or channels is None or interlace:
        raise SystemExit(f'{path}: need 8-bit RGBA, grey+alpha or palette, non-interlaced')
    raw = zlib.decompress(idat)
    stride = w * channels
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = prev[x]
            c = prev[x - channels] if x >= channels else 0
            if f == 1:
                line[x] = (line[x] + a) & 0xff
            elif f == 2:
                line[x] = (line[x] + b) & 0xff
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 0xff
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xff
        rows.append(line)
        prev = line
    if ctype == 3:  # palette: alpha from tRNS, opaque past its end
        alpha = [trns[i] if i < len(trns) else 255 for row in rows for i in row]
    else:
        alpha = [row[x * channels + channels - 1] for row in rows for x in range(w)]
    rgb = [tuple(row[x * 4:x * 4 + 3]) for row in rows for x in range(w)] if ctype == 6 else None
    return w, h, alpha, rgb


def main():
    colour = sys.argv[1] == '--colour'
    path, name = sys.argv[1 + colour], sys.argv[2 + colour]
    w, h, alpha, rgb = read_png(path)
    if colour and rgb is None:
        raise SystemExit(f'{path}: --colour needs an 8-bit RGBA PNG')
    # (B, G, R, A), как в палитре LVGL
    px = [(0, 0, 0, 0) if a == 0 else (rgb[i][2], rgb[i][1], rgb[i][0], a) if colour else (0xff, 0xff, 0xff, a)
          for i, a in enumerate(alpha)]
    if len(set(px)) > 256:
        raise SystemExit(f'{path}: more than 256 colours')
    palette = sorted(set(px), key=lambda c: (c[3], c))
    index = {c: i for i, c in enumerate(palette)}
    palette += [(0, 0, 0, 0)] * (256 - len(palette))
    pal = bytes(v for c in palette for v in c)
    idx = bytes(index[p] for p in px)

    def rows(data, per_row):
        return ''.join('  ' + ''.join(f'0x{v:02x}, ' for v in data[i:i + per_row]) + '\n'
                       for i in range(0, len(data), per_row))

    attr = 'LV_ATTRIBUTE_IMG_' + name.upper()
    print(f'''#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif


#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef {attr}
#define {attr}
#endif

/* LV_IMG_CF_INDEXED_8BIT: 256 x lv_color32_t palette (B, G, R, A), then one
 * index per pixel. Converted from {path.split('/')[-1]} by assets/png_to_indexed.py. */
const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST {attr} uint8_t {name}_map[] = {{
  /*Palette*/
{rows(pal, 32)}  /*Pixels*/
{rows(idx, w)}}};

const lv_img_dsc_t {name} = {{
  .header.cf = LV_IMG_CF_INDEXED_8BIT,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = {w},
  .header.h = {h},
  .data_size = {len(pal) + len(idx)},
  .data = {name}_map,
}};''')


main()
