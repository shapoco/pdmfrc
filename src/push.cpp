// SPDX-License-Identifier: MIT
// Input image -> levels of the duty plane

#include "internal.hpp"

namespace pdmfrc {
namespace internal {
namespace {

PDMFRC_ALWAYS_INLINE uint32_t load16(const uint8_t* p) { return *(const uint16_t*)p; }

PDMFRC_ALWAYS_INLINE uint32_t swap16(uint32_t v) { return ((v >> 8) | (v << 8)) & 0xFFFFu; }

// Averages the components of 4 RGB565 pixels (native order), rounding each
PDMFRC_ALWAYS_INLINE uint32_t average565(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  const uint32_t RB = 0xF81Fu, G = 0x07E0u;
  // R (bits 11..17) and B (bits 0..6) do not overlap in the sum
  const uint32_t rb = (a & RB) + (b & RB) + (c & RB) + (d & RB) + ((2u << 11) | 2u);
  const uint32_t g = (a & G) + (b & G) + (c & G) + (d & G) + (2u << 5);
  return ((rb >> 2) & RB) | ((g >> 2) & G);
}

PDMFRC_ALWAYS_INLINE uint8_t gray565(uint32_t c) {
  const uint32_t r5 = c >> 11, g6 = (c >> 5) & 63, b5 = c & 31;
  const uint32_t r = (r5 << 3) | (r5 >> 2), g = (g6 << 2) | (g6 >> 4), b = (b5 << 3) | (b5 >> 2);
  return (uint8_t)((r * 77 + g * 150 + b * 29 + 128) >> 8);
}

// Converters: input pixel -> level
template <bool DBL, bool LUT>
struct Gray8 {
  static PDMFRC_ALWAYS_INLINE uint8_t get(const uint8_t* p, ptrdiff_t stride, const uint8_t* lut) {
    uint32_t v = p[0];
    if (DBL) v = (v + p[1] + p[stride] + p[stride + 1] + 2) >> 2;
    return LUT ? lut[v] : (uint8_t)v;
  }
};

template <bool SWAPPED, bool DBL, bool LUT>
struct Rgb565 {
  static PDMFRC_ALWAYS_INLINE uint32_t at(const uint8_t* p) {
    return SWAPPED ? swap16(load16(p)) : load16(p);
  }
  static PDMFRC_ALWAYS_INLINE uint8_t get(const uint8_t* p, ptrdiff_t stride, const uint8_t* lut) {
    uint32_t c = at(p);
    if (DBL) c = average565(c, at(p + 2), at(p + stride), at(p + stride + 2));
    return LUT ? lut[lut565Index(c)] : gray565(c);
  }
};

template <class Conv>
PDMFRC_ALWAYS_INLINE void walk(const PushArgs& a) {
  const int w = a.width;
  const ptrdiff_t sx = a.stepX, sy = a.stepY, stride = a.stride;
  const uint8_t* const lut = a.lut;
  const uint8_t* row = a.src;

  if (a.packV8) {
    // [y / 8][x][y % 8]: the 8 vertical pixels of a page are consecutive.
    // Walks the rectangle a page at a time, a column of up to 8 pixels at a time.
    const int yEnd = a.dy + a.dh;
    for (int y = a.dy; y < yEnd;) {
      int n = 8 - (y & 7);
      if (n > yEnd - y) n = yEnd - y;
      uint8_t* out = a.duty + ((size_t)(y >> 3) * w + a.dx) * 8 + (y & 7);
      const uint8_t* p = row;
      if (n == 8) {
        for (int x = 0; x < a.dw; ++x, out += 8, p += sx) {
          const uint8_t* q = p;
#pragma GCC unroll 8
          for (int k = 0; k < 8; ++k, q += sy) out[k] = Conv::get(q, stride, lut);
        }
      } else {
        for (int x = 0; x < a.dw; ++x, out += 8, p += sx) {
          const uint8_t* q = p;
          for (int k = 0; k < n; ++k, q += sy) out[k] = Conv::get(q, stride, lut);
        }
      }
      row += sy * n;
      y += n;
    }
  } else {
    // [y][x]
    for (int y = 0; y < a.dh; ++y, row += sy) {
      uint8_t* out = a.duty + (size_t)(a.dy + y) * w + a.dx;
      const uint8_t* p = row;
      for (int x = 0; x < a.dw; ++x, p += sx) out[x] = Conv::get(p, stride, lut);
    }
  }
}

// One non-template function per combination so that PDMFRC_HOT_ATTR (e.g. a
// section attribute) applies to plain functions only. A disabled format
// leaves its entries of the table empty
#if PDMFRC_FORMAT_GRAY8
PDMFRC_HOT void pushGray8(const PushArgs& a) { walk<Gray8<false, false>>(a); }
PDMFRC_HOT void pushGray8L(const PushArgs& a) { walk<Gray8<false, true>>(a); }
PDMFRC_HOT void pushGray8D(const PushArgs& a) { walk<Gray8<true, false>>(a); }
PDMFRC_HOT void pushGray8DL(const PushArgs& a) { walk<Gray8<true, true>>(a); }
#endif
#if PDMFRC_FORMAT_RGB565
PDMFRC_HOT void push565(const PushArgs& a) { walk<Rgb565<false, false, false>>(a); }
PDMFRC_HOT void push565L(const PushArgs& a) { walk<Rgb565<false, false, true>>(a); }
PDMFRC_HOT void push565D(const PushArgs& a) { walk<Rgb565<false, true, false>>(a); }
PDMFRC_HOT void push565DL(const PushArgs& a) { walk<Rgb565<false, true, true>>(a); }
#endif
#if PDMFRC_FORMAT_RGB565_SWAPPED
PDMFRC_HOT void push565S(const PushArgs& a) { walk<Rgb565<true, false, false>>(a); }
PDMFRC_HOT void push565SL(const PushArgs& a) { walk<Rgb565<true, false, true>>(a); }
PDMFRC_HOT void push565SD(const PushArgs& a) { walk<Rgb565<true, true, false>>(a); }
PDMFRC_HOT void push565SDL(const PushArgs& a) { walk<Rgb565<true, true, true>>(a); }
#endif

}  // namespace

void pushRect(PixelFormat fmt, bool dbl, const PushArgs& a) {
  typedef void (*Fn)(const PushArgs&);
  // [format][double][lut]
  static const Fn FNS[3][2][2] = {
#if PDMFRC_FORMAT_GRAY8
      {{pushGray8, pushGray8L}, {pushGray8D, pushGray8DL}},
#else
      {{nullptr, nullptr}, {nullptr, nullptr}},
#endif
#if PDMFRC_FORMAT_RGB565
      {{push565, push565L}, {push565D, push565DL}},
#else
      {{nullptr, nullptr}, {nullptr, nullptr}},
#endif
#if PDMFRC_FORMAT_RGB565_SWAPPED
      {{push565S, push565SL}, {push565SD, push565SDL}},
#else
      {{nullptr, nullptr}, {nullptr, nullptr}},
#endif
  };
  const Fn fn = FNS[(int)fmt][dbl ? 1 : 0][a.lut ? 1 : 0];
  if (fn) fn(a);
}

bool formatEnabled(PixelFormat fmt) {
  switch (fmt) {
    case PixelFormat::GRAY8:
      return PDMFRC_FORMAT_GRAY8 != 0;
    case PixelFormat::RGB565:
      return PDMFRC_FORMAT_RGB565 != 0;
    case PixelFormat::RGB565_SWAPPED:
      return PDMFRC_FORMAT_RGB565_SWAPPED != 0;
  }
  return false;
}

}  // namespace internal
}  // namespace pdmfrc
