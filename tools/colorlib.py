"""Common color processing of the pdmfrc tools: RGB565, OKLab and LUTs.

The LUTs follow the same algorithm as Pdm::initLut() (src/lut.cpp), but in
floating point: a color almost equidistant from two palette entries may
rarely map to a different level than on the device (fixed point).
"""

import numpy as np
from PIL import Image


def load_rgb(path) -> np.ndarray:
    """Loads an image as uint8 (H, W, 3)."""
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.uint8)


def rgb888_to_rgb565(rgb: np.ndarray) -> np.ndarray:
    """uint8 (..., 3) -> RGB565 (uint16, native order), each component rounded."""
    c = rgb.astype(np.int32)
    r = (c[..., 0] * 31 + 127) // 255
    g = (c[..., 1] * 63 + 127) // 255
    b = (c[..., 2] * 31 + 127) // 255
    return ((r << 11) | (g << 5) | b).astype(np.uint16)


def rgb565_to_rgb888(v: np.ndarray) -> np.ndarray:
    """RGB565 (native order) -> uint8 (..., 3), extended by bit replication."""
    v = v.astype(np.int32)
    r = (v >> 11) & 31
    g = (v >> 5) & 63
    b = v & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.uint8)


def rgb565_to_gray(v: np.ndarray) -> np.ndarray:
    """RGB565 (native order) -> gray value, the same integer arithmetic as Pdm::push()."""
    c = rgb565_to_rgb888(v).astype(np.int32)
    return ((c[..., 0] * 77 + c[..., 1] * 150 + c[..., 2] * 29 + 128) >> 8).astype(np.uint8)


def byteswap16(v: np.ndarray) -> np.ndarray:
    v = v.astype(np.uint16)
    return ((v >> 8) | (v << 8)).astype(np.uint16)


def srgb_to_oklab(rgb: np.ndarray) -> np.ndarray:
    """sRGB (float 0..1, (..., 3)) -> OKLab."""
    c = np.asarray(rgb, dtype=np.float64)
    lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    m1 = np.array([
        [0.4122214708, 0.5363325363, 0.0514459929],
        [0.2119034982, 0.6806995451, 0.1073969566],
        [0.0883024619, 0.2817188376, 0.6299787005],
    ])
    m2 = np.array([
        [0.2104542553, 0.7936177850, -0.0040720468],
        [1.9779984951, -2.4285922050, 0.4505937099],
        [0.0259066487, 0.7827717662, -0.8086783821],
    ])
    return np.cbrt(lin @ m1.T) @ m2.T


def load_palette(path) -> np.ndarray:
    """Loads a palette image (256x1) as uint8 (256, 3). The index is the level."""
    pal = load_rgb(path)
    if pal.shape[:2] != (1, 256):
        raise SystemExit(f"{path}: palette must be 256x1, got {pal.shape[1]}x{pal.shape[0]}")
    return pal[0]


def nearest_index(rgb: np.ndarray, palette: np.ndarray) -> np.ndarray:
    """The palette index nearest in OKLab to each color (uint8 (N, 3))."""
    p = srgb_to_oklab(palette / 255.0)
    x = srgb_to_oklab(np.asarray(rgb).reshape(-1, 3) / 255.0)
    out = np.empty(len(x), dtype=np.uint8)
    for i in range(0, len(x), 4096):
        d = ((x[i:i + 4096, None, :] - p[None, :, :]) ** 2).sum(-1)
        out[i:i + 4096] = d.argmin(1)
    return out


def cell_center8(index: np.ndarray, bits: int, full_bits: int) -> np.ndarray:
    """8-bit value of the center of a LUT cell, rounded half to even (as src/lut.cpp)."""
    w = 1 << (full_bits - bits)
    num = (2 * np.asarray(index) * w + w - 1) * 255
    den = 2 * ((1 << full_bits) - 1)
    q, r = np.divmod(num, den)
    r2 = 2 * r
    return q + ((r2 > den) | ((r2 == den) & ((q & 1) == 1)))


class Lut565:
    """LUT indexed by the upper bits of the RGB565 components."""

    def __init__(self, palette: np.ndarray, rbits: int = 4, gbits: int = 5, bbits: int = 4):
        if not (1 <= rbits <= 5 and 1 <= gbits <= 6 and 1 <= bbits <= 5):
            raise SystemExit("bits out of range (R:1-5, G:1-6, B:1-5)")
        self.rbits, self.gbits, self.bbits = rbits, gbits, bbits
        r, g, b = np.meshgrid(cell_center8(np.arange(1 << rbits), rbits, 5),
                              cell_center8(np.arange(1 << gbits), gbits, 6),
                              cell_center8(np.arange(1 << bbits), bbits, 5), indexing="ij")
        rgb = np.stack([r.ravel(), g.ravel(), b.ravel()], -1)
        self.table = nearest_index(rgb, palette)

    def index_of(self, rgb565: np.ndarray) -> np.ndarray:
        """RGB565 (native order) -> LUT index (as internal::lut565Index)."""
        v = rgb565.astype(np.int32)
        r = ((v >> 11) & 31) >> (5 - self.rbits)
        g = ((v >> 5) & 63) >> (6 - self.gbits)
        b = (v & 31) >> (5 - self.bbits)
        return (r << (self.gbits + self.bbits)) | (g << self.bbits) | b

    def lookup(self, rgb565: np.ndarray) -> np.ndarray:
        return self.table[self.index_of(rgb565)]


def lut_gray8(palette: np.ndarray) -> np.ndarray:
    """GRAY8 LUT: the level whose OKLab lightness is nearest to each gray value."""
    pl = srgb_to_oklab(palette / 255.0)[:, 0]
    g = np.arange(256)
    gl = srgb_to_oklab(np.stack([g, g, g], -1) / 255.0)[:, 0]
    return np.abs(gl[:, None] - pl[None, :]).argmin(1).astype(np.uint8)


def c_array_rows(values, per_line: int, fmt: str, indent: str = "    ") -> list:
    """Formats values as lines of a C array initializer."""
    values = list(values)
    return [indent + ", ".join(fmt.format(v) for v in values[i:i + per_line]) + ","
            for i in range(0, len(values), per_line)]
