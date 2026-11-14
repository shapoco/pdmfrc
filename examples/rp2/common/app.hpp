// Frame loop of the RP2040 / RP2350 examples: SG12864 transfer at a regular
// frame rate, Pdm::pop() and the common serial commands
// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

#include "pdmfrc/pdmfrc.hpp"

namespace app {

struct Hooks {
  const char* title = "";          // first line of the help
  bool (*onKey)(int c) = nullptr;  // keys of the example; true if handled
  // Called right after each pop (at the head of the wait for the next frame,
  // so that heavy work does not delay the frame start)
  void (*afterPop)() = nullptr;
  void (*printStatus)() = nullptr;  // status lines of the example
  const char* help = nullptr;       // help lines of the example
};

// System clock [kHz]. Raises the core voltage above 133 MHz. Call before init()
// (the PIO clock divider follows the system clock at that time)
void setClock(uint32_t khz);

// Initializes stdio, the SG12864 and the frame rate (from the module)
void init(pdmfrc::Pdm& pdm, const pdmfrc::LcdModule& module);

// Resets the phase with the current phase mode and a time-based seed
void resetPhase();

void resetStats();
void printStatus();

// Runs the frame loop (never returns)
[[noreturn]] void run(const Hooks& hooks);

}  // namespace app
