// Recommended settings of the AQM1248A (128x48, ST7565R, FSTN positive)
// SPDX-License-Identifier: MIT
#pragma once

#include "pdmfrc/palettes/aqm1248a.hpp"
#include "pdmfrc/pdmfrc.hpp"

namespace pdmfrc {
namespace configs {

// The ST7565R takes 8 vertical pixels per byte with the LSB at the top, and
// darkens a dot for bit 1. The LCD scans by its own oscillator, which cannot
// be synchronized: 73 fps (about half of its scan rate) looked best. The
// palette was made at 73 fps with V0 regulator ratio 4 and electronic volume
// 22 (V0 about 7.8 V), with the anti-ghost below; the levels 0..76 are white,
// 77..237 make the gradation (nearly black from about 190) and 238 and above
// are black. Many dark pixels in a column (SEG) darken the whole column: the
// anti-ghost works on the columns.
static const LcdModule AQM1248A = {
    {128, 48, PixelFormat::RGB565_SWAPPED,
     CFG_FLAG_PACK_V8 | CFG_FLAG_LSB1ST | CFG_FLAG_USE_LUT | CFG_FLAG_ANTIGHOST_COL},
    73 << 8,
    palettes::AQM1248A,
    107,  // anti-ghost strength
    115,  // anti-ghost gamma 0.45
};

}  // namespace configs
}  // namespace pdmfrc
