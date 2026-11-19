// Title screen of the 3D demo (platform independent, drawn with ShapoGFX)
// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

namespace title {

// Portrait, at the resolution of the display (no 2x2 averaging)
constexpr int WIDTH = 64;
constexpr int HEIGHT = 128;

// Draws the title (width x HEIGHT, byte-swapped RGB565, stride in bytes):
// white text centered on black. width (up to WIDTH) and height (up to HEIGHT)
// are the visible size on a smaller display: a layout whose lines all fit in
// them is chosen (the text is centered, so a centered crop shows it). Pushed
// without the LUT, black is level 0 and white is level 255
void draw(uint16_t* dst, uint32_t stride, int width = WIDTH, int height = HEIGHT);

}  // namespace title
