// Gradient (levels 0..255 from left to right) for making the palette of a
// display, and a pattern for tuning the anti-ghost
// SPDX-License-Identifier: MIT
#include <stdio.h>

#include "app.hpp"
#include "display.hpp"
#include "gradient.hpp"
#include "pico/stdlib.h"

#ifndef SYS_CLOCK_KHZ
#define SYS_CLOCK_KHZ 125000
#endif

namespace {

constexpr int PATTERN_LEVEL_STEP = 4;

pdmfrc::Pdm pdm;
// Planes + anti-ghost curve and amounts (at most one per row or column)
constexpr size_t ARENA_SIZE =
    display::WIDTH * display::HEIGHT * 2 + 256 + display::WIDTH + display::HEIGHT;
alignas(4) uint8_t arena[ARENA_SIZE];

bool pattern = false;    // false: gradient, true: anti-ghost pattern
int pattern_level = 64;  // background level of the anti-ghost pattern

void redraw() {
  if (pattern) {
    gradient::drawAntiGhostPattern(pdm, (uint8_t)pattern_level);
  } else {
    gradient::draw(pdm);
  }
}

bool on_key(int c) {
  switch (c) {
    case 't':
      pattern = !pattern;
      break;
    case 'l':
    case 'L': {
      const int v = pattern_level + (c == 'L' ? PATTERN_LEVEL_STEP : -PATTERN_LEVEL_STEP);
      pattern_level = v < 0 ? 0 : (v > 255 ? 255 : v);
      break;
    }
    default:
      return false;
  }
  redraw();
  return true;
}

void print_status() {
  if (pattern) {
    printf("pattern: anti-ghost (background level %d)\n", pattern_level);
  } else {
    puts("pattern: gradient");
  }
}

}  // namespace

int main() {
  app::setClock(SYS_CLOCK_KHZ);
  const pdmfrc::Config cfg = gradient::makeConfig(display::module());
  if (pdmfrc::calcRequiredArenaSize(cfg) > sizeof(arena)) panic("arena");
  pdm.init(cfg, arena, sizeof(arena));
  app::init(pdm);

  redraw();
  app::resetPhase();

  app::Hooks hooks;
  hooks.title = "PDM-FRC gradient (levels 0..255, no LUT)";
  hooks.onKey = on_key;
  hooks.printStatus = print_status;
  hooks.help =
      " t    gradient / anti-ghost pattern\n"
      " l/L  background level of the anti-ghost pattern -4/+4";
  app::run(hooks);
}
