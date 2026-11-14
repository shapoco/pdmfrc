// SPDX-License-Identifier: MIT
#include "app.hpp"

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"
#include "tg12864b.hpp"

namespace app {
namespace {

constexpr int FPS_PRESETS[] = {30,  40,  50,  60,  70,  75,  80,  90,  100, 103,
                               120, 144, 160, 180, 200, 240, 300, 360, 480, 600};
constexpr uint32_t FPS_MIN_Q8 = 1u << 8;
constexpr uint32_t FPS_MAX_Q8 = 700u << 8;
const char* const PHASE_NAMES[] = {"zero", "random", "bayer"};

pdmfrc::Pdm* pdm = nullptr;
uint32_t fps_q8 = 103u << 8;
pdmfrc::Phase phase = pdmfrc::Phase::RANDOM;
const Hooks* hooks = nullptr;

uint32_t streams[2][tg12864b::STREAM_LEN];
uint8_t pages[tg12864b::WIDTH * tg12864b::PAGES];

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

uint32_t step_bus_cycle(uint32_t ns, bool up) {
  if (up)
    ns += (ns < 2000) ? 100 : 1000;
  else
    ns -= (ns <= 2000) ? 100 : 1000;
  return ns < 300 ? 300 : (ns > 20000 ? 20000 : ns);
}

void print_help() {
  printf("--- %s ---\n", hooks->title);
  puts(
      " f/F  FPS preset down/up        [/]  FPS -1/+1\n"
      " r    phase mode + reset        R    phase reset\n"
      " b/B  bus cycle shorter/longer  w    busy wait / fixed cycle\n"
      " s    status (and reset stats)  h/?  help");
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
    case 'r':
      phase = (pdmfrc::Phase)(((int)phase + 1) % 3);
      resetPhase();
      break;
    case 'R':
      resetPhase();
      break;
    case 'b':
      tg12864b::setBusCycleNs(step_bus_cycle(tg12864b::busCycleNs(), false));
      break;
    case 'B':
      tg12864b::setBusCycleNs(step_bus_cycle(tg12864b::busCycleNs(), true));
      break;
    case 'w':
      tg12864b::setWaitMode(tg12864b::waitMode() == tg12864b::WaitMode::BUSY
                               ? tg12864b::WaitMode::FIXED
                               : tg12864b::WaitMode::BUSY);
      break;
    case 's':
      printStatus();
      resetStats();
      return false;
    case 'h':
    case '?':
      print_help();
      return false;
    default:
      if (!hooks->onKey || !hooks->onKey(c)) return false;
      break;
  }
  printStatus();
  resetStats();
  return fps_q8 != old_fps;
}

void pop_to(uint32_t* stream) {
  pdm->pop(pages);
  tg12864b::fillStream(stream, pages);
}

}  // namespace

void setClock(uint32_t khz) {
  if (khz > 133000) {
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
  }
  set_sys_clock_khz(khz, true);
}

void init(pdmfrc::Pdm& p, const pdmfrc::LcdModule& module) {
  stdio_init_all();
  tg12864b::init();
  tg12864b::initStream(streams[0]);
  tg12864b::initStream(streams[1]);
  pdm = &p;
  fps_q8 = module.fpsQ8;
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
  printf("clk=%luMHz fps=%lu.%02lu phase=%s bus=%luns %s\n",
         (unsigned long)(clock_get_hz(clk_sys) / 1000000), (unsigned long)(fps_q8 >> 8),
         (unsigned long)((fps_q8 & 255) * 100 / 256), PHASE_NAMES[(int)phase],
         (unsigned long)tg12864b::busCycleNs(),
         tg12864b::waitMode() == tg12864b::WaitMode::BUSY ? "busy-wait" : "fixed-cycle");
  printf(
      "frame: pop avg=%luus max=%luus xfer=%luus late=%lu/%lu | start delay max=%luus "
      "interval min=%luus max=%luus (nominal %luus)\n",
      (unsigned long)avg, (unsigned long)stat_pop_max, (unsigned long)tg12864b::lastTransferUs(),
      (unsigned long)stat_late, (unsigned long)stat_frames, (unsigned long)stat_delay_max,
      (unsigned long)(stat_interval_min == UINT32_MAX ? 0 : stat_interval_min),
      (unsigned long)stat_interval_max, (unsigned long)frame_time_us(1));
  if (hooks && hooks->printStatus) hooks->printStatus();
}

void run(const Hooks& h) {
  hooks = &h;
  int buf = 0;
  pop_to(streams[buf]);
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

    tg12864b::startTransfer(streams[buf]);
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
    buf ^= 1;
    ++n;

    const uint32_t t0 = time_us_32();
    pop_to(streams[buf]);
    const uint32_t dt = time_us_32() - t0;
    stat_pop_sum += dt;
    if (dt > stat_pop_max) stat_pop_max = dt;
    ++stat_frames;

    if (h.afterPop) h.afterPop();
  }
}

}  // namespace app
