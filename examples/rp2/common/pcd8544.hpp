// PCD8544 (Nokia 5110 LCD module, 84x48, positive) driver: SPI by PIO + DMA.
// Wiring (the same as the AQM1248A): RST=GPIO16, CE=GPIO17, CLK=GPIO18,
// DIN=GPIO19, DC=GPIO20
// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

namespace pcd8544 {

constexpr int WIDTH = 84;
constexpr int HEIGHT = 48;
constexpr int PAGES = HEIGHT / 8;

// Transfer stream: [Set Y 0, Set X 0, data x WIDTH * PAGES]. In the horizontal
// addressing the X address wraps to the next page by itself, so a frame is
// one run of bytes. One entry per byte (the PIO program of the AQM1248A,
// aqm1248a.pio: bit 15 = DC, bits 14..7 = byte)
constexpr int STREAM_CMD_SLOTS = 2;
constexpr int STREAM_LEN = STREAM_CMD_SLOTS + WIDTH * PAGES;

constexpr uint32_t SCLK_MIN_HZ = 500000;
constexpr uint32_t SCLK_MAX_HZ = 4000000;  // tcy >= 250 ns

// V_LCD = 3.06 V + 0.06 V * Vop (temperature coefficient 0). The register
// goes to 127 but V_LCD must stay below 8.5 V (absolute maximum)
constexpr int VOP_MAX = 90;
constexpr int BIAS_MAX = 7;  // bias system (3: 1/48 mux rate, recommended for 48 rows)

// Resets and initializes the LCD (clears the display RAM). Call after the
// system clock is set
void init();

// Fills the command entries of the stream. The data entries are left as they
// are
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

// Operation voltage (contrast, 0..VOP_MAX) and bias system (0..BIAS_MAX).
// They wait for the transfer
void setVop(int value);
int vop();
void setBias(int value);
int bias();

// V_LCD estimated from the datasheet [mV]
uint32_t estimatedVlcdMv();

}  // namespace pcd8544
