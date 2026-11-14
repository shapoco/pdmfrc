// SPDX-License-Identifier: MIT
// Palette (level -> apparent color) -> LUT (input pixel -> level)

#include <math.h>

#include "internal.hpp"

namespace pdmfrc {
namespace internal {
namespace {

// Fixed-point scale of OKLab. L: 0..1, a/b: within +-0.4, so that each fits
// in int16 and the squared distance in int32
constexpr float LAB_SCALE = 8192.0f;

struct Lab {
  int16_t l, a, b;
};

float srgbToLinear(int v) {
  const float c = (float)v / 255.0f;
  return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

inline int16_t fix(float v) { return (int16_t)lroundf(LAB_SCALE * v); }

// linear sRGB -> OKLab (fixed point)
Lab toLab(float r, float g, float b) {
  const float l = cbrtf(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
  const float m = cbrtf(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
  const float s = cbrtf(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
  Lab o;
  o.l = fix(0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s);
  o.a = fix(1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s);
  o.b = fix(0.0259066487f * l + 0.7827717662f * m - 0.8086783821f * s);
  return o;
}

// 8-bit value of the center of a LUT cell: the midpoint of the component
// values [lo, lo + w - 1] scaled to 8 bits, rounded half to even
int cellCenter8(int index, int bits, int fullBits) {
  const int w = 1 << (fullBits - bits);
  const int num = (2 * index * w + w - 1) * 255;
  const int den = 2 * ((1 << fullBits) - 1);
  int q = num / den;
  const int r2 = 2 * (num % den);
  if (r2 > den || (r2 == den && (q & 1))) ++q;
  return q;
}

// The palette in OKLab: 1.5 KB on the stack of the caller
struct PaletteLab {
  Lab c[256];
  explicit PaletteLab(const uint8_t (*palette)[3]) {
    for (int i = 0; i < 256; ++i) {
      c[i] = toLab(srgbToLinear(palette[i][0]), srgbToLinear(palette[i][1]),
                   srgbToLinear(palette[i][2]));
    }
  }
};

// The nearest level; the smallest one on a tie
uint8_t nearest(const PaletteLab& p, const Lab& x) {
  int32_t bestD = INT32_MAX;
  int best = 0;
  for (int i = 0; i < 256; ++i) {
    const int32_t dl = (int32_t)x.l - p.c[i].l;
    const int32_t da = (int32_t)x.a - p.c[i].a;
    const int32_t db = (int32_t)x.b - p.c[i].b;
    const int32_t d = dl * dl + da * da + db * db;
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  return (uint8_t)best;
}

}  // namespace

void buildLut565(uint8_t* lut, const uint8_t (*palette)[3]) {
  const PaletteLab pal(palette);

  // linear values of the cell centers
  float linR[1 << LUT_R_BITS], linG[1 << LUT_G_BITS], linB[1 << LUT_B_BITS];
  for (int i = 0; i < (1 << LUT_R_BITS); ++i) linR[i] = srgbToLinear(cellCenter8(i, LUT_R_BITS, 5));
  for (int i = 0; i < (1 << LUT_G_BITS); ++i) linG[i] = srgbToLinear(cellCenter8(i, LUT_G_BITS, 6));
  for (int i = 0; i < (1 << LUT_B_BITS); ++i) linB[i] = srgbToLinear(cellCenter8(i, LUT_B_BITS, 5));

  uint8_t* out = lut;
  for (int r = 0; r < (1 << LUT_R_BITS); ++r) {
    for (int g = 0; g < (1 << LUT_G_BITS); ++g) {
      for (int b = 0; b < (1 << LUT_B_BITS); ++b) {
        *out++ = nearest(pal, toLab(linR[r], linG[g], linB[b]));
      }
    }
  }
}

void buildLutGray8(uint8_t* lut, const uint8_t (*palette)[3]) {
  const PaletteLab pal(palette);

  // Lightness only, so that the levels follow the brightness of the input
  for (int g = 0; g < 256; ++g) {
    const float v = srgbToLinear(g);
    const int32_t l = toLab(v, v, v).l;
    int32_t bestD = INT32_MAX;
    int best = 0;
    for (int i = 0; i < 256; ++i) {
      const int32_t d = l > pal.c[i].l ? l - pal.c[i].l : pal.c[i].l - l;
      if (d < bestD) {
        bestD = d;
        best = i;
      }
    }
    lut[g] = (uint8_t)best;
  }
}

}  // namespace internal
}  // namespace pdmfrc
