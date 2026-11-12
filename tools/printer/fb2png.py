#!/usr/bin/env python3
"""Convert a raw AD5M framebuffer dump (800x480, 32 bpp BGRA, stride 3200) to PNG.

  ssh root@ff 'dd if=/dev/fb0 bs=3200 count=480 2>/dev/null' > shot.raw
  python3 fb2png.py shot.raw shot.png
"""
import struct
import sys
import zlib

W, H = 800, 480
raw = open(sys.argv[1], 'rb').read()
assert len(raw) >= W * H * 4, f"short dump: {len(raw)} bytes"
rows = bytearray()
for y in range(H):
    rows.append(0)
    line = raw[y * W * 4:(y + 1) * W * 4]
    for x in range(W):
        b, g, r = line[x * 4], line[x * 4 + 1], line[x * 4 + 2]
        rows += bytes((r, g, b))

def chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)

png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(bytes(rows), 6)) + chunk(b'IEND', b'')
open(sys.argv[2], 'wb').write(png)
