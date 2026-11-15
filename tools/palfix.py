#!/usr/bin/env python3
"""Fixes the dips of a palette image (256x1 px): makes its lightness monotonic.

The lightness of a real display changes monotonically with the level, so a
part where it goes back (e.g. by unevenness of the photograph) is an error.
Each such part is replaced by the straight line (in RGB) between the last
level before it and the first level after it that continues the slope:

  lightness
    |  a
    |   \\    ^^^^      a: the last level before the plateau the rise returns to
    |    ----    --   e: the first level beyond that plateau
    |                \\e
    +------------------ level

The direction (darker / lighter with the level) is detected from both ends, or
given by --direction. The lightness is OKLab L.

Example:
  tools/palfix.py assets/palettes/aqm1248a_raw.png assets/palettes/aqm1248a.png
"""

import argparse
import pathlib
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import colorlib  # noqa: E402


def lightness(pal: np.ndarray) -> np.ndarray:
    return colorlib.srgb_to_oklab(pal.astype(np.float64))[:, 0]


def fix(pal: np.ndarray, decreasing: bool) -> tuple:
    """Returns the fixed palette and the replaced ranges [(a, e), ...] (a, e kept)."""
    out = pal.astype(np.float64).copy()
    n = len(out)
    ranges = []
    # Works on y that must not increase
    y = lightness(pal)
    if not decreasing:
        y = -y
    i = 1
    m, first = y[0], 0  # running minimum and where it was first reached
    floor = 0  # end of the last replaced range (kept as it is)
    while i < n:
        if y[i] < m:
            m, first = y[i], i
        elif y[i] > m:
            # A rise: back to the level before the plateau of the minimum, on to
            # the first level below the minimum (or the end)
            a = first - 1 if first > floor else first
            e = i + 1
            while e < n and y[e] >= m:
                e += 1
            if e == n:  # never goes below again: ends on the plateau
                e = n - 1
                out[e] = out[first]
                y[e] = m
            t = (np.arange(a + 1, e) - a) / (e - a)
            out[a + 1:e] = out[a] + (out[e] - out[a]) * t[:, None]
            ranges.append((a, e))
            m, first = y[e], e
            floor = i = e
        i += 1
    return np.clip(np.rint(out), 0, 255).astype(np.uint8), ranges


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("palette", type=pathlib.Path)
    ap.add_argument("output", type=pathlib.Path, nargs="?", help="omitted: only report")
    ap.add_argument("--direction", choices=["auto", "darker", "lighter"], default="auto",
                    help="change of the lightness as the level rises (default: from both ends)")
    args = ap.parse_args()

    pal = colorlib.load_palette(args.palette)
    if args.direction == "auto":
        lt = lightness(pal)
        decreasing = lt[-1] <= lt[0]
    else:
        decreasing = args.direction == "darker"

    fixed, ranges = fix(pal, decreasing)
    print(f"direction: {'darker' if decreasing else 'lighter'} with the level")
    for a, e in ranges:
        before = " ".join(str(int(v)) for v in pal[a:e + 1, 0])
        after = " ".join(str(int(v)) for v in fixed[a:e + 1, 0])
        print(f"levels {a}..{e} (R): {before}\n{'':>{len(str(a)) + len(str(e)) + 15}}-> {after}")
    if not ranges:
        print("no dips")

    lt = lightness(fixed)
    d = np.diff(lt) if decreasing else -np.diff(lt)
    if (d > 1e-9).any():
        print(f"warning: still {int((d > 1e-9).sum())} rises after rounding (colored palette?)")

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(fixed.reshape(1, -1, 3), "RGB").save(args.output)


if __name__ == "__main__":
    main()
