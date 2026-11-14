// Gradient example (architecture independent part)
// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

#include "pdmfrc/pdmfrc.hpp"

namespace gradient {

// Config for the gradient: GRAY8 input without a LUT, the levels are the
// pixel values as they are
pdmfrc::Config makeConfig(const pdmfrc::LcdModule& module);

// Draws a horizontal gradient of levels 0 (left) .. 255 (right) to a GRAY8 buffer
void generate(uint8_t* buf, int width, int height);

}  // namespace gradient
