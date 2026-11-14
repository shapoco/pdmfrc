// Still image example (architecture independent part)
// SPDX-License-Identifier: MIT
#pragma once

#include "pdmfrc/pdmfrc.hpp"

namespace image {

// Pushes the image (RGB565_SWAPPED, the size of the screen) through the LUT
// (useLut) or through the gray conversion for comparison
bool show(pdmfrc::Pdm& pdm, bool useLut);

int width();
int height();

}  // namespace image
