// SPDX-License-Identifier: MIT
#include "app.hpp"

#include <stdio.h>

#include "display.hpp"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"

namespace app {
namespace {

constexpr int FPS_PRESETS[] = {5,   10,  15,  20,  25,  30,  40,  50,  60,  70,  73,  75, 80,
                               90,  100, 103, 120, 144, 160, 180, 200, 240, 300, 360, 480, 600};
constexpr uint32_t FPS_MIN_Q8 = 1u << 8;
constexpr uint32_t FPS_MAX_Q8 = 700u << 8;
const char* const PHASE_NAMES[] = {"zero", "random", "bayer"};
constexpr int GAMMA100_MIN = 20;
constexpr int GAMMA100_MAX = 300;

pdmfrc::Pdm* pdm = nullptr;
uint32_t fps_q8 = 103u << 8;
pdmfrc::Phase phase = pdmfrc::Phase::RANDOM;
const Hooks* hooks = nullptr;

uint32_t stat_frames = 0;
uint64_t stat_pop_sum = 0;
uint32_t stat_pop_max = 0;
uint32_t stat_late = 0;
// Jitter of the frame start: delay from the target time, interval between starts [us]
uint32_t stat_delay_max = 0;
uint32_t stat_interval_min = UINT32_MAX;
uint32_t stat_interval_max = 0;
uint64_t last_start_us = 0;

uint64_t frame_time_us(uint32_t n) { return ((uint64_t)n * 256000000u) / fps_q8; }

uint32_t step_preset(uint32_t cur_q8, bool up) {
  const int n = (int)(sizeof(FPS_PRESETS) / sizeof(FPS_PRESETS[0]));
  if (up) {
    for (int v : FPS_PRESETS)
      if (((uint32_t)v << 8) > cur_q8) return (uint32_t)v << 8;
    return (uint32_t)FPS_PRESETS[n - 1] << 8;
  }
  for (int i = n - 1; i >= 0; --i)
    if (((uint32_t)FPS_PRESETS[i] << 8) < cur_q8) return (uint32_t)FPS_PRESETS[i] << 8;
  return (uint32_t)FPS_PRESETS[0] << 8;
}

uint32_t clamp_fps(int64_t v) {
  return v < FPS_MIN_Q8 ? FPS_MIN_Q8 : (v > FPS_MAX_Q8 ? FPS_MAX_Q8 : (uint32_t)v);
}

// Steps by 0.1 fps and rounds to a multiple of 0.1 fps
uint32_t step_fps_tenth(uint32_t cur_q8, bool up) {
  const int64_t tenths = ((int64_t)cur_q8 * 10 + 128) / 256 + (up ? 1 : -1);
  return clamp_fps((tenths * 256 + 5) / 10);
}

// Anti-ghost gamma: Q8 <-> hundredths, stepped by 0.05 in GAMMA100_MIN..GAMMA100_MAX
int gamma100(uint16_t q8) { return (q8 * 100 + 128) / 256; }

int step_gamma100(int cur, bool up) {
  const int v = (cur + (up ? 5 : -5)) / 5 * 5;  // also rounds to a multiple of 0.05
  return v < GAMMA100_MIN ? GAMMA100_MIN : (v > GAMMA100_MAX ? GAMMA100_MAX : v);
}

bool anti_ghost_enabled() {
  return (pdm->getConfig().flags &
          (pdmfrc::CFG_FLAG_ANTIGHOST_ROW | pdmfrc::CFG_FLAG_ANTIGHOST_COL)) != 0;
}

void print_help() {
  printf("--- %s ---\n", hooks->title);
  puts(
      " f/F  FPS preset down/up        [/]  FPS -1/+1\n"
      " {/}  FPS -0.1/+0.1\n"
      " r    phase mode + reset        R    phase reset\n"
      " s    status (and reset stats)  h/?  help");
  if (anti_ghost_enabled()) {
    puts(" a/A  anti-ghost strength -1/+1      e/E  anti-ghost gamma -0.05/+0.05");
  }
  if (display::help()) puts(display::help());
  if (hooks->help) puts(hooks->help);
}

// Returns whether the frame rate changed
bool handle_key(int c) {
  const uint32_t old_fps = fps_q8;
  switch (c) {
    case 'f':
      fps_q8 = step_preset(fps_q8, false);
      break;
    case 'F':
      fps_q8 = step_preset(fps_q8, true);
      break;
    case '[':
      fps_q8 = clamp_fps((int64_t)fps_q8 - 256);
      break;
    case ']':
      fps_q8 = clamp_fps((int64_t)fps_q8 + 256);
      break;
    case '{':
      fps_q8 = step_fps_tenth(fps_q8, false);
      break;
    case '}':
      fps_q8 = step_fps_tenth(fps_q8, true);
      break;
    case 'r':
      phase = (pdmfrc::Phase)(((int)phase + 1) % 3);
      resetPhase();
      break;
    case 'R':
      resetPhase();
      break;
    case 'a':
    case 'A': {
      if (!anti_ghost_enabled()) return false;
      const int v = pdm->getAntiGhost() + (c == 'A' ? 1 : -1);
      pdm->setAntiGhost((uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)), pdm->getAntiGhostGamma());
      break;
    }
    case 'e':
    case 'E': {
      if (!anti_ghost_enabled()) return false;
      const int g = step_gamma100(gamma100(pdm->getAntiGhostGamma()), c == 'E');
      pdm->setAntiGhost(pdm->getAntiGhost(), (uint16_t)((g * 256 + 50) / 100));
      break;
    }
    case 's':
      printStatus();
      resetStats();
      return false;
    case 'h':
    case '?':
      print_help();
      return false;
    default:
      if (!display::onKey(c) && (!hooks->onKey || !hooks->onKey(c))) return false;
      break;
  }
  printStatus();
  resetStats();
  return fps_q8 != old_fps;
}

}  // namespace

void setClock(uint32_t khz) {
  if (khz > 133000) {
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
  }
  set_sys_clock_khz(khz, true);
}

void init(pdmfrc::Pdm& p) {
  stdio_init_all();
  pdm = &p;
  display::init(p);
  fps_q8 = display::module().fpsQ8;
  // false without CFG_FLAG_ANTIGHOST_*
  pdm->setAntiGhost(display::module().antiGhost, display::module().antiGhostGammaQ8);
}

void resetPhase() { pdm->resetPhase(phase, time_us_32() | 1); }

void resetStats() {
  stat_frames = 0;
  stat_pop_sum = 0;
  stat_pop_max = 0;
  stat_late = 0;
  stat_delay_max = 0;
  stat_interval_min = UINT32_MAX;
  stat_interval_max = 0;
  last_start_us = 0;  // the first interval after a reset is not counted
}

void printStatus() {
  const uint32_t avg = stat_frames ? (uint32_t)(stat_pop_sum / stat_frames) : 0;
  const uint32_t fps100 = (uint32_t)(((uint64_t)fps_q8 * 100 + 128) / 256);
  printf("%s: clk=%luMHz fps=%lu.%02lu phase=%s\n", display::name(),
         (unsigned long)(clock_get_hz(clk_sys) / 1000000), (unsigned long)(fps100 / 100),
         (unsigned long)(fps100 % 100), PHASE_NAMES[(int)phase]);
  if (anti_ghost_enabled()) {
    const int g = gamma100(pdm->getAntiGhostGamma());
    printf("anti-ghost: %s strength=%u gamma=%d.%02d\n",
           (pdm->getConfig().flags & pdmfrc::CFG_FLAG_ANTIGHOST_COL) ? "column" : "row",
           (unsigned)pdm->getAntiGhost(), g / 100, g % 100);
  }
  display::printStatus();
  printf(
      "frame: pop avg=%luus max=%luus xfer=%luus late=%lu/%lu | start delay max=%luus "
      "interval min=%luus max=%luus (nominal %luus)\n",
      (unsigned long)avg, (unsigned long)stat_pop_max, (unsigned long)display::lastTransferUs(),
      (unsigned long)stat_late, (unsigned long)stat_frames, (unsigned long)stat_delay_max,
      (unsigned long)(stat_interval_min == UINT32_MAX ? 0 : stat_interval_min),
      (unsigned long)stat_interval_max, (unsigned long)frame_time_us(1));
  if (hooks && hooks->printStatus) hooks->printStatus();
}

void run(const Hooks& h) {
  hooks = &h;
  display::prepare();
  resetStats();

  uint64_t base_us = time_us_64();
  uint32_t n = 0;
  for (;;) {
    const uint64_t target = base_us + frame_time_us(n);
    uint64_t now;
    while ((now = time_us_64()) < target) {
      const int c = getchar_timeout_us(0);
      if (c != PICO_ERROR_TIMEOUT && handle_key(c)) {
        base_us = time_us_64();
        n = 0;
        break;
      }
    }
    if (now > target + frame_time_us(1)) {
      ++stat_late;
      base_us = now;
      n = 0;
    }

    display::startTransfer();
    {
      const uint64_t started = time_us_64();
      // Ahead of the target when a key broke the wait: not counted
      const uint32_t delay = started > target ? (uint32_t)(started - target) : 0;
      if (delay > stat_delay_max) stat_delay_max = delay;
      if (last_start_us) {
        const uint32_t iv = (uint32_t)(started - last_start_us);
        if (iv < stat_interval_min) stat_interval_min = iv;
        if (iv > stat_interval_max) stat_interval_max = iv;
      }
      last_start_us = started;
    }
    ++n;

    const uint32_t t0 = time_us_32();
    display::prepare();
    const uint32_t dt = time_us_32() - t0;
    stat_pop_sum += dt;
    if (dt > stat_pop_max) stat_pop_max = dt;
    ++stat_frames;

    if (h.afterPop) h.afterPop();
  }
}

}  // namespace app
