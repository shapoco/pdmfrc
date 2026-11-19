// Recommended settings of the SG12864ASLB (128x64, KS0108 compatible, STN positive)
// SPDX-License-Identifier: MIT
#pragma once

#include "pdmfrc/palettes/sg12864aslb.hpp"
#include "pdmfrc/pdmfrc.hpp"

namespace pdmfrc {
namespace configs {

// The same interface as the TG12864B (KS0108: 8 vertical pixels per byte with
// the LSB at the top, a dot driven by bit 1), but a positive display: a driven
// dot is dark, so the palette goes from white (level 0) to black (level 255).
// Unlike the TG12864B, the ghosts are not colored but plain gray, so the
// palette is a grayscale. 73 fps looked best. The palette was made at 73 fps
// from the photograph, normalized in the lightness (tools/palfix.py
// --normalize); the levels up to about 60 are nearly white and the levels from
// about 200 are nearly black.
static const LcdModule SG12864ASLB = {
    {128, 64, PixelFormat::RGB565_SWAPPED, CFG_FLAG_PACK_V8 | CFG_FLAG_LSB1ST | CFG_FLAG_USE_LUT},
    73 << 8,
    palettes::SG12864ASLB,
    0,
    256,
};

}  // namespace configs
}  // namespace pdmfrc
