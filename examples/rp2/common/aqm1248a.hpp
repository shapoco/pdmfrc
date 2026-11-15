// AQM1248A (128x48, ST7565R, FSTN positive) driver: 4-line SPI by PIO + DMA
// Wiring: RST=GPIO16, CS=GPIO17, SCK=GPIO18, SDI=GPIO19, RS=GPIO20
// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

namespace aqm1248a {

constexpr int WIDTH = 128;
constexpr int HEIGHT = 48;
constexpr int PAGES = HEIGHT / 8;

// Transfer stream: for each page [Set Page, Set Column Upper, Set Column
// Lower, data x WIDTH]. One entry per byte (see aqm1248a.pio)
constexpr int STREAM_PAGE_SLOTS = 3 + WIDTH;
constexpr int STREAM_LEN = PAGES * STREAM_PAGE_SLOTS;

constexpr uint32_t SCLK_MIN_HZ = 500000;
constexpr uint32_t SCLK_MAX_HZ = 20000000;  // tSCYC >= 50 ns at VDD = 3.3 V

constexpr int CONTRAST_MAX = 63;        // electronic volume
constexpr int REGULATOR_RATIO_MAX = 7;  // V0 regulator internal resistor ratio

// Resets and initializes the LCD (clears the display RAM). Call after the
// system clock is set
void init();

// Fills the command entries of the stream (for the current orientation).
// The data entries are left as they are
void initStream(uint16_t* stream);

// 8 vertical pixels per byte with the LSB at the top ([page][x], WIDTH *
// PAGES bytes, the output of Pdm::pop()) -> the data entries of the stream
void fillStream(uint16_t* stream, const uint8_t* pages);

// Waits for the previous transfer, then starts the transfer of stream
void startTransfer(const uint16_t* stream);

// Waits until the transfer is over
void waitIdle();

// Duration of the last completed transfer [us] (DMA completion)
uint32_t lastTransferUs();

// SCLK frequency (SCLK_MIN_HZ..SCLK_MAX_HZ, waits for the transfer)
void setSclkHz(uint32_t hz);
uint32_t sclkHz();

// Electronic volume (0..CONTRAST_MAX) and V0 regulator resistor ratio
// (0..REGULATOR_RATIO_MAX). They set V0 = (1 + Rb/Ra) * VEV (waits for the
// transfer)
void setContrast(int value);
int contrast();
void setRegulatorRatio(int value);
int regulatorRatio();

// V0 estimated from the datasheet [mV] (VREG = 2.1 V at 25 degC)
uint32_t estimatedV0mV();

// Rotates the display 180 degrees (ADC / COM direction and the column offset).
// Call initStream() for the streams afterwards (waits for the transfer)
void setFlipped(bool flipped);
bool flipped();

}  // namespace aqm1248a
