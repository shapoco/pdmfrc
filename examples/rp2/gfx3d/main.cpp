// 3D demo: a metal cube floating over wavy water under a sunset sky.
// core 1 renders the scene, core 0 pushes it to PDM-FRC and drives the LCD.
// At startup a title is shown for TITLE_US while core 1 builds the LUT and
// initializes the scene.
// SPDX-License-Identifier: MIT
#include <stdio.h>

#include "app.hpp"
#include "display.hpp"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "scene.hpp"
#include "title.hpp"

#ifndef SYS_CLOCK_KHZ
#define SYS_CLOCK_KHZ 250000
#endif

namespace {

using namespace pdmfrc;

const LcdModule& MODULE = display::module();
Pdm pdm;
// Planes + RGB565 LUT (R4G5B4)
alignas(4) uint8_t arena[display::WIDTH * display::HEIGHT * 2 + 8192];

// Orientation of the display as installed and the push flags that show the
// image upright
enum class Rotation : uint8_t { R0, CCW90, R180, CW90 };
const char* const ROT_NAMES[] = {"0", "ccw90", "180", "cw90"};
constexpr uint8_t ROT_FLAGS[] = {
    0,
    PUSH_FLAG_SWAP_XY | PUSH_FLAG_X_FLIP,
    PUSH_FLAG_X_FLIP | PUSH_FLAG_Y_FLIP,
    PUSH_FLAG_SWAP_XY | PUSH_FLAG_Y_FLIP,
};

// Messages between the cores (FIFO)
constexpr uint32_t MSG_FRAME_DONE = 0x46524d44;  // core 1 -> 0: the back buffer is complete
constexpr uint32_t MSG_ACK = 0x41434b21;         // core 0 -> 1: pushed, draw the next one

constexpr uint64_t TITLE_US = 3000000;  // minimum time of the title screen

Rotation rot = Rotation::CCW90;  // the display stands portrait, turned counterclockwise
int scale = 2;                   // 2: average 2x2 (PUSH_FLAG_DOUBLE), 1: crop the center
bool layout_dirty = true;        // rotation / scale changed: clear the margins

// Shared with core 1
uint16_t backbuf[scene::BB_W * scene::BB_H];
volatile bool core1_ready = false;   // core 1 -> 0: LUT built, scene initialized
volatile bool demo_started = false;  // core 0 -> 1: title over, start rendering
volatile bool sync_frames = true;    // core 1 waits for each push (no tearing)
volatile bool paused = false;
volatile uint32_t core1_frames = 0;
volatile uint32_t core1_init_us = 0;
scene::Timing core1_timing = {};  // written by core 1 (for display only)
alignas(8) uint32_t core1_stack[8 * 1024 / 4];

volatile uint32_t lut_build_us = 0;
uint32_t stat_pushes = 0;
uint64_t stat_push_sum = 0;
uint32_t stat_push_max = 0;
uint32_t stat_core1_frames0 = 0;
uint64_t stat_t0 = 0;

uint32_t clock_us() { return time_us_32(); }

// Builds the LUT and initializes the scene while core 0 shows the title, then
// waits for the end of the title. The back buffer holds the title until then
void core1_main() {
  uint32_t t0 = time_us_32();
  pdm.initLut(MODULE.palette);
  lut_build_us = time_us_32() - t0;

  t0 = time_us_32();
  scene::init(clock_us);
  core1_init_us = time_us_32() - t0;

  __dmb();  // the LUT is visible to core 0 before the flag
  core1_ready = true;
  while (!demo_started) tight_loop_contents();

  uint64_t t_base = time_us_64();
  float t_anim = 0.0f;
  for (;;) {
    const uint64_t now = time_us_64();
    if (!paused) t_anim += (float)(now - t_base) * 1e-6f;
    t_base = now;

    scene::Timing tm;
    scene::render(backbuf, t_anim, tm);
    core1_timing = tm;
    core1_frames = core1_frames + 1;

    if (sync_frames) {
      multicore_fifo_push_blocking(MSG_FRAME_DONE);
      while (multicore_fifo_pop_blocking() != MSG_ACK) {
      }
    }
  }
}

// An image (w x h at the top left of the back buffer) -> PDM-FRC, in the
// current rotation and averaged scale x scale. The image is centered on the
// screen: what does not fit is cropped
void push_centered(int w, int h, int scale, uint8_t flags) {
  const Config& cfg = pdm.getConfig();
  flags |= ROT_FLAGS[(int)rot] | (scale == 2 ? PUSH_FLAG_DOUBLE : 0);
  const bool swap = (flags & PUSH_FLAG_SWAP_XY) != 0;
  const int lw = w / scale, lh = h / scale;            // logical size of the image
  const int rw = swap ? lh : lw, rh = swap ? lw : lh;  // as seen on the screen
  const int dw = rw < cfg.width ? rw : cfg.width, dh = rh < cfg.height ? rh : cfg.height;
  const int dx = (cfg.width - dw) / 2, dy = (cfg.height - dh) / 2;
  // The crop is centered, so its position in the image does not depend on the flips
  const int cw = swap ? dh : dw, ch = swap ? dw : dh;
  const int u = (lw - cw) / 2, v = (lh - ch) / 2;
  pdm.push(backbuf + (v * scale) * scene::BB_W + u * scale, scene::BB_W * 2, dx, dy, dw, dh, flags);
}

// Back buffer (BB_W x BB_H) -> PDM-FRC, the margins are level 0
void push_frame() {
  const uint32_t t0 = time_us_32();
  if (layout_dirty) {
    pdm.fill(0);
    layout_dirty = false;
  }
  push_centered(scene::BB_W, scene::BB_H, scale, PUSH_FLAG_USE_LUT);

  const uint32_t dt = time_us_32() - t0;
  ++stat_pushes;
  stat_push_sum += dt;
  if (dt > stat_push_max) stat_push_max = dt;
}

// Pushes on each completion of core 1 and answers. Always answers, even
// without sync, since core 1 may be waiting at the moment of the switch
void service_core1() {
  while (multicore_fifo_rvalid()) {
    if (multicore_fifo_pop_blocking() != MSG_FRAME_DONE) continue;
    push_frame();
    multicore_fifo_push_blocking(MSG_ACK);
  }
}

void reset_local_stats();

// Starts the demo once the title has been shown for TITLE_US and core 1 is ready
uint64_t title_t0 = 0;
void poll_title() {
  if (!core1_ready || time_us_64() - title_t0 < TITLE_US) return;
  __dmb();
  layout_dirty = true;  // clear the title
  reset_local_stats();
  demo_started = true;
}

// Right after the pop, at the head of the wait, so that the push never delays
// the next frame start. Without sync, pushes every frame whatever core 1 is doing
void after_pop() {
  if (!demo_started) {
    poll_title();
    return;
  }
  if (!sync_frames) push_frame();
  service_core1();
}

void reset_local_stats() {
  stat_pushes = 0;
  stat_push_sum = 0;
  stat_push_max = 0;
  stat_core1_frames0 = core1_frames;
  stat_t0 = time_us_64();
}

void print_status() {
  const uint64_t el = time_us_64() - stat_t0;
  const uint32_t c1 = core1_frames - stat_core1_frames0;
  const uint32_t c1_fps10 = el ? (uint32_t)((uint64_t)c1 * 10000000u / el) : 0;
  const uint32_t p_avg = stat_pushes ? (uint32_t)(stat_push_sum / stat_pushes) : 0;
  const scene::Timing tm = core1_timing;
  if (!demo_started) puts("gfx3d: title screen");
  printf(
      "gfx3d: rot=%s scale=1/%d sync=%s%s lut_build=%lums core1_init=%lums push avg=%luus "
      "max=%luus n=%lu\n",
      ROT_NAMES[(int)rot], scale, sync_frames ? "on" : "off", paused ? " (paused)" : "",
      (unsigned long)(lut_build_us / 1000), (unsigned long)(core1_init_us / 1000),
      (unsigned long)p_avg, (unsigned long)stat_push_max, (unsigned long)stat_pushes);
  printf("core1: %lu.%lu fps | bg=%luus reflection=%luus cube=%luus total=%luus\n",
         (unsigned long)(c1_fps10 / 10), (unsigned long)(c1_fps10 % 10),
         (unsigned long)tm.background, (unsigned long)tm.reflection, (unsigned long)tm.cube,
         (unsigned long)tm.total);
  scene::print_stats();
  reset_local_stats();
}

bool on_key(int c) {
  switch (c) {
    case 'y':
      sync_frames = !sync_frames;
      break;
    case 'o':
      rot = (Rotation)(((int)rot + 1) & 3);
      layout_dirty = true;
      break;
    case 'z':
      scale = (scale == 2) ? 1 : 2;
      layout_dirty = true;
      break;
    case 'p':
      paused = !paused;
      break;
    default:
      return false;
  }
  return true;
}

}  // namespace

int main() {
  // The PIO clock divider follows the system clock at display init: set it first
  app::setClock(SYS_CLOCK_KHZ);
  if (calcRequiredArenaSize(MODULE.config) > sizeof(arena)) panic("arena");
  pdm.init(MODULE.config, arena, sizeof(arena));
  app::init(pdm);
  app::resetPhase();

  // The title goes without the LUT (gray = level), which core 1 builds meanwhile
  title::draw(backbuf, scene::BB_W * 2);
  push_centered(title::WIDTH, title::HEIGHT, 1, 0);
  multicore_launch_core1_with_stack(core1_main, core1_stack, sizeof(core1_stack));
  title_t0 = time_us_64();
  reset_local_stats();

  app::Hooks hooks;
  hooks.title = "PDM-FRC gfx3d (core1: render, core0: push + pop)";
  hooks.onKey = on_key;
  hooks.afterPop = after_pop;
  hooks.printStatus = print_status;
  hooks.help =
      " y    frame sync on/off         o    rotation (0/ccw90/180/cw90)\n"
      " z    scale 1/2 <-> 1/1         p    pause animation";
  app::run(hooks);
}
