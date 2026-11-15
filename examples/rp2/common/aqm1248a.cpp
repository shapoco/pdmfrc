// SPDX-License-Identifier: MIT
#include "aqm1248a.hpp"

#include "aqm1248a.pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

namespace aqm1248a {
namespace {

constexpr uint PIN_RST = 16;  // active Low
constexpr uint PIN_CS = 17;   // active Low
constexpr uint PIN_SCK = 18;
constexpr uint PIN_SDI = 19;
constexpr uint PIN_RS = 20;  // H: display data, L: command

constexpr uint8_t CMD_DISPLAY_OFF = 0xAE;
constexpr uint8_t CMD_DISPLAY_ON = 0xAF;
constexpr uint8_t CMD_START_LINE = 0x40;
constexpr uint8_t CMD_SET_PAGE = 0xB0;
constexpr uint8_t CMD_SET_COL_UPPER = 0x10;
constexpr uint8_t CMD_SET_COL_LOWER = 0x00;
constexpr uint8_t CMD_ADC_NORMAL = 0xA0;
constexpr uint8_t CMD_ADC_REVERSE = 0xA1;
constexpr uint8_t CMD_DISPLAY_NORMAL = 0xA6;
constexpr uint8_t CMD_ALL_POINTS_OFF = 0xA4;
constexpr uint8_t CMD_BIAS_1_6 = 0xA3;  // 1/6 bias at 1/49 duty
constexpr uint8_t CMD_COM_NORMAL = 0xC0;
constexpr uint8_t CMD_COM_REVERSE = 0xC8;
constexpr uint8_t CMD_POWER_CONTROL = 0x28;  // | booster 4 | regulator 2 | follower 1
constexpr uint8_t CMD_REGULATOR_RATIO = 0x20;
constexpr uint8_t CMD_ELECTRONIC_VOLUME = 0x81;  // followed by the value

constexpr int RAM_COLUMNS = 132;

PIO pio = pio0;
uint sm = 0;
int dma_ch = -1;
uint32_t sclk_hz = 10000000;
// The palette (pdmfrc/palettes/aqm1248a.hpp) was made with these:
// V0 = (1 + Rb/Ra) * (1 - alpha / 162) * VREG = 5.0 * (1 - 41 / 162) * 2.1 = 7.84 V
int cur_contrast = 22;  // electronic volume (alpha = 63 - 22 = 41)
int cur_ratio = 4;      // 1 + Rb/Ra = 5.0
bool cur_flipped = false;

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

  constexpr uint32_t PIN_MASK = (1u << PIN_SCK) | (1u << PIN_SDI) | (1u << PIN_RS);
  pio_sm_set_pins_with_mask(pio, sm, 0, PIN_MASK);
  pio_sm_set_pindirs_with_mask(pio, sm, PIN_MASK, PIN_MASK);
  pio_gpio_init(pio, PIN_SCK);
  pio_gpio_init(pio, PIN_SDI);
  pio_gpio_init(pio, PIN_RS);

  pio_sm_config c = aqm1248a_program_get_default_config(prog_offset);
  sm_config_set_sideset_pins(&c, PIN_SCK);
  sm_config_set_out_pins(&c, PIN_SDI, 1);
  sm_config_set_set_pins(&c, PIN_RS, 1);
  sm_config_set_out_shift(&c, false, true, 9);  // shift left (RS, then MSB first), autopull 9bit
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
  busy_wait_us_32(1);  // tCSH
}

// Starts a transfer: pulses CS high to reset the serial counter of the LCD
void begin_transfer() {
  gpio_put(PIN_CS, 1);
  busy_wait_us_32(1);
  gpio_put(PIN_CS, 0);
}

// Commands by the CPU (no DMA)
void write_commands(const uint8_t* cmds, int n) {
  waitIdle();
  begin_transfer();
  for (int i = 0; i < n; ++i) pio_sm_put_blocking(pio, sm, (uint32_t)entry(false, cmds[i]) << 16);
  wait_pio_idle();
}

void write_command(uint8_t cmd) { write_commands(&cmd, 1); }

void apply_v0() {
  const uint8_t cmds[] = {(uint8_t)(CMD_REGULATOR_RATIO | cur_ratio), CMD_ELECTRONIC_VOLUME,
                          (uint8_t)cur_contrast};
  write_commands(cmds, sizeof(cmds));
}

void apply_orientation() {
  const uint8_t cmds[] = {cur_flipped ? CMD_ADC_REVERSE : CMD_ADC_NORMAL,
                          cur_flipped ? CMD_COM_NORMAL : CMD_COM_REVERSE};
  write_commands(cmds, sizeof(cmds));
}

void clear_ram() {
  for (int page = 0; page < 8; ++page) {  // pages 0..7 of all 132 columns
    const uint8_t cmds[] = {(uint8_t)(CMD_SET_PAGE | page), CMD_SET_COL_UPPER, CMD_SET_COL_LOWER};
    write_commands(cmds, sizeof(cmds));
    begin_transfer();
    for (int x = 0; x < RAM_COLUMNS; ++x)
      pio_sm_put_blocking(pio, sm, (uint32_t)entry(true, 0) << 16);
    wait_pio_idle();
  }
}

}  // namespace

void init() {
  gpio_init(PIN_RST);
  gpio_put(PIN_RST, 0);
  gpio_set_dir(PIN_RST, GPIO_OUT);
  gpio_init(PIN_CS);
  gpio_put(PIN_CS, 1);
  gpio_set_dir(PIN_CS, GPIO_OUT);
  init_pio_dma();

  sleep_ms(1);
  gpio_put(PIN_RST, 1);
  sleep_ms(5);

  // Initialization flow of the ST7565R datasheet (built-in power supply)
  const uint8_t cmds[] = {CMD_DISPLAY_OFF, CMD_BIAS_1_6, CMD_START_LINE | 0};
  write_commands(cmds, sizeof(cmds));
  apply_orientation();
  apply_v0();
  write_command(CMD_POWER_CONTROL | 0x4);  // booster
  sleep_ms(2);
  write_command(CMD_POWER_CONTROL | 0x6);  // + regulator
  sleep_ms(2);
  write_command(CMD_POWER_CONTROL | 0x7);  // + follower
  sleep_ms(5);
  clear_ram();
  const uint8_t on[] = {CMD_DISPLAY_NORMAL, CMD_ALL_POINTS_OFF, CMD_DISPLAY_ON};
  write_commands(on, sizeof(on));
}

void initStream(uint16_t* stream) {
  // Reversed ADC maps column 0 to SEG131: the visible columns start at 4
  const int col = cur_flipped ? RAM_COLUMNS - WIDTH : 0;
  for (int page = 0; page < PAGES; ++page) {
    uint16_t* p = stream + page * STREAM_PAGE_SLOTS;
    p[0] = entry(false, (uint8_t)(CMD_SET_PAGE | page));
    p[1] = entry(false, (uint8_t)(CMD_SET_COL_UPPER | (col >> 4)));
    p[2] = entry(false, (uint8_t)(CMD_SET_COL_LOWER | (col & 0xF)));
  }
}

void __not_in_flash_func(fillStream)(uint16_t* stream, const uint8_t* pages) {
  for (int page = 0; page < PAGES; ++page) {
    uint16_t* p = stream + page * STREAM_PAGE_SLOTS + 3;
    const uint8_t* src = pages + page * WIDTH;
    for (int x = 0; x < WIDTH; ++x) p[x] = (uint16_t)(0x8000u | ((uint32_t)src[x] << 7));
  }
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

void setContrast(int value) {
  cur_contrast = value < 0 ? 0 : (value > CONTRAST_MAX ? CONTRAST_MAX : value);
  apply_v0();
}

int contrast() { return cur_contrast; }

void setRegulatorRatio(int value) {
  cur_ratio = value < 0 ? 0 : (value > REGULATOR_RATIO_MAX ? REGULATOR_RATIO_MAX : value);
  apply_v0();
}

int regulatorRatio() { return cur_ratio; }

uint32_t estimatedV0mV() {
  // V0 = (1 + Rb/Ra) * (1 - alpha / 162) * VREG, alpha = 63 - register,
  // 1 + Rb/Ra = 3.0 + 0.5 * ratio
  const uint32_t alpha = (uint32_t)(63 - cur_contrast);
  return (uint32_t)((30 + 5 * cur_ratio) * (162 - alpha) * 2100 / (10 * 162));
}

void setFlipped(bool f) {
  cur_flipped = f;
  apply_orientation();
}

bool flipped() { return cur_flipped; }

}  // namespace aqm1248a
