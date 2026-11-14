#include "tg12864b.hpp"

#include <initializer_list>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "tg12864b_write.pio.h"

namespace tg12864b {
namespace {

constexpr uint PIN_DI = 2;
constexpr uint PIN_RW = 3;
constexpr uint PIN_E = 4;
constexpr uint PIN_D0 = 5;  // D0-D7 = GPIO5-12
constexpr uint PIN_CS1 = 13;
constexpr uint PIN_CS2 = 14;
constexpr uint PIN_RES = 15;  // アクティブ Low

constexpr uint PIO_PIN_BASE = PIN_DI;
constexpr uint PIO_PIN_COUNT = PIN_CS2 - PIN_DI + 1;  // 13
constexpr uint PIO_CLOCKS_PER_WRITE = 24;

constexpr uint32_t T_ASU_NS = 140;
constexpr uint32_t T_EW_NS = 450;

constexpr uint32_t DATA_MASK = 0xFFu << PIN_D0;

constexpr uint8_t CMD_DISPLAY_ON = 0x3F;
constexpr uint8_t CMD_SET_COL = 0x40;
constexpr uint8_t CMD_SET_PAGE = 0xB8;
constexpr uint8_t CMD_START_LINE = 0xC0;

constexpr uint8_t STATUS_BUSY = 0x80;
constexpr uint8_t STATUS_RESET = 0x10;

enum Chip : uint8_t { CHIP1 = 1, CHIP2 = 2, BOTH = 3 };

uint32_t cycles_per_us = 125;

PIO pio = pio0;
uint sm = 0;
int dma_ch = -1;
uint32_t cycle_ns = 1200;

// ---- 初期化用 GPIO 直接制御 ----

inline void delay_ns(uint32_t ns) { busy_wait_at_least_cycles(ns * cycles_per_us / 1000 + 1); }

inline void select(uint8_t chip_mask) {
    gpio_put(PIN_CS1, (chip_mask & CHIP1) != 0);
    gpio_put(PIN_CS2, (chip_mask & CHIP2) != 0);
}

uint8_t read_status(Chip one_chip) {
    gpio_set_dir_in_masked(DATA_MASK);
    gpio_put(PIN_DI, 0);
    gpio_put(PIN_RW, 1);
    select(one_chip);
    delay_ns(T_ASU_NS);
    gpio_put(PIN_E, 1);
    delay_ns(T_EW_NS);
    uint8_t status = (gpio_get_all() >> PIN_D0) & 0xFF;
    gpio_put(PIN_E, 0);
    delay_ns(T_EW_NS);
    select(0);
    gpio_put(PIN_RW, 0);
    gpio_set_dir_out_masked(DATA_MASK);
    return status;
}

void wait_ready(uint8_t chip_mask, uint8_t flags = STATUS_BUSY) {
    for (uint8_t chip : {CHIP1, CHIP2}) {
        if (!(chip_mask & chip)) continue;
        while (read_status((Chip)chip) & flags) tight_loop_contents();
    }
}

void write_cmd_gpio(uint8_t chip_mask, uint8_t cmd) {
    wait_ready(chip_mask);
    gpio_put(PIN_DI, 0);
    gpio_put(PIN_RW, 0);
    select(chip_mask);
    gpio_put_masked(DATA_MASK, (uint32_t)cmd << PIN_D0);
    delay_ns(T_ASU_NS);
    gpio_put(PIN_E, 1);
    delay_ns(T_EW_NS);
    gpio_put(PIN_E, 0);
    delay_ns(T_EW_NS);
    select(0);
}

// ---- PIO/DMA ----

uint offset_fixed = 0;
uint offset_busy = 0;
WaitMode cur_mode = WaitMode::BUSY;

volatile uint32_t xfer_start_us = 0;
volatile uint32_t xfer_last_us = 0;

void dma_irq_handler() {
    dma_hw->ints0 = 1u << dma_ch;
    xfer_last_us = time_us_32() - xfer_start_us;
}

void apply_clkdiv() {
    const float div = (float)clock_get_hz(clk_sys) * (float)cycle_ns /
                      (1e9f * (float)PIO_CLOCKS_PER_WRITE);
    pio_sm_set_clkdiv(pio, sm, div < 1.0f ? 1.0f : (div > 65535.0f ? 65535.0f : div));
}

// 現在の cur_mode のプログラムでステートマシンを (再) 起動する
void start_sm() {
    constexpr uint32_t PIN_MASK = ((1u << PIO_PIN_COUNT) - 1) << PIO_PIN_BASE;
    pio_sm_set_enabled(pio, sm, false);

    pio_sm_config c;
    uint offset;
    if (cur_mode == WaitMode::FIXED) {
        offset = offset_fixed;
        c = tg12864b_fixed_program_get_default_config(offset);
        sm_config_set_out_shift(&c, true, true, 32);  // 右シフト, autopull 32bit
    } else {
        offset = offset_busy;
        c = tg12864b_busy_program_get_default_config(offset);
        sm_config_set_out_shift(&c, true, false, 32);  // 右シフト, autopull なし
        sm_config_set_jmp_pin(&c, PIN_D0 + 7);         // D7 = Busy
    }
    sm_config_set_out_pins(&c, PIO_PIN_BASE, PIO_PIN_COUNT);
    sm_config_set_sideset_pins(&c, PIN_E);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    // E=Low, CS 非選択, 全ピン出力
    pio_sm_set_pins_with_mask(pio, sm, 0, PIN_MASK);
    pio_sm_set_pindirs_with_mask(pio, sm, PIN_MASK, PIN_MASK);

    pio_sm_init(pio, sm, offset, &c);
    apply_clkdiv();

    if (cur_mode == WaitMode::BUSY) {
        // Y = Status Read 中の pindirs (D0-D7 のみ入力)
        constexpr uint32_t READ_DIRS = ((1u << PIO_PIN_COUNT) - 1) & ~(0xFFu << BIT_D0);
        pio_sm_put(pio, sm, READ_DIRS);
        pio_sm_exec(pio, sm, pio_encode_pull(false, true));
        pio_sm_exec(pio, sm, pio_encode_mov(pio_y, pio_osr));
    }

    pio_sm_set_enabled(pio, sm, true);
}

void init_pio_dma() {
    sm = pio_claim_unused_sm(pio, true);
    offset_fixed = pio_add_program(pio, &tg12864b_fixed_program);
    offset_busy = pio_add_program(pio, &tg12864b_busy_program);

    for (uint pin = PIO_PIN_BASE; pin < PIO_PIN_BASE + PIO_PIN_COUNT; ++pin) {
        if (pin == PIN_RW) continue;  // R/W は Busy 待ちモードのみ PIO が駆動
        pio_gpio_init(pio, pin);
    }
    start_sm();
    if (cur_mode == WaitMode::BUSY) pio_gpio_init(pio, PIN_RW);

    dma_ch = dma_claim_unused_channel(true);
    dma_channel_config d = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
    channel_config_set_read_increment(&d, true);
    channel_config_set_write_increment(&d, false);
    channel_config_set_dreq(&d, pio_get_dreq(pio, sm, true));
    dma_channel_configure(dma_ch, &d, &pio->txf[sm], nullptr, STREAM_LEN, false);

    dma_channel_set_irq0_enabled(dma_ch, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

}  // namespace

void init() {
    cycles_per_us = clock_get_hz(clk_sys) / 1000000;

    for (uint pin = PIN_DI; pin <= PIN_RES; ++pin) {
        gpio_init(pin);
        gpio_put(pin, 0);
        gpio_set_dir(pin, GPIO_OUT);
    }
    select(0);

    sleep_ms(50);
    gpio_put(PIN_RES, 1);
    sleep_ms(1);
    wait_ready(BOTH, STATUS_BUSY | STATUS_RESET);

    write_cmd_gpio(BOTH, CMD_DISPLAY_ON);
    write_cmd_gpio(BOTH, CMD_START_LINE | 0);
    wait_ready(BOTH);

    init_pio_dma();
}

void initStream(uint32_t* stream) {
    for (int half = 0; half < 2; ++half) {
        for (int page = 0; page < PAGES; ++page) {
            stream[streamSlotIndex(half, page, 0)] = makeWord(false, half, CMD_SET_PAGE | page);
            stream[streamSlotIndex(half, page, 1)] = makeWord(false, half, CMD_SET_COL | 0);
            for (int col = 0; col < CHIP_WIDTH; ++col) {
                stream[streamIndex(half, page, col)] = makeWord(true, half, 0);
            }
        }
    }
}

void __not_in_flash_func(fillStream)(uint32_t* stream, const uint8_t* pages) {
    for (int page = 0; page < PAGES; ++page) {
        for (int x = 0; x < WIDTH; ++x) {
            const int half = x / CHIP_WIDTH;
            stream[streamIndex(half, page, x % CHIP_WIDTH)] = makeWord(true, half, pages[page * WIDTH + x]);
        }
    }
}

void waitTransfer() { dma_channel_wait_for_finish_blocking(dma_ch); }

void startTransfer(const uint32_t* stream) {
    waitTransfer();
    xfer_start_us = time_us_32();
    dma_channel_transfer_from_buffer_now(dma_ch, stream, STREAM_LEN);
}

uint32_t lastTransferUs() { return xfer_last_us; }

void setBusCycleNs(uint32_t ns) {
    cycle_ns = ns;
    apply_clkdiv();
}

uint32_t busCycleNs() { return cycle_ns; }

void setWaitMode(WaitMode mode) {
    if (mode == cur_mode) return;
    waitTransfer();
    // FIFO が空になり、最後の書き込みサイクルが終わるまで待つ
    while (!pio_sm_is_tx_fifo_empty(pio, sm)) tight_loop_contents();
    sleep_us(cycle_ns / 1000 * 4 + 100);
    cur_mode = mode;
    if (mode == WaitMode::BUSY) {
        start_sm();
        pio_gpio_init(pio, PIN_RW);
    } else {
        // R/W を SIO (Low 固定) に戻してから切り替える
        gpio_init(PIN_RW);
        gpio_put(PIN_RW, 0);
        gpio_set_dir(PIN_RW, GPIO_OUT);
        start_sm();
    }
}

WaitMode waitMode() { return cur_mode; }

}  // namespace tg12864b
