#!/usr/bin/env python3
"""Render the calibration pattern to manual/images/calibration.png.

The manual shows what `screen calibration` puts on the panel. Drawing that picture by hand would
be one more thing that can quietly stop matching the firmware, so this reproduces the pattern the
way draw_calibration() builds it -- and reads the geometry constants straight out of
components/video/video_player.cpp rather than repeating them, so changing one there and rerunning
this is enough.

    make docs-image

With --check it renders to memory and compares, without writing: that is what `make docs-check`
runs, so a change to the pattern that never reached the manual fails the build rather than leaving
the page showing the old shape.

Standard library only, like tools/check_command_docs.py: no Pillow, and no docs venv needed. The
PNG is written by hand, which for a two-colour 240x240 image is a dozen lines of zlib.
"""

from __future__ import annotations

import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "components" / "video" / "video_player.cpp"
OUT = ROOT / "manual" / "images" / "calibration.png"

# The panel, from components/board_ws128/include/board.h.
W = H = 240
# Nearest-neighbour, so the two-pixel lines stay two crisp pixels wide at any size on the page.
SCALE = 2

# Off the round panel's visible area: transparent, so the page's own background shows through
# and the picture reads as a disc in either theme.
CLEAR = (0, 0, 0, 0)
BG = (0, 0, 0, 255)
FG = (255, 255, 255, 255)
# The panel's rim, drawn just outside the visible area -- not something the firmware draws, but
# without it an unlit panel has no edge against a dark page. Mid grey shows up against both.
RIM = (107, 107, 107, 255)
RIM_PX = 2
PAD = RIM_PX + 1


def constants() -> dict[str, int]:
    """CALIB_* from the firmware, so the picture cannot drift from what the board draws."""
    text = SOURCE.read_text(encoding="utf-8")
    found = dict(re.findall(r"constexpr int (CALIB_\w+)\s*=\s*(\d+)", text))
    wanted = ["CALIB_LINE", "CALIB_ARROW_W", "CALIB_ARROW_H", "CALIB_ARROW_GAP"]
    missing = [name for name in wanted if name not in found]
    if missing:
        print(
            f"error: {', '.join(missing)} not found in {SOURCE.relative_to(ROOT)};\n"
            "       has draw_calibration() changed shape?",
            file=sys.stderr,
        )
        raise SystemExit(2)
    return {name: int(found[name]) for name in wanted}


def pattern(c: dict[str, int]) -> list[list[bool]]:
    """draw_calibration(), pixel for pixel: the crosshair, then the arrowhead above the crossing."""
    line, aw, ah, gap = (
        c["CALIB_LINE"],
        c["CALIB_ARROW_W"],
        c["CALIB_ARROW_H"],
        c["CALIB_ARROW_GAP"],
    )
    mid = W // 2
    on = [[False] * W for _ in range(H)]

    for y in range(mid - line // 2, mid - line // 2 + line):
        for x in range(W):
            on[y][x] = True
    for x in range(mid - line // 2, mid - line // 2 + line):
        for y in range(H):
            on[y][x] = True

    ax, ay = mid - aw // 2, mid - line // 2 - gap - ah
    for row in range(ah):
        half = max(1, (row + 1) * (aw // 2) // ah)
        for x in range(aw // 2 - half, aw // 2 + half):
            on[ay + row][ax + x] = True
    return on


def png(rows: list[bytes], width: int, height: int) -> bytes:
    """8-bit RGBA, one IDAT, every scanline filtered as 'none'."""

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + kind
            + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
        )

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    raw = b"".join(b"\x00" + row for row in rows)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def main() -> int:
    on = pattern(constants())
    # The visible area is a circle centred on the seam between pixels 119 and 120.
    centre = (W - 1) / 2 + PAD
    radius = W / 2
    width, height = W + 2 * PAD, H + 2 * PAD

    rows: list[bytes] = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            r2 = (x - centre) ** 2 + (y - centre) ** 2
            if r2 <= radius**2:
                lit = 0 <= y - PAD < H and 0 <= x - PAD < W and on[y - PAD][x - PAD]
                row += bytes(FG if lit else BG)
            elif r2 <= (radius + RIM_PX) ** 2:
                row += bytes(RIM)
            else:
                row += bytes(CLEAR)
        scaled = scale_row(row, SCALE)
        rows.extend([scaled] * SCALE)   # and down, for the same reason

    image = png(rows, width * SCALE, height * SCALE)
    if "--check" in sys.argv[1:]:
        current = OUT.read_bytes() if OUT.exists() else b""
        if current == image:
            print(f"{OUT.relative_to(ROOT)} is up to date")
            return 0
        print(
            f"error: {OUT.relative_to(ROOT)} does not match the pattern the firmware draws;\n"
            "       run `make docs-image` and commit the result",
            file=sys.stderr,
        )
        return 1

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(image)
    print(f"{OUT.relative_to(ROOT)}: {width * SCALE}x{height * SCALE}, {len(image)} bytes")
    return 0


def scale_row(row: bytearray, scale: int) -> bytes:
    """Each pixel repeated `scale` times across."""
    out = bytearray()
    for i in range(0, len(row), 4):
        out += row[i : i + 4] * scale
    return bytes(out)


if __name__ == "__main__":
    sys.exit(main())
