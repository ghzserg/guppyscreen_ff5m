#!/usr/bin/env python3
"""Convert test framebuffer PPM images into portable PNG files (stdlib only)."""

from pathlib import Path
import struct
import sys
import zlib


def chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data))
    )


def convert(src: Path) -> Path:
    data = src.read_bytes()
    header, pixels = data.split(b"\n255\n", 1)
    magic, dimensions = header.split(b"\n", 1)
    if magic != b"P6":
        raise ValueError(f"Not a binary PPM: {src}")
    width, height = map(int, dimensions.split())
    row = width * 3
    if len(pixels) != row * height:
        raise ValueError(f"Invalid framebuffer size: {src}")
    raw = b"".join(b"\x00" + pixels[y * row:(y + 1) * row] for y in range(height))
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 6))
        + chunk(b"IEND", b"")
    )
    dest = src.with_suffix(".png")
    dest.write_bytes(png)
    src.unlink()
    print(dest)
    return dest


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python3 ci/ppm_to_png.py <screenshot-directory>")
    images = sorted(Path(sys.argv[1]).rglob("*.ppm"))
    if not images:
        raise SystemExit("No PPM screenshots found")
    for image in images:
        convert(image)
