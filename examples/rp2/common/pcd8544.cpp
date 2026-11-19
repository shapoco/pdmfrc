// SPDX-License-Identifier: MIT
#include "pcd8544.hpp"

#include "aqm1248a.pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

namespace pcd8544 {
namespace {

constexpr uint PIN_RST = 16;  // active Low
constexpr uint PIN_CE = 17;   // active Low
constexpr uint PIN_CLK = 18;
constexpr uint PIN_DIN = 19;
constexpr uint PIN_DC = 20;  // H: display data, L: command

// Function set: 0x20 | PD (power down) << 2 | V (vertical addressing) << 1 | H
constexpr uint8_t CMD_FUNCTION_BASIC = 0x20;     // H = 0, horizontal addressing
constexpr uint8_t CMD_FUNCTION_EXTENDED = 0x21;  // H = 1
// H = 0
constexpr uint8_t CMD_DISPLAY_BLANK = 0x08;
constexpr uint8_t CMD_DISPLAY_NORMAL = 0x0C;
constexpr uint8_t CMD_SET_Y = 0x40;  // | 0..5
constexpr uint8_t CMD_SET_X = 0x80;  // | 0..83
// H = 1
constexpr uint8_t CMD_TEMP_COEF = 0x04;  // | 0..3
constexpr uint8_t CMD_BIAS = 0x10;       // | 0..7
constexpr uint8_t CMD_SET_VOP = 0x80;    // | 0..127

PIO pio = pio0;
uint sm = 0;
int dma_ch = -1;
uint32_t sclk_hz = SCLK_MAX_HZ;
int cur_vop = 64;  // V_LCD = 3.06 + 0.06 * 64 = 6.9 V. Modules differ: tune by eye (k/K)
int cur_bias = 3;  // 1/48 mux rate

volatile uint32_t xfer_start_us = 0;
volatile uint32_t xfer_last_us = 0;

constexpr uint16_t entry(bool data, uint8_t value) {
  return (uint16_t)((data ? 0x8000u : 0u) | ((uint32_t)value << 7));
}

void dma_irq_handler() {
  dma_hw->ints0 = 1u << dma_ch;
  xfer_last_us = time_us_32() - xfer_start_us;
}

void apply_clkdiv() {
  const float div = (float)clock_get_hz(clk_sys) / (2.0f * (float)sclk_hz);
  pio_sm_set_clkdiv(pio, sm, div < 1.0f ? 1.0f : (div > 65535.0f ? 65535.0f : div));
}

void init_pio_dma() {
  sm = pio_claim_unused_sm(pio, true);
  const uint prog_offset = pio_add_program(pio, &aqm1248a_program);

  constexpr uint32_t PIN_MASK = (1u << PIN_CLK) | (1u << PIN_DIN) | (1u << PIN_DC);
  pio_sm_set_pins_with_mask(pio, sm, 0, PIN_MASK);
  pio_sm_set_pindirs_with_mask(pio, sm, PIN_MASK, PIN_MASK);
  pio_gpio_init(pio, PIN_CLK);
  pio_gpio_init(pio, PIN_DIN);
  pio_gpio_init(pio, PIN_DC);

  pio_sm_config c = aqm1248a_program_get_default_config(prog_offset);
  sm_config_set_sideset_pins(&c, PIN_CLK);
  sm_config_set_out_pins(&c, PIN_DIN, 1);
  sm_config_set_set_pins(&c, PIN_DC, 1);
  sm_config_set_out_shift(&c, false, true, 9);  // shift left (DC, then MSB first), autopull 9bit
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
  pio_sm_init(pio, sm, prog_offset, &c);
  apply_clkdiv();
  pio_sm_set_enabled(pio, sm, true);

  dma_ch = dma_claim_unused_channel(true);
  dma_channel_config d = dma_channel_get_default_config(dma_ch);
  channel_config_set_transfer_data_size(&d, DMA_SIZE_16);
  channel_config_set_read_increment(&d, true);
  channel_config_set_write_increment(&d, false);
  channel_config_set_dreq(&d, pio_get_dreq(pio, sm, true));
  dma_channel_configure(dma_ch, &d, &pio->txf[sm], nullptr, STREAM_LEN, false);

  dma_channel_set_irq0_enabled(dma_ch, true);
  irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
  irq_set_enabled(DMA_IRQ_0, true);
}

// Waits for the PIO: once the FIFO is empty, the program stalls on the
// autopull of its first instruction after the last byte (TXSTALL). The OSR may
// still hold a byte while the FIFO is empty, so the FIFO alone is not enough
void wait_pio_idle() {
  while (!pio_sm_is_tx_fifo_empty(pio, sm)) tight_loop_contents();
  const uint32_t stall = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
  pio->fdebug = stall;
  while (!(pio->fdebug & stall)) tight_loop_contents();
  busy_wait_us_32(1);
}

// Starts a transfer: pulses CE high to reset the serial interface of the LCD
void begin_transfer() {
  gpio_put(PIN_CE, 1);
  busy_wait_us_32(1);
  gpio_put(PIN_CE, 0);
}

// Commands by the CPU (no DMA)
void write_commands(const uint8_t* cmds, int n) {
  waitIdle();
  begin_transfer();
  for (int i = 0; i < n; ++i) pio_sm_put_blocking(pio, sm, (uint32_t)entry(false, cmds[i]) << 16);
  wait_pio_idle();
}

// Extended instruction set (H = 1), then back to the basic one
void apply_extended() {
  const uint8_t cmds[] = {CMD_FUNCTION_EXTENDED, CMD_TEMP_COEF | 0, (uint8_t)(CMD_BIAS | cur_bias),
                          (uint8_t)(CMD_SET_VOP | cur_vop), CMD_FUNCTION_BASIC};
  write_commands(cmds, sizeof(cmds));
}

void clear_ram() {
  const uint8_t cmds[] = {CMD_SET_Y | 0, CMD_SET_X | 0};
  write_commands(cmds, sizeof(cmds));
  begin_transfer();
  for (int i = 0; i < WIDTH * PAGES; ++i) pio_sm_put_blocking(pio, sm, (uint32_t)entry(true, 0) << 16);
  wait_pio_idle();
}

}  // namespace

void init() {
  gpio_init(PIN_RST);
  gpio_put(PIN_RST, 0);
  gpio_set_dir(PIN_RST, GPIO_OUT);
  gpio_init(PIN_CE);
  gpio_put(PIN_CE, 1);
  gpio_set_dir(PIN_CE, GPIO_OUT);
  init_pio_dma();

  sleep_ms(1);
  gpio_put(PIN_RST, 1);
  sleep_ms(1);

  apply_extended();
  const uint8_t blank[] = {CMD_DISPLAY_BLANK};
  write_commands(blank, sizeof(blank));
  clear_ram();
  const uint8_t normal[] = {CMD_DISPLAY_NORMAL};
  write_commands(normal, sizeof(normal));
}

void initStream(uint16_t* stream) {
  stream[0] = entry(false, CMD_SET_Y | 0);
  stream[1] = entry(false, CMD_SET_X | 0);
}

void __not_in_flash_func(fillStream)(uint16_t* stream, const uint8_t* pages) {
  uint16_t* p = stream + STREAM_CMD_SLOTS;
  for (int i = 0; i < WIDTH * PAGES; ++i) p[i] = (uint16_t)(0x8000u | ((uint32_t)pages[i] << 7));
}

void waitIdle() {
  if (dma_ch >= 0) dma_channel_wait_for_finish_blocking(dma_ch);
  wait_pio_idle();
}

void startTransfer(const uint16_t* stream) {
  waitIdle();
  begin_transfer();
  xfer_start_us = time_us_32();
  dma_channel_transfer_from_buffer_now(dma_ch, stream, STREAM_LEN);
}

uint32_t lastTransferUs() { return xfer_last_us; }

void setSclkHz(uint32_t hz) {
  sclk_hz = hz < SCLK_MIN_HZ ? SCLK_MIN_HZ : (hz > SCLK_MAX_HZ ? SCLK_MAX_HZ : hz);
  waitIdle();
  apply_clkdiv();
}

uint32_t sclkHz() { return sclk_hz; }

void setVop(int value) {
  cur_vop = value < 0 ? 0 : (value > VOP_MAX ? VOP_MAX : value);
  apply_extended();
}

int vop() { return cur_vop; }

void setBias(int value) {
  cur_bias = value < 0 ? 0 : (value > BIAS_MAX ? BIAS_MAX : value);
  apply_extended();
}

int bias() { return cur_bias; }

uint32_t estimatedVlcdMv() { return 3060 + 60 * (uint32_t)cur_vop; }

}  // namespace pcd8544
