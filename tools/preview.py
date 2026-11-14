#!/usr/bin/env python3
"""Predicts the apparent colors of an image on the display.

Converts the image as the device does (RGB565 -> LUT -> palette color) and
writes a PNG of three panels side by side: the input (RGB565), through the
LUT, and through the gray conversion (PUSH_FLAG_USE_LUT off) for comparison.

Example:
  tools/preview.py assets/images/tg12864b.png assets/palettes/tg12864b.png preview.png
"""

import argparse
import pathlib
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import colorlib  # noqa: E402


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", type=pathlib.Path)
    ap.add_argument("palette", type=pathlib.Path)
    ap.add_argument("output", type=pathlib.Path)
    ap.add_argument("--rbits", type=int, default=4)
    ap.add_argument("--gbits", type=int, default=5)
    ap.add_argument("--bbits", type=int, default=4)
    ap.add_argument("--scale", type=int, default=4, help="magnification of the output")
    args = ap.parse_args()

    pal = colorlib.load_palette(args.palette)
    rgb565 = colorlib.rgb888_to_rgb565(colorlib.load_rgb(args.image))

    lut = colorlib.Lut565(pal, args.rbits, args.gbits, args.bbits)
    panels = [colorlib.rgb565_to_rgb888(rgb565), pal[lut.lookup(rgb565)], pal[colorlib.rgb565_to_gray(rgb565)]]
    out = np.concatenate([np.pad(p, ((2, 2), (2, 2), (0, 0)), constant_values=40) for p in panels], 1)
    im = Image.fromarray(out.astype(np.uint8))
    im = im.resize((im.width * args.scale, im.height * args.scale), Image.NEAREST)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    im.save(args.output)


if __name__ == "__main__":
    main()
