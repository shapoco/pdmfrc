// SPDX-License-Identifier: MIT
#include "image.hpp"

// Generated at build time by tools/img2cpp.py from assets/images/sg12864.png
#include "image_data.hpp"

namespace image {

bool show(pdmfrc::Pdm& pdm, bool useLut) {
  const pdmfrc::Config& cfg = pdm.getConfig();
  if (cfg.width != IMAGE_WIDTH || cfg.height != IMAGE_HEIGHT) return false;
  return pdm.push(IMAGE, useLut ? pdmfrc::PUSH_FLAG_USE_LUT : 0);
}

int width() { return IMAGE_WIDTH; }
int height() { return IMAGE_HEIGHT; }

}  // namespace image
