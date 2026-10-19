#!/usr/bin/env python3
"""Generate a 16-bit BT.2020/PQ PNG for the M0 HDR pipeline spike.

The image is split vertically: the left half uses one 10-bit PQ code and the
right half uses another. A cICP chunk is written explicitly because FFmpeg's PNG
encoder does not emit colour metadata (see docs/DESIGN_NOTES.md).

Output samples are full-range 16-bit values. The PQ code is encoded as
round(code * 65535); the cICP transfer function is 16 (SMPTE ST 2084).
"""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path


def png_chunk(chunk_type: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + chunk_type
        + payload
        + struct.pack(">I", zlib.crc32(chunk_type + payload) & 0xFFFFFFFF)
    )


def write_pq_pattern(path: Path, width: int, height: int, left_code: int, right_code: int) -> None:
    if not 0 <= left_code <= 1023 or not 0 <= right_code <= 1023:
        raise ValueError("PQ codes must be in [0, 1023]")

    left_sample = round(left_code / 1023 * 65535)
    right_sample = round(right_code / 1023 * 65535)

    left_pixel = struct.pack(">HHH", left_sample, left_sample, left_sample)
    right_pixel = struct.pack(">HHH", right_sample, right_sample, right_sample)
    split = width // 2
    row = left_pixel * split + right_pixel * (width - split)

    raw = bytearray()
    for _ in range(height):
        raw.append(0)  # PNG filter type: none
        raw.extend(row)

    ihdr = struct.pack(">IIBBBBB", width, height, 16, 2, 0, 0, 0)
    # colour primaries 9 = BT.2020, transfer 16 = PQ, matrix 0 = RGB, full range
    cicp = bytes([9, 16, 0, 1])

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"cICP", cicp)
        + png_chunk(b"IDAT", zlib.compress(bytes(raw), level=9))
        + png_chunk(b"IEND", b"")
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("patterns/m0-pq-653-712.png"))
    parser.add_argument("--width", type=int, default=2560)
    parser.add_argument("--height", type=int, default=1440)
    parser.add_argument("--left-code", type=int, default=653, help="10-bit PQ code for the left half")
    parser.add_argument("--right-code", type=int, default=712, help="10-bit PQ code for the right half")
    arguments = parser.parse_args()

    write_pq_pattern(arguments.output, arguments.width, arguments.height, arguments.left_code, arguments.right_code)
    print(f"wrote {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
