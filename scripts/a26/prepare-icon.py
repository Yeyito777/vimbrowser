#!/usr/bin/env python3
"""Deterministically generate the 220x220 A26 BGRX launcher icon."""

from __future__ import annotations

import argparse
from pathlib import Path

import PIL
from PIL import Image

PROJECT_ROOT = Path(__file__).resolve().parents[2]
SOURCE = PROJECT_ROOT / "assets" / "a26-browser.png"
OUTPUT = PROJECT_ROOT / "assets" / "a26-browser.bgrx"
SIZE = (220, 220)
PINNED_PILLOW = "12.1.1"


def generated_bytes() -> bytes:
    if PIL.__version__ != PINNED_PILLOW:
        raise SystemExit(f"Pillow {PINNED_PILLOW} is required; found {PIL.__version__}")
    with Image.open(SOURCE) as source:
        rgb = source.convert("RGB").resize(SIZE, Image.Resampling.LANCZOS).tobytes()
    output = bytearray(SIZE[0] * SIZE[1] * 4)
    for input_index, output_index in zip(range(0, len(rgb), 3), range(0, len(output), 4)):
        red, green, blue = rgb[input_index : input_index + 3]
        output[output_index : output_index + 4] = bytes((blue, green, red, 0))
    return bytes(output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    expected = generated_bytes()
    if arguments.check:
        if not OUTPUT.exists() or OUTPUT.read_bytes() != expected:
            raise SystemExit("generated BGRX icon is missing or stale")
        print("A26 browser icon is reproducible and current")
    else:
        OUTPUT.write_bytes(expected)
        print(f"wrote {len(expected)} bytes to {OUTPUT.relative_to(PROJECT_ROOT)}")


if __name__ == "__main__":
    main()
