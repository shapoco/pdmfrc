// Recommended settings of the Nokia 5110 LCD module (84x48, PCD8544, positive)
// SPDX-License-Identifier: MIT
#pragma once

#include "pdmfrc/palettes/nokia5110.hpp"
#include "pdmfrc/pdmfrc.hpp"

namespace pdmfrc {
namespace configs {

// The PCD8544 takes 8 vertical pixels per byte with the LSB at the top, and
// darkens a dot for bit 1. The LCD scans by its own oscillator, which cannot
// be synchronized: 79 fps looked best. The palette is a grayscale made at
// 79 fps from the photograph (tools/palfix.py --smooth 3 --normalize: a column
// of the 84 px wide display spans 3 levels), with Vop 64 and bias 1/48.
static const LcdModule NOKIA5110 = {
    {84, 48, PixelFormat::RGB565_SWAPPED, CFG_FLAG_PACK_V8 | CFG_FLAG_LSB1ST | CFG_FLAG_USE_LUT},
    79 << 8,
    palettes::NOKIA5110,
    0,
    256,
};

}  // namespace configs
}  // namespace pdmfrc
