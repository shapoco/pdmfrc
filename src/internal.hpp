// SPDX-License-Identifier: MIT
#pragma once

#include "pdmfrc/pdmfrc.hpp"

#ifndef PDMFRC_LUT565_R_BITS
#define PDMFRC_LUT565_R_BITS 4
#endif
#ifndef PDMFRC_LUT565_G_BITS
#define PDMFRC_LUT565_G_BITS 5
#endif
#ifndef PDMFRC_LUT565_B_BITS
#define PDMFRC_LUT565_B_BITS 4
#endif

// Input formats compiled in (1) or not (0)
#ifndef PDMFRC_FORMAT_GRAY8
#define PDMFRC_FORMAT_GRAY8 1
#endif
#ifndef PDMFRC_FORMAT_RGB565
#define PDMFRC_FORMAT_RGB565 1
#endif
#ifndef PDMFRC_FORMAT_RGB565_SWAPPED
#define PDMFRC_FORMAT_RGB565_SWAPPED 1
#endif

// Attribute of the inner loops of push / pop (e.g. a section placed in RAM)
#ifndef PDMFRC_HOT_ATTR
#define PDMFRC_HOT_ATTR
#endif

#if defined(__GNUC__) || defined(__clang__)
#define PDMFRC_ALWAYS_INLINE inline __attribute__((always_inline))
#define PDMFRC_NOINLINE __attribute__((noinline))
#else
#define PDMFRC_ALWAYS_INLINE inline
#define PDMFRC_NOINLINE
#endif

// An inner loop: kept out of line so that PDMFRC_HOT_ATTR is not lost by inlining
#define PDMFRC_HOT PDMFRC_NOINLINE PDMFRC_HOT_ATTR

namespace pdmfrc {
namespace internal {

constexpr int LUT_R_BITS = PDMFRC_LUT565_R_BITS;
constexpr int LUT_G_BITS = PDMFRC_LUT565_G_BITS;
constexpr int LUT_B_BITS = PDMFRC_LUT565_B_BITS;
static_assert(1 <= LUT_R_BITS && LUT_R_BITS <= 5, "PDMFRC_LUT565_R_BITS must be 1..5");
static_assert(1 <= LUT_G_BITS && LUT_G_BITS <= 6, "PDMFRC_LUT565_G_BITS must be 1..6");
static_assert(1 <= LUT_B_BITS && LUT_B_BITS <= 5, "PDMFRC_LUT565_B_BITS must be 1..5");

constexpr size_t LUT565_SIZE = (size_t)1 << (LUT_R_BITS + LUT_G_BITS + LUT_B_BITS);

// RGB565 (native order) -> index of the RGB565 LUT
PDMFRC_ALWAYS_INLINE uint32_t lut565Index(uint32_t c) {
  return ((c >> (16 - LUT_R_BITS)) << (LUT_G_BITS + LUT_B_BITS)) |
         (((c >> (11 - LUT_G_BITS)) & ((1u << LUT_G_BITS) - 1)) << LUT_B_BITS) |
         ((c >> (5 - LUT_B_BITS)) & ((1u << LUT_B_BITS) - 1));
}

// Builds a LUT (lut.cpp)
void buildLut565(uint8_t* lut, const uint8_t (*palette)[3]);
void buildLutGray8(uint8_t* lut, const uint8_t (*palette)[3]);

// Arguments of a push to the duty plane (push.cpp)
struct PushArgs {
  uint8_t* duty;
  int width;           // of the screen
  bool packV8;         // layout of the planes
  int dx, dy, dw, dh;  // destination rectangle
  const uint8_t* src;  // input pixel of the destination (dx, dy)
  ptrdiff_t stepX;     // bytes to the input pixel of (x + 1, y)
  ptrdiff_t stepY;     // bytes to the input pixel of (x, y + 1)
  ptrdiff_t stride;    // bytes of an input line
  const uint8_t* lut;  // nullptr: gray value as the level
};

// Whether fmt is compiled in
bool formatEnabled(PixelFormat fmt);

void pushRect(PixelFormat fmt, bool dbl, const PushArgs& a);

}  // namespace internal
}  // namespace pdmfrc
