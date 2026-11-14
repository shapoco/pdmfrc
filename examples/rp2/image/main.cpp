// Still image through the palette LUT
// SPDX-License-Identifier: MIT
#include <stdio.h>

#include "app.hpp"
#include "image.hpp"
#include "pdmfrc/configs/sg12864.hpp"
#include "pico/stdlib.h"

#ifndef SYS_CLOCK_KHZ
#define SYS_CLOCK_KHZ 125000
#endif

namespace {

const pdmfrc::LcdModule& MODULE = pdmfrc::configs::SG12864;
pdmfrc::Pdm pdm;
alignas(4) uint8_t arena[128 * 64 * 2 + 8192];  // planes + RGB565 LUT (R4G5B4)

bool use_lut = true;
uint32_t lut_build_us = 0;
uint32_t push_us = 0;

void show() {
  const uint32_t t0 = time_us_32();
  image::show(pdm, use_lut);
  push_us = time_us_32() - t0;
}

bool on_key(int c) {
  if (c != 'g') return false;
  use_lut = !use_lut;  // compares the LUT with the gray conversion
  show();
  return true;
}

void print_status() {
  printf("image: view=%s lut_build=%lums push=%luus\n", use_lut ? "lut" : "gray",
         (unsigned long)(lut_build_us / 1000), (unsigned long)push_us);
}

}  // namespace

int main() {
  app::setClock(SYS_CLOCK_KHZ);
  if (pdmfrc::calcRequiredArenaSize(MODULE.config) > sizeof(arena)) panic("arena");
  pdm.init(MODULE.config, arena, sizeof(arena));
  app::init(pdm, MODULE);

  const uint32_t t0 = time_us_32();
  pdm.initLut(MODULE.palette);
  lut_build_us = time_us_32() - t0;
  show();
  app::resetPhase();

  app::Hooks hooks;
  hooks.title = "pdmfrc image (RGB565 via palette LUT)";
  hooks.onKey = on_key;
  hooks.printStatus = print_status;
  hooks.help = " g    LUT <-> gray conversion";
  app::run(hooks);
}
