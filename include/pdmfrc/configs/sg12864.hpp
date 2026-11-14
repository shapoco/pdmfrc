// Recommended settings of the SG12864 (128x64, KS0108 compatible, STN blue negative)
// SPDX-License-Identifier: MIT
#pragma once

#include "pdmfrc/palettes/sg12864.hpp"
#include "pdmfrc/pdmfrc.hpp"

namespace pdmfrc {
namespace configs {

// The KS0108 takes 8 vertical pixels per byte with the LSB at the top, and
// lights a dot for bit 1. The palette was measured at 103 fps.
static const LcdModule SG12864 = {
    {128, 64, PixelFormat::RGB565_SWAPPED, CFG_FLAG_PACK_V8 | CFG_FLAG_LSB1ST | CFG_FLAG_USE_LUT},
    103 << 8,
    palettes::SG12864,
};

}  // namespace configs
}  // namespace pdmfrc
