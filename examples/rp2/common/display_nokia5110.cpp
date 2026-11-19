// Display: Nokia 5110 LCD module (84x48, PCD8544, positive), PIO + DMA
// SPDX-License-Identifier: MIT
#include <stdio.h>

#include "display.hpp"
#include "pcd8544.hpp"
#include "pdmfrc/configs/nokia5110.hpp"

namespace display {
namespace {

static_assert(WIDTH == pcd8544::WIDTH && HEIGHT == pcd8544::HEIGHT, "size");

constexpr uint32_t SCLK_PRESETS_KHZ[] = {500, 1000, 2000, 3000, 4000};

pdmfrc::Pdm* pdm = nullptr;
uint16_t streams[2][pcd8544::STREAM_LEN];
uint8_t pages[pcd8544::WIDTH * pcd8544::PAGES];
int back = 0;

uint32_t step_sclk(uint32_t cur_khz, bool up) {
  const int n = (int)(sizeof(SCLK_PRESETS_KHZ) / sizeof(SCLK_PRESETS_KHZ[0]));
  if (up) {
    for (int i = 0; i < n; ++i)
      if (SCLK_PRESETS_KHZ[i] > cur_khz) return SCLK_PRESETS_KHZ[i];
    return SCLK_PRESETS_KHZ[n - 1];
  }
  for (int i = n - 1; i >= 0; --i)
    if (SCLK_PRESETS_KHZ[i] < cur_khz) return SCLK_PRESETS_KHZ[i];
  return SCLK_PRESETS_KHZ[0];
}

}  // namespace

const char* name() { return "Nokia 5110"; }

const pdmfrc::LcdModule& module() { return pdmfrc::configs::NOKIA5110; }

void init(pdmfrc::Pdm& p) {
  pdm = &p;
  pcd8544::init();
  pcd8544::initStream(streams[0]);
  pcd8544::initStream(streams[1]);
}

void prepare() {
  pdm->pop(pages);
  pcd8544::fillStream(streams[back], pages);
}

void startTransfer() {
  pcd8544::startTransfer(streams[back]);
  back ^= 1;
}

uint32_t lastTransferUs() { return pcd8544::lastTransferUs(); }

bool onKey(int c) {
  switch (c) {
    case 'c':
    case 'C':
      pcd8544::setSclkHz(step_sclk(pcd8544::sclkHz() / 1000, c == 'C') * 1000);
      return true;
    case 'k':
    case 'K':
      pcd8544::setVop(pcd8544::vop() + (c == 'K' ? 1 : -1));
      return true;
    case 'x':
    case 'X':
      pcd8544::setBias(pcd8544::bias() + (c == 'X' ? 1 : -1));
      return true;
    default:
      return false;
  }
}

void printStatus() {
  const uint32_t sclk = pcd8544::sclkHz();
  const uint32_t v = pcd8544::estimatedVlcdMv();
  printf("nokia5110: sclk=%lu.%03luMHz vop=%d (V_LCD~%lu.%02luV) bias=%d\n",
         (unsigned long)(sclk / 1000000), (unsigned long)(sclk / 1000 % 1000), pcd8544::vop(),
         (unsigned long)(v / 1000), (unsigned long)(v % 1000 / 10), pcd8544::bias());
}

const char* help() {
  return " c/C  SCLK down/up               k/K  Vop (contrast) -1/+1\n"
         " x/X  bias system -1/+1";
}

}  // namespace display
