// SPDX-License-Identifier: MIT
#include "gradient.hpp"

namespace gradient {

pdmfrc::Config makeConfig(const pdmfrc::LcdModule& module) {
  pdmfrc::Config cfg = module.config;
  cfg.inputFormat = pdmfrc::PixelFormat::GRAY8;
  cfg.flags &= (uint8_t)~pdmfrc::CFG_FLAG_USE_LUT;
  return cfg;
}

uint8_t levelAt(int x, int width) {
  return (uint8_t)(width > 1 ? (x * 255 + (width - 1) / 2) / (width - 1) : 0);
}

void draw(pdmfrc::Pdm& pdm) {
  const pdmfrc::Config& cfg = pdm.getConfig();
  for (int x = 0; x < cfg.width; ++x) {
    pdm.fill(levelAt(x, cfg.width), (pdmfrc::pos_t)x, 0, 1, cfg.height);
  }
}

}  // namespace gradient
