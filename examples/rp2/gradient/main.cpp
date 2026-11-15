// Gradient (levels 0..255 from left to right) for making the palette of a display
// SPDX-License-Identifier: MIT
#include "app.hpp"
#include "display.hpp"
#include "gradient.hpp"
#include "pico/stdlib.h"

#ifndef SYS_CLOCK_KHZ
#define SYS_CLOCK_KHZ 125000
#endif

namespace {

pdmfrc::Pdm pdm;
alignas(4) uint8_t arena[display::WIDTH * display::HEIGHT * 2];  // planes

}  // namespace

int main() {
  app::setClock(SYS_CLOCK_KHZ);
  const pdmfrc::Config cfg = gradient::makeConfig(display::module());
  if (pdmfrc::calcRequiredArenaSize(cfg) > sizeof(arena)) panic("arena");
  pdm.init(cfg, arena, sizeof(arena));
  app::init(pdm);

  gradient::draw(pdm);
  app::resetPhase();

  app::Hooks hooks;
  hooks.title = "PDM-FRC gradient (levels 0..255, no LUT)";
  app::run(hooks);
}
