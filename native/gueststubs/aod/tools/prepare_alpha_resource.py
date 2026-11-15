#!/usr/bin/env python3
"""Rebuild the fixed single-theme 8-bit alpha PNG from its verified local raw mask."""

import argparse
import hashlib
from pathlib import Path
import struct
import sys
import zlib

WIDTH = 1216
HEIGHT = 2688
EXPECTED_ALPHA_SHA256 = "019bfeea5f8e5109befa231f8febce04732320ff1e256e51fa28b092e6231c7a"
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff))


def _check_size(alpha: bytes, width: int, height: int) -> None:
    if not (1 <= width <= 4096 and 1 <= height <= 4096) or len(alpha) != width * height:
        raise ValueError("invalid alpha dimensions or byte length")


def build_png(alpha: bytes, width: int, height: int) -> bytes:
    _check_size(alpha, width, height)
    rows = b"".join(b"\0" + alpha[y * width:(y + 1) * width] for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    return PNG_SIGNATURE + _chunk(b"IHDR", header) + _chunk(b"IDAT", zlib.compress(rows, 9)) + _chunk(b"IEND", b"")


def validate_png(png: bytes, alpha: bytes, width: int, height: int) -> None:
    """Accept only our metadata-free, non-interlaced grayscale PNG with exact pixels."""
    _check_size(alpha, width, height)
    if not png.startswith(PNG_SIGNATURE):
        raise ValueError("invalid PNG signature")
    pos = len(PNG_SIGNATURE)
    chunks = []
    while pos < len(png):
        if len(png) - pos < 12:
            raise ValueError("truncated PNG chunk")
        length = struct.unpack_from(">I", png, pos)[0]
        end = pos + 12 + length
        if end > len(png):
            raise ValueError("truncated PNG data")
        kind = png[pos + 4:pos + 8]
        payload = png[pos + 8:pos + 8 + length]
        stored_crc = struct.unpack_from(">I", png, pos + 8 + length)[0]
        if stored_crc != zlib.crc32(kind + payload) & 0xffffffff:
            raise ValueError("invalid PNG checksum")
        chunks.append((kind, payload))
        pos = end
    if [kind for kind, _ in chunks] != [b"IHDR", b"IDAT", b"IEND"] or chunks[-1][1] != b"":
        raise ValueError("unexpected PNG chunks or metadata")
    expected_header = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    if chunks[0][1] != expected_header:
        raise ValueError("PNG is not the expected 8-bit grayscale image")
    length = height * (width + 1)
    try:
        decoder = zlib.decompressobj()
        rows = decoder.decompress(chunks[1][1], length + 1)
        if len(rows) != length or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
            raise ValueError("invalid PNG compressed data")
    except zlib.error as error:
        raise ValueError("invalid PNG compressed data") from error
    for y in range(height):
        start = y * (width + 1)
        if rows[start] != 0 or rows[start + 1:start + 1 + width] != alpha[y * width:(y + 1) * width]:
            raise ValueError("PNG pixels differ from the source alpha")


def _read_alpha(source: Path) -> bytes:
    # A bounded read rejects both truncated and oversized inputs before use.
    with source.open("rb") as stream:
        alpha = stream.read(WIDTH * HEIGHT + 1)
    _check_size(alpha, WIDTH, HEIGHT)
    if hashlib.sha256(alpha).hexdigest() != EXPECTED_ALPHA_SHA256:
        raise ValueError("source alpha SHA-256 mismatch")
    return alpha


def prepare(source: Path, output: Path) -> str:
    alpha = _read_alpha(source)
    png = build_png(alpha, WIDTH, HEIGHT)
    validate_png(png, alpha, WIDTH, HEIGHT)
    # Exclusive creation never replaces a pre-existing user or project file.
    with output.open("xb") as stream:
        stream.write(png)
    return hashlib.sha256(png).hexdigest()


def verify(source: Path, image: Path) -> str:
    alpha = _read_alpha(source)
    limit = (WIDTH + 1) * HEIGHT * 2 + 1024
    with image.open("rb") as stream:
        png = stream.read(limit + 1)
    if len(png) > limit:
        raise ValueError("PNG is too large")
    validate_png(png, alpha, WIDTH, HEIGHT)
    return hashlib.sha256(png).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--alpha", required=True, type=Path, help="explicit verified local alpha.raw")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--output", type=Path, help="new output PNG; never overwritten")
    mode.add_argument("--check", type=Path, help="verify an existing PNG without modifying it")
    args = parser.parse_args()
    try:
        digest = prepare(args.alpha, args.output) if args.output else verify(args.alpha, args.check)
    except (OSError, ValueError) as error:
        print(f"alpha resource preparation failed: {error}", file=sys.stderr)
        return 1
    print(f"{'created' if args.output else 'verified'} {WIDTH}x{HEIGHT} grayscale PNG; SHA-256 {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
