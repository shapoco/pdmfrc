#pragma once

#include <stdint.h>

// 夕焼けの空と波打つ水面、その上で回転する金属キューブ (アーキテクチャ非依存。描画は ShapoGFX)
namespace scene {

// バックバッファ: 縦長 128x256、バイトスワップ済み RGB565
constexpr int BB_W = 128;
constexpr int BB_H = 256;

// 各段階の所要時間 [us] (直近のフレーム)
struct Timing {
    uint32_t background;  // 空と水面
    uint32_t reflection;  // 水面下のキューブ (シーン構築 + 1 ラインずつ描画)
    uint32_t cube;        // 水面上のキューブ
    uint32_t total;
};

// マイクロ秒単位の時刻を返す関数 (Timing の計測用)
using ClockFn = uint32_t (*)();

// 環境マップ用テクスチャの生成と 3D レンダラの初期化 (起動時に 1 回)。clock は nullptr でもよい
void init(ClockFn clock);

// 時刻 t [s] のシーンを dst (BB_W x BB_H) に描画する
void render(uint16_t* dst, float t, Timing& timing);

// 3D レンダラの統計 (直近のフレーム) を表示する
void print_stats();

}  // namespace scene
