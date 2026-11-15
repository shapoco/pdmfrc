// Display: AQM1248A (128x48, ST7565R, FSTN positive), PIO + DMA
// SPDX-License-Identifier: MIT
#include <stdio.h>

#include "aqm1248a.hpp"
#include "display.hpp"
#include "pdmfrc/configs/aqm1248a.hpp"

namespace display {
namespace {

static_assert(WIDTH == aqm1248a::WIDTH && HEIGHT == aqm1248a::HEIGHT, "size");

constexpr uint32_t SCLK_PRESETS_KHZ[] = {1000, 2000, 4000, 5000, 8000, 10000, 16000, 20000};

pdmfrc::Pdm* pdm = nullptr;
uint16_t streams[2][aqm1248a::STREAM_LEN];
uint8_t pages[aqm1248a::WIDTH * aqm1248a::PAGES];
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

const char* name() { return "AQM1248A"; }

const pdmfrc::LcdModule& module() { return pdmfrc::configs::AQM1248A; }

void init(pdmfrc::Pdm& p) {
  pdm = &p;
  aqm1248a::init();
  aqm1248a::initStream(streams[0]);
  aqm1248a::initStream(streams[1]);
}

void prepare() {
  pdm->pop(pages);
  aqm1248a::fillStream(streams[back], pages);
}

void startTransfer() {
  aqm1248a::startTransfer(streams[back]);
  back ^= 1;
}

uint32_t lastTransferUs() { return aqm1248a::lastTransferUs(); }

bool onKey(int c) {
  switch (c) {
    case 'c':
    case 'C':
      aqm1248a::setSclkHz(step_sclk(aqm1248a::sclkHz() / 1000, c == 'C') * 1000);
      return true;
    case 'k':
    case 'K':
      aqm1248a::setContrast(aqm1248a::contrast() + (c == 'K' ? 1 : -1));
      return true;
    case 'x':
    case 'X':
      aqm1248a::setRegulatorRatio(aqm1248a::regulatorRatio() + (c == 'X' ? 1 : -1));
      return true;
    case 'u':
      aqm1248a::setFlipped(!aqm1248a::flipped());
      // Only the command entries change, the data entries stay
      aqm1248a::initStream(streams[0]);
      aqm1248a::initStream(streams[1]);
      return true;
    default:
      return false;
  }
}

void printStatus() {
  const uint32_t sclk = aqm1248a::sclkHz();
  const uint32_t v0 = aqm1248a::estimatedV0mV();
  printf("aqm1248a: sclk=%lu.%03luMHz contrast=%d ratio=%d (V0~%lu.%02luV) %s\n",
         (unsigned long)(sclk / 1000000), (unsigned long)(sclk / 1000 % 1000), aqm1248a::contrast(),
         aqm1248a::regulatorRatio(), (unsigned long)(v0 / 1000), (unsigned long)(v0 % 1000 / 10),
         aqm1248a::flipped() ? "flipped" : "normal");
}

const char* help() {
  return " c/C  SCLK down/up               k/K  contrast (electronic volume) -1/+1\n"
         " x/X  V0 regulator ratio -1/+1   u    rotate 180 degrees";
}

}  // namespace display
