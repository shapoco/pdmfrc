// Display of the RP2040 / RP2350 examples. The display is chosen at build time
// (CMake PDMFRC_DISPLAY), and exactly one display_<name>.cpp is linked.
// The driver owns two transfer buffers: prepare() pops the next frame into the
// back buffer while the front one is being transferred
// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

#include "pdmfrc/pdmfrc.hpp"

namespace display {

#if defined(PDMFRC_DISPLAY_TG12864B)
constexpr int WIDTH = 128;
constexpr int HEIGHT = 64;
#else
#error "No display selected (PDMFRC_DISPLAY_*)"
#endif

const char* name();

// Recommended settings (width and height are WIDTH and HEIGHT). The palette
// may be null when the display has not been measured yet
const pdmfrc::LcdModule& module();

// Initializes the display. Call after the system clock is set and pdm is
// initialized with a Config of WIDTH x HEIGHT and the packing of module()
void init(pdmfrc::Pdm& pdm);

// Pops the next frame into the back buffer
void prepare();

// Waits for the previous transfer, then starts the transfer of the back buffer
// (prepared by prepare()) and makes it the front one
void startTransfer();

// Duration of the last completed transfer [us]
uint32_t lastTransferUs();

// Keys of the display; true if handled
bool onKey(int c);

// Status line of the display
void printStatus();

// Help lines of the display (null if none)
const char* help();

}  // namespace display
