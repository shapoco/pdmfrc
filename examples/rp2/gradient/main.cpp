// Gradient (levels 0..255 from left to right) for making the palette of a display
// SPDX-License-Identifier: MIT
#include "app.hpp"
#include "gradient.hpp"
#include "pdmfrc/configs/sg12864.hpp"

#ifndef SYS_CLOCK_KHZ
#define SYS_CLOCK_KHZ 125000
#endif

namespace {

const pdmfrc::LcdModule& MODULE = pdmfrc::configs::SG12864;
pdmfrc::Pdm pdm;
alignas(4) uint8_t arena[128 * 64 * 2];
uint8_t image[128 * 64];

}  // namespace

int main() {
  app::setClock(SYS_CLOCK_KHZ);
  const pdmfrc::Config cfg = gradient::makeConfig(MODULE);
  static_assert(sizeof(arena) >= 128 * 64 * 2, "arena");
  pdm.init(cfg, arena, sizeof(arena));
  app::init(pdm, MODULE);

  gradient::generate(image, cfg.width, cfg.height);
  pdm.push(image);
  app::resetPhase();

  app::Hooks hooks;
  hooks.title = "pdmfrc gradient (levels 0..255, no LUT)";
  app::run(hooks);
}
