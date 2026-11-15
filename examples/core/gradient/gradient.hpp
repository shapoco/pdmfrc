// Gradient example (architecture independent part)
// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

#include "pdmfrc/pdmfrc.hpp"

namespace gradient {

// Config for the gradient: GRAY8 input without a LUT
pdmfrc::Config makeConfig(const pdmfrc::LcdModule& module);

// Level of column x of a screen of the given width: 0 (left) .. 255 (right)
uint8_t levelAt(int x, int width);

// Fills the duty plane with a horizontal gradient of levels 0 (left) .. 255
// (right), column by column (needs no image buffer, for large screens)
void draw(pdmfrc::Pdm& pdm);

// Pattern for tuning the anti-ghost: the screen at the given level with a
// block of level 255 at the top center (the middle quarter of the width, the
// upper half of the height). The ghost of the block darkens the area below it
// (columns) and on its sides (rows); the anti-ghost is right when that area
// looks the same as the rest
void drawAntiGhostPattern(pdmfrc::Pdm& pdm, uint8_t level);

}  // namespace gradient
