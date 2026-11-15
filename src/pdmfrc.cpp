// SPDX-License-Identifier: MIT

#include <math.h>
#include <string.h>

#include "internal.hpp"

#if defined(__ARM_FEATURE_SIMD32) && __ARM_FEATURE_SIMD32
#include <arm_acle.h>
#define PDMFRC_USE_UQSUB8 1  // 4 x 8-bit saturating subtraction (Armv7E-M, Armv8-M DSP)
#elif defined(PDMFRC_EMULATE_UQSUB8)
// For the host test of the UQSUB8 path
#define PDMFRC_USE_UQSUB8 1
static inline uint32_t __uqsub8(uint32_t a, uint32_t b) {
  uint32_t r = 0;
  for (int i = 0; i < 32; i += 8) {
    const int d = (int)((a >> i) & 0xFF) - (int)((b >> i) & 0xFF);
    r |= (uint32_t)(d > 0 ? d : 0) << i;
  }
  return r;
}
#else
#define PDMFRC_USE_UQSUB8 0
#endif

namespace pdmfrc {

using internal::PushArgs;

namespace {

constexpr uint8_t BAYER8[8][8] = {
    {0, 32, 8, 40, 2, 34, 10, 42},  {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44, 4, 36, 14, 46, 6, 38}, {60, 28, 52, 20, 62, 30, 54, 22},
    {3, 35, 11, 43, 1, 33, 9, 41},  {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7, 39, 13, 45, 5, 37}, {63, 31, 55, 23, 61, 29, 53, 21},
};

inline size_t planeIndex(const Config& cfg, int x, int y) {
  if (cfg.flags & CFG_FLAG_PACK_V8) return ((size_t)(y >> 3) * cfg.width + x) * 8 + (y & 7);
  return (size_t)y * cfg.width + x;
}

// Advances 8 consecutive pixels of the planes by one frame. Bit k of the
// result is pixel k (LSB first) or pixel 7 - k (MSB first)
template <bool LSB1ST, class Level>
PDMFRC_ALWAYS_INLINE uint32_t step8(uint8_t* a, Level level) {
  uint32_t b = 0;
#pragma GCC unroll 8
  for (int k = 0; k < 8; ++k) {
    const uint32_t l = level(k);
    const uint32_t s = (uint32_t)a[k] + l + (l >> 7);  // l: 0..255 -> 0..256
    a[k] = (uint8_t)s;
    b |= (s >> 8) << (LSB1ST ? k : 7 - k);
  }
  return b;
}

// rows lines of cols bytes. Line r starts at plane index base + r * rowStep
template <bool LSB1ST>
PDMFRC_ALWAYS_INLINE void popRows(uint8_t* acc, const uint8_t* duty, size_t base, size_t rowStep,
                                  int rows, int cols, uint8_t* dest, size_t stride, uint32_t inv) {
  for (int r = 0; r < rows; ++r, base += rowStep, dest += stride) {
    uint8_t* a = acc + base;
    const uint8_t* d = duty + base;
    for (int c = 0; c < cols; ++c, a += 8, d += 8) {
      dest[c] = (uint8_t)(step8<LSB1ST>(a, [d](int k) -> uint32_t { return d[k]; }) ^ inv);
    }
  }
}

PDMFRC_ALWAYS_INLINE uint32_t load32(const uint8_t* p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

// popRows() with the anti-ghost: the levels of the byte (r, c) are lowered by
// off[r * offRowStep + c * offColStep] (+ k for the pixel k of the byte when
// VEC), saturating at 0
template <bool LSB1ST, bool VEC>
PDMFRC_ALWAYS_INLINE void popRowsAg(uint8_t* acc, const uint8_t* duty, size_t base, size_t rowStep,
                                    int rows, int cols, uint8_t* dest, size_t stride, uint32_t inv,
                                    const uint8_t* off, size_t offRowStep, size_t offColStep) {
  for (int r = 0; r < rows; ++r, base += rowStep, dest += stride, off += offRowStep) {
    uint8_t* a = acc + base;
    const uint8_t* d = duty + base;
    const uint8_t* o = off;
    for (int c = 0; c < cols; ++c, a += 8, d += 8, o += offColStep) {
#if PDMFRC_USE_UQSUB8
      // The plane bytes of a group of 8 pixels and the VEC amounts are 4-byte aligned
      const uint32_t o0 = VEC ? load32(o) : o[0] * 0x01010101u;
      const uint32_t o1 = VEC ? load32(o + 4) : o0;
      const uint32_t l[2] = {__uqsub8(load32(d), o0), __uqsub8(load32(d + 4), o1)};
      const uint32_t b = step8<LSB1ST>(
          a, [&l](int k) -> uint32_t { return (l[k >> 2] >> ((k & 3) * 8)) & 0xFFu; });
#else
      const int32_t o0 = o[0];
      const uint32_t b = step8<LSB1ST>(a, [d, o, o0](int k) -> uint32_t {
        const int32_t t = (int32_t)d[k] - (VEC ? (int32_t)o[k] : o0);
        return (uint32_t)(t & ~(t >> 31));  // max(t, 0)
      });
#endif
      dest[c] = (uint8_t)(b ^ inv);
    }
  }
}

PDMFRC_HOT void popLsb(uint8_t* acc, const uint8_t* duty, size_t base, size_t rowStep, int rows,
                       int cols, uint8_t* dest, size_t stride, uint32_t inv) {
  popRows<true>(acc, duty, base, rowStep, rows, cols, dest, stride, inv);
}

PDMFRC_HOT void popMsb(uint8_t* acc, const uint8_t* duty, size_t base, size_t rowStep, int rows,
                       int cols, uint8_t* dest, size_t stride, uint32_t inv) {
  popRows<false>(acc, duty, base, rowStep, rows, cols, dest, stride, inv);
}

// Anti-ghost variants: S = one amount per byte, V = one amount per pixel
#define PDMFRC_DEFINE_POP_AG(name, lsb, vec)                                                      \
  PDMFRC_HOT void name(uint8_t* acc, const uint8_t* duty, size_t base, size_t rowStep, int rows,  \
                       int cols, uint8_t* dest, size_t stride, uint32_t inv, const uint8_t* off,  \
                       size_t offRowStep, size_t offColStep) {                                    \
    popRowsAg<lsb, vec>(acc, duty, base, rowStep, rows, cols, dest, stride, inv, off, offRowStep, \
                        offColStep);                                                              \
  }
PDMFRC_DEFINE_POP_AG(popLsbAgS, true, false)
PDMFRC_DEFINE_POP_AG(popLsbAgV, true, true)
PDMFRC_DEFINE_POP_AG(popMsbAgS, false, false)
PDMFRC_DEFINE_POP_AG(popMsbAgV, false, true)
#undef PDMFRC_DEFINE_POP_AG

// Sum of the levels of a line: n bytes, step bytes apart
uint32_t sumStrided(const uint8_t* p, size_t step, int n) {
  uint32_t s = 0;
  for (int i = 0; i < n; ++i, p += step) s += *p;
  return s;
}

// Sum of f(level) of a line of n bytes, step bytes apart
uint32_t sumStridedCurve(const uint8_t* p, size_t step, int n, const uint8_t* f) {
  uint32_t s = 0;
  for (int i = 0; i < n; ++i, p += step) s += f[*p];
  return s;
}

// Sum of f(level) of groups of 8 consecutive bytes, step bytes apart
uint32_t sumGroups8Curve(const uint8_t* p, size_t step, int groups, const uint8_t* f) {
  uint32_t s = 0;
  for (int i = 0; i < groups; ++i, p += step) {
    s += f[p[0]] + f[p[1]] + f[p[2]] + f[p[3]] + f[p[4]] + f[p[5]] + f[p[6]] + f[p[7]];
  }
  return s;
}

constexpr size_t AG_CURVE_SIZE = 256;

// Sum of the levels of a column of the vertically packed plane: groups of 8
// consecutive (4-byte aligned) bytes, step bytes apart
uint32_t sumGroups8(const uint8_t* p, size_t step, int groups) {
  uint32_t s = 0;
  for (int i = 0; i < groups; ++i, p += step) {
    const uint32_t w0 = load32(p), w1 = load32(p + 4);
    // 2 lanes of 16 bits, at most 4 * 255 each
    const uint32_t v = (w0 & 0x00FF00FFu) + ((w0 >> 8) & 0x00FF00FFu) + (w1 & 0x00FF00FFu) +
                       ((w1 >> 8) & 0x00FF00FFu);
    s += (v & 0xFFFFu) + (v >> 16);
  }
  return s;
}

int antiGhostLines(const Config& cfg) {
  if (cfg.flags & CFG_FLAG_ANTIGHOST_COL) return cfg.width;
  if (cfg.flags & CFG_FLAG_ANTIGHOST_ROW) return cfg.height;
  return 0;
}

}  // namespace

void getDefaultConfig(Config* cfg, pos_t w, pos_t h, PixelFormat fmt, uint8_t flags) {
  if (!cfg) return;
  cfg->width = w;
  cfg->height = h;
  cfg->inputFormat = fmt;
  cfg->flags = flags;
}

size_t bytesPerPixel(PixelFormat fmt) { return fmt == PixelFormat::GRAY8 ? 1 : 2; }

size_t calcLutSize(PixelFormat fmt) {
  return fmt == PixelFormat::GRAY8 ? 256 : internal::LUT565_SIZE;
}

size_t calcRequiredArenaSize(const Config& cfg) {
  size_t n = (size_t)cfg.width * cfg.height * 2;
  if (cfg.flags & CFG_FLAG_USE_LUT) n += calcLutSize(cfg.inputFormat);
  if (const int lines = antiGhostLines(cfg))
    n += AG_CURVE_SIZE + (((size_t)lines + 3) & ~(size_t)3);
  return n;
}

bool Pdm::init(const Config& cfg, uint8_t* arena, size_t arenaSize) {
  duty_ = acc_ = lutBuf_ = agOffset_ = agCurve_ = nullptr;
  lut_ = nullptr;
  agStrength_ = 0;
  agGammaQ8_ = 256;
  if (cfg.width == 0 || cfg.height == 0 || !arena) return false;
  constexpr uint8_t AG_BOTH = CFG_FLAG_ANTIGHOST_ROW | CFG_FLAG_ANTIGHOST_COL;
  if ((cfg.flags & AG_BOTH) == AG_BOTH) return false;
  if ((uintptr_t)arena & 3) return false;
  if (!internal::formatEnabled(cfg.inputFormat)) return false;
  if (cfg.flags & CFG_FLAG_PACK_V8) {
    if (cfg.height % 8) return false;
  } else {
    if (cfg.width % 8) return false;
  }
  if (arenaSize < calcRequiredArenaSize(cfg)) return false;

  cfg_ = cfg;
  const size_t n = (size_t)cfg.width * cfg.height;
  duty_ = arena;
  acc_ = arena + n;
  uint8_t* p = arena + 2 * n;
  if (cfg.flags & CFG_FLAG_USE_LUT) {
    lutBuf_ = p;
    p += calcLutSize(cfg.inputFormat);
  }
  if (const int lines = antiGhostLines(cfg)) {
    agCurve_ = p;
    for (size_t l = 0; l < AG_CURVE_SIZE; ++l) agCurve_[l] = (uint8_t)l;  // gamma 1
    agOffset_ = p + AG_CURVE_SIZE;
    memset(agOffset_, 0, (size_t)lines);
  }
  memset(duty_, 0, n);
  resetPhase(Phase::RANDOM, 1);
  return true;
}

bool Pdm::initLut(const uint8_t (*palette)[3]) {
  if (!lutBuf_ || !palette) return false;
  if (cfg_.inputFormat == PixelFormat::GRAY8) {
    internal::buildLutGray8(lutBuf_, palette);
  } else {
    internal::buildLut565(lutBuf_, palette);
  }
  lut_ = lutBuf_;
  return true;
}

bool Pdm::setLut(const uint8_t* lut) {
  if (!isInitialized()) return false;
  lut_ = lut;
  return true;
}

bool Pdm::setAntiGhost(uint8_t strength, uint16_t gammaQ8) {
  if (!agOffset_ || gammaQ8 == 0) return false;
  if (gammaQ8 != agGammaQ8_) {
    const float g = (float)gammaQ8 / 256.0f;
    for (size_t l = 0; l < AG_CURVE_SIZE; ++l) {
      agCurve_[l] = (uint8_t)(255.0f * powf((float)l / 255.0f, g) + 0.5f);
    }
    agGammaQ8_ = gammaQ8;
  }
  agStrength_ = strength;
  updateAntiGhost(0, 0, cfg_.width, cfg_.height);
  return true;
}

void Pdm::updateAntiGhost(int x, int y, int w, int h) {
  if (!agOffset_) return;
  const bool col = (cfg_.flags & CFG_FLAG_ANTIGHOST_COL) != 0;
  const bool v8 = (cfg_.flags & CFG_FLAG_PACK_V8) != 0;
  const int width = cfg_.width, height = cfg_.height;
  const int first = col ? x : y, end = first + (col ? w : h);
  const uint32_t strength = agStrength_;
  // sum * strength <= 65535 * 255 * 255 + den / 2 fits in 32 bits
  const uint32_t den = (uint32_t)(col ? height : width) * 255;
  // f(level) = level (gamma 1) sums the bytes directly
  const uint8_t* f = agGammaQ8_ == 256 ? nullptr : agCurve_;
  for (int i = first; i < end; ++i) {
    uint32_t sum = 0;
    if (strength && f) {
      if (col) {
        sum = v8 ? sumGroups8Curve(duty_ + (size_t)i * 8, (size_t)width * 8, height >> 3, f)
                 : sumStridedCurve(duty_ + i, (size_t)width, height, f);
      } else {
        sum = v8 ? sumStridedCurve(duty_ + (size_t)(i >> 3) * width * 8 + (i & 7), 8, width, f)
                 : sumStridedCurve(duty_ + (size_t)i * width, 1, width, f);
      }
    } else if (strength) {
      if (col) {
        sum = v8 ? sumGroups8(duty_ + (size_t)i * 8, (size_t)width * 8, height >> 3)
                 : sumStrided(duty_ + i, (size_t)width, height);
      } else {
        sum = v8 ? sumStrided(duty_ + (size_t)(i >> 3) * width * 8 + (i & 7), 8, width)
                 : sumStrided(duty_ + (size_t)i * width, 1, width);
      }
    }
    agOffset_[i] = (uint8_t)((sum * strength + den / 2) / den);
  }
}

bool Pdm::inside(pos_t x, pos_t y, pos_t w, pos_t h) const {
  return isInitialized() && (uint32_t)x + w <= cfg_.width && (uint32_t)y + h <= cfg_.height;
}

bool Pdm::push(const void* image, size_t stride, pos_t dx, pos_t dy, pos_t dw, pos_t dh,
               uint8_t flags) {
  if (!inside(dx, dy, dw, dh) || !image) return false;
  const bool useLut = (flags & PUSH_FLAG_USE_LUT) != 0;
  if (useLut && !lut_) return false;
  if (dw == 0 || dh == 0) return true;

  const bool swap = (flags & PUSH_FLAG_SWAP_XY) != 0;
  const bool xFlip = (flags & PUSH_FLAG_X_FLIP) != 0;
  const bool yFlip = (flags & PUSH_FLAG_Y_FLIP) != 0;
  const bool dbl = (flags & PUSH_FLAG_DOUBLE) != 0;
  const ptrdiff_t s = dbl ? 2 : 1;
  const ptrdiff_t bpp = (ptrdiff_t)bytesPerPixel(cfg_.inputFormat);
  if (stride == 0) stride = (size_t)((swap ? dh : dw) * s * bpp);
  const ptrdiff_t st = (ptrdiff_t)stride;

  // (x', y') = (flipped x, flipped y); (u, v) = swap ? (y', x') : (x', y')
  const int x0 = xFlip ? dw - 1 : 0, y0 = yFlip ? dh - 1 : 0;
  const ptrdiff_t ex = xFlip ? -1 : 1, ey = yFlip ? -1 : 1;
  int u0, v0;
  ptrdiff_t dudx, dvdx, dudy, dvdy;
  if (swap) {
    u0 = y0;
    v0 = x0;
    dudx = 0;
    dvdx = ex;
    dudy = ey;
    dvdy = 0;
  } else {
    u0 = x0;
    v0 = y0;
    dudx = ex;
    dvdx = 0;
    dudy = 0;
    dvdy = ey;
  }

  PushArgs a;
  a.duty = duty_;
  a.width = cfg_.width;
  a.packV8 = (cfg_.flags & CFG_FLAG_PACK_V8) != 0;
  a.dx = dx;
  a.dy = dy;
  a.dw = dw;
  a.dh = dh;
  a.src = (const uint8_t*)image + s * ((ptrdiff_t)v0 * st + (ptrdiff_t)u0 * bpp);
  a.stepX = s * (dudx * bpp + dvdx * st);
  a.stepY = s * (dudy * bpp + dvdy * st);
  a.stride = st;
  a.lut = useLut ? lut_ : nullptr;
  internal::pushRect(cfg_.inputFormat, dbl, a);
  updateAntiGhost(dx, dy, dw, dh);
  return true;
}

bool Pdm::push(const void* image, uint8_t flags) {
  return push(image, 0, 0, 0, cfg_.width, cfg_.height, flags);
}

bool Pdm::fill(uint8_t level, pos_t dx, pos_t dy, pos_t dw, pos_t dh) {
  if (!inside(dx, dy, dw, dh)) return false;
  for (int y = dy; y < dy + dh; ++y) {
    for (int x = dx; x < dx + dw; ++x) duty_[planeIndex(cfg_, x, y)] = level;
  }
  updateAntiGhost(dx, dy, dw, dh);
  return true;
}

bool Pdm::fill(uint8_t level) { return fill(level, 0, 0, cfg_.width, cfg_.height); }

void Pdm::resetPhase(Phase phase, uint32_t seed) {
  if (!isInitialized()) return;
  uint32_t r = seed ? seed : 1;
  for (int y = 0; y < cfg_.height; ++y) {
    for (int x = 0; x < cfg_.width; ++x) {
      uint32_t v = 0;
      switch (phase) {
        case Phase::ZERO:
          v = 0;
          break;
        case Phase::RANDOM:
          r ^= r << 13;  // xorshift32
          r ^= r >> 17;
          r ^= r << 5;
          v = r >> 24;
          break;
        case Phase::BAYER:
          v = BAYER8[y & 7][x & 7] * 4 + 2;
          break;
      }
      acc_[planeIndex(cfg_, x, y)] = (uint8_t)v;
    }
  }
}

bool Pdm::pop(uint8_t* dest, size_t stride, pos_t sx, pos_t sy, pos_t sw, pos_t sh, uint8_t flags) {
  if (!inside(sx, sy, sw, sh) || !dest || flags != 0) return false;
  const bool v8 = (cfg_.flags & CFG_FLAG_PACK_V8) != 0;
  size_t base, rowStep;
  int rows, cols;
  if (v8) {
    if ((sy | sh) & 7) return false;
    base = ((size_t)(sy >> 3) * cfg_.width + sx) * 8;
    rowStep = (size_t)cfg_.width * 8;
    rows = sh >> 3;
    cols = sw;
  } else {
    if ((sx | sw) & 7) return false;
    base = (size_t)sy * cfg_.width + sx;
    rowStep = cfg_.width;
    rows = sh;
    cols = sw >> 3;
  }
  if (stride == 0) stride = (size_t)cols;
  const uint32_t inv = (cfg_.flags & CFG_FLAG_INVERT) ? 0xFFu : 0u;
  const bool lsb = (cfg_.flags & CFG_FLAG_LSB1ST) != 0;
  if (agOffset_ && agStrength_) {
    // Amount of the byte (r, c), pixel k: off[r * offRowStep + c * offColStep (+ k when vec)]
    const bool col = (cfg_.flags & CFG_FLAG_ANTIGHOST_COL) != 0;
    const uint8_t* off = agOffset_ + (col ? sx : sy);
    size_t offRowStep, offColStep;
    bool vec;
    if (v8) {
      vec = !col;  // row: the 8 pixels of a byte are 8 rows
      offRowStep = col ? 0 : 8;
      offColStep = col ? 1 : 0;
    } else {
      vec = col;  // column: the 8 pixels of a byte are 8 columns
      offRowStep = col ? 0 : 1;
      offColStep = col ? 8 : 0;
    }
    if (lsb) {
      (vec ? popLsbAgV : popLsbAgS)(acc_, duty_, base, rowStep, rows, cols, dest, stride, inv, off,
                                    offRowStep, offColStep);
    } else {
      (vec ? popMsbAgV : popMsbAgS)(acc_, duty_, base, rowStep, rows, cols, dest, stride, inv, off,
                                    offRowStep, offColStep);
    }
    return true;
  }
  if (lsb) {
    popLsb(acc_, duty_, base, rowStep, rows, cols, dest, stride, inv);
  } else {
    popMsb(acc_, duty_, base, rowStep, rows, cols, dest, stride, inv);
  }
  return true;
}

bool Pdm::pop(uint8_t* dest, uint8_t flags) {
  return pop(dest, 0, 0, 0, cfg_.width, cfg_.height, flags);
}

}  // namespace pdmfrc
