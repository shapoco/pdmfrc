#pragma once

#include <stdint.h>

namespace tg12864b {

inline constexpr int WIDTH = 128;
inline constexpr int HEIGHT = 64;
inline constexpr int PAGES = HEIGHT / 8;
inline constexpr int CHIP_WIDTH = 64;

// 転送ストリーム: ページ毎に [Set Page, Set Column 0, データ 64 バイト] を
// CS1/CS2 交互に並べる (片方の Busy 中にもう片方へ書く)
inline constexpr int STREAM_ROW_SLOTS = 2 + CHIP_WIDTH;
inline constexpr int STREAM_LEN = 2 * PAGES * STREAM_ROW_SLOTS;

// PIO が出力するワードのビット配置 (bit n = GPIO(2+n))。上位 16bit は Status Read 用
inline constexpr int BIT_DI = 0;
inline constexpr int BIT_RW = 1;
inline constexpr int BIT_D0 = 3;
inline constexpr int BIT_CS1 = 11;
inline constexpr int BIT_CS2 = 12;
inline constexpr int READ_SHIFT = 16;

inline constexpr uint32_t makeWord(bool is_data, int half, uint8_t value) {
    const uint32_t cs = (half == 0) ? (1u << BIT_CS1) : (1u << BIT_CS2);
    const uint32_t write = (is_data ? (1u << BIT_DI) : 0u) | ((uint32_t)value << BIT_D0) | cs;
    const uint32_t read = (1u << BIT_RW) | cs;
    return write | (read << READ_SHIFT);
}

// (half, page, slot) のストリーム上のインデックス。slot 0: Set Page, 1: Set Column, 2..: データ
inline constexpr int streamSlotIndex(int half, int page, int slot) {
    return (page * STREAM_ROW_SLOTS + slot) * 2 + half;
}

// データ部 (half, page, col) のストリーム上のインデックス
inline constexpr int streamIndex(int half, int page, int col) {
    return streamSlotIndex(half, page, 2 + col);
}

enum class WaitMode : uint8_t { BUSY, FIXED };

// GPIO 直接制御でリセット・Display ON・Start Line 0 → PIO/DMA 転送に切り替え
void init();

// ストリームのコマンド部を埋め、データ部を 0 クリアする
void initStream(uint32_t* stream);

// 縦 8px パック (LSB が上) の 1 画面分 ([page][x], WIDTH * PAGES バイト。Pdm::pop() の出力) を
// ストリームのデータ部に書き込む
void fillStream(uint32_t* stream, const uint8_t* pages);

// 前回の転送完了を待ってから stream の DMA 転送を開始する
void startTransfer(const uint32_t* stream);
void waitTransfer();

// 直近に完了した転送の所要時間 [µs] (DMA 完了まで。PIO FIFO 残りは含まない)
uint32_t lastTransferUs();

// PIO クロック = 24 / バスサイクル
void setBusCycleNs(uint32_t ns);
uint32_t busCycleNs();

// Busy 待ちモード (転送完了を待ってから PIO プログラムを切り替える)
void setWaitMode(WaitMode mode);
WaitMode waitMode();

}  // namespace tg12864b
