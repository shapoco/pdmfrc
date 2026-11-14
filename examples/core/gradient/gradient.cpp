// SPDX-License-Identifier: MIT
#include "gradient.hpp"

namespace gradient {

pdmfrc::Config makeConfig(const pdmfrc::LcdModule& module) {
  pdmfrc::Config cfg = module.config;
  cfg.inputFormat = pdmfrc::PixelFormat::GRAY8;
  cfg.flags &= (uint8_t)~pdmfrc::CFG_FLAG_USE_LUT;
  return cfg;
}

void generate(uint8_t* buf, int width, int height) {
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      buf[y * width + x] = (uint8_t)(width > 1 ? (x * 255 + (width - 1) / 2) / (width - 1) : 0);
    }
  }
}

}  // namespace gradient
