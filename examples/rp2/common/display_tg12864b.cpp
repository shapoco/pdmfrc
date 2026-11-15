// Display: TG12864B (128x64, KS0108 compatible), PIO + DMA
// SPDX-License-Identifier: MIT
#include <stdio.h>

#include "display.hpp"
#include "pdmfrc/configs/tg12864b.hpp"
#include "tg12864b.hpp"

namespace display {
namespace {

static_assert(WIDTH == tg12864b::WIDTH && HEIGHT == tg12864b::HEIGHT, "size");

pdmfrc::Pdm* pdm = nullptr;
uint32_t streams[2][tg12864b::STREAM_LEN];
uint8_t pages[tg12864b::WIDTH * tg12864b::PAGES];
int back = 0;

uint32_t step_bus_cycle(uint32_t ns, bool up) {
  if (up)
    ns += (ns < 2000) ? 100 : 1000;
  else
    ns -= (ns <= 2000) ? 100 : 1000;
  return ns < 300 ? 300 : (ns > 20000 ? 20000 : ns);
}

}  // namespace

const char* name() { return "TG12864B"; }

const pdmfrc::LcdModule& module() { return pdmfrc::configs::TG12864B; }

void init(pdmfrc::Pdm& p) {
  pdm = &p;
  tg12864b::init();
  tg12864b::initStream(streams[0]);
  tg12864b::initStream(streams[1]);
}

void prepare() {
  pdm->pop(pages);
  tg12864b::fillStream(streams[back], pages);
}

void startTransfer() {
  tg12864b::startTransfer(streams[back]);
  back ^= 1;
}

uint32_t lastTransferUs() { return tg12864b::lastTransferUs(); }

bool onKey(int c) {
  switch (c) {
    case 'b':
      tg12864b::setBusCycleNs(step_bus_cycle(tg12864b::busCycleNs(), false));
      return true;
    case 'B':
      tg12864b::setBusCycleNs(step_bus_cycle(tg12864b::busCycleNs(), true));
      return true;
    case 'w':
      tg12864b::setWaitMode(tg12864b::waitMode() == tg12864b::WaitMode::BUSY
                                ? tg12864b::WaitMode::FIXED
                                : tg12864b::WaitMode::BUSY);
      return true;
    default:
      return false;
  }
}

void printStatus() {
  printf("tg12864b: bus=%luns %s\n", (unsigned long)tg12864b::busCycleNs(),
         tg12864b::waitMode() == tg12864b::WaitMode::BUSY ? "busy-wait" : "fixed-cycle");
}

const char* help() { return " b/B  bus cycle shorter/longer  w    busy wait / fixed cycle"; }

}  // namespace display
