// Title screen of the 3D demo (platform independent, drawn with ShapoGFX)
// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

namespace title {

// Portrait, at the resolution of the display (no 2x2 averaging)
constexpr int WIDTH = 64;
constexpr int HEIGHT = 128;

// Draws the title (width x HEIGHT, byte-swapped RGB565, stride in bytes):
// white text centered on black. width (up to WIDTH) is the visible width on
// a narrower display: a layout whose lines all fit is chosen. Pushed without
// the LUT, black is level 0 and white is level 255
void draw(uint16_t* dst, uint32_t stride, int width = WIDTH);

}  // namespace title
