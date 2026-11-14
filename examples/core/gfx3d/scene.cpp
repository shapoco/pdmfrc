#include "scene.hpp"

#include <math.h>
#include <stdio.h>

#include "shapoco/gfx3d/gfx3d.hpp"

// Attribute of the per-pixel background loop (e.g. a section placed in RAM)
#ifndef SCENE_HOT_ATTR
#define SCENE_HOT_ATTR
#endif

namespace scene {
namespace {

uint32_t no_clock() { return 0; }
ClockFn now_us = no_clock;  // 所要時間の計測用 (init() で与える)

namespace g2 = shapoco::gfx2d;
namespace g3 = shapoco::gfx3d;

constexpr float PI = 3.14159265358979f;

// ---------------------------------------------------------------------------
// カメラとシーン

constexpr float FOV_Y = 60.0f * PI / 180.0f;
constexpr float ASPECT = (float)BB_W / BB_H;
// 注視点はキューブ直下の水面 (原点)。カメラはその周りを周回する
constexpr float PITCH = 0.25f;     // 見下ろす角度 [rad] (固定。背景の描画がこれを前提にする)
constexpr float DIST = 5.5f;       // 注視点からカメラまでの距離
constexpr float YAW_SPEED = 0.12f;  // カメラが周回する速さ [rad/s]

constexpr float CUBE_H = 1.3f;  // キューブ中心の高さ
constexpr float CUBE_SIZE = 1.2f;

// 平行光源 (光の進む向き、視点座標系)。カメラ側の斜め上から奥へ向かって照らし、見えている面に光を当てる。
// 水面上は上から下へ、水面下の像は下から上へ (y の符号を反転)
constexpr float LIGHT_X = 0.3f, LIGHT_Y = 0.35f, LIGHT_Z = -0.85f;
// ShapoGFX は照明の結果を 1.0 で頭打ちにするので、光を受けた面はテクスチャの色そのものになる
const g3::colorf LIGHT_COL = {1.0f, 0.97f, 0.9f, 1.0f};
// 環境光は暗めにしてコントラストを強める (LCD の最暗色に沈まない程度)
const g3::colorf AMBIENT_COL = {0.4f, 0.36f, 0.44f, 1.0f};

// ---------------------------------------------------------------------------
// 環境マッピング用テクスチャ (128x128, バイトスワップ済み RGB565)
//   行   0.. 63: 空 (天頂 → 地平線)
//   行  64..127: 水面 (地平線 → 真下、空を上下反転して暗くしたもの)
//   横 128 px で方位 90° (360° で 4 回繰り返す)
// ShapoGFX の環境マップは視点座標系の法線 n から (u, v) = ((nx + 1) / 2, (1 - ny) / 2)
// を引くので、上向きの面に天頂、横向きの面に地平線、下向きの面に水面が写る。

constexpr int TEX_W = 128, TEX_H = 128, SKY_ROWS = TEX_H / 2;
constexpr float TEX_U_PER_RAD = TEX_W / (PI / 2);

uint16_t env_pixels[TEX_W * TEX_H];
const g3::Texture env_tex = {g3::PixelFormat::RGB565_SWAPPED, TEX_W, TEX_H, TEX_W * 2, env_pixels};

// 空の行 ↔ 仰角 e: v = SKY_ROWS * (1 - sqrt(sin e))。地平線付近の解像度を上げる
float sky_row_f(float sin_e) { return SKY_ROWS * (1.0f - sqrtf(sin_e > 0 ? sin_e : 0)); }

uint16_t pack_swapped(float r, float g, float b) {
    auto q = [](float v, int maxv) {
        int i = (int)lroundf(v * maxv);
        return i < 0 ? 0 : (i > maxv ? maxv : i);
    };
    const uint16_t c = (uint16_t)((q(r, 31) << 11) | (q(g, 63) << 5) | q(b, 31));
    return __builtin_bswap16(c);
}

uint32_t hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// 横方向に周期 period (格子数) で繰り返す値ノイズ (0..1)
float value_noise(float x, float y, int period) {
    const int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    auto h = [&](int ix, int iy) {
        return (float)(hash2(((ix % period) + period) % period, iy) & 0xffff) / 65535.0f;
    };
    const float n0 = h(xi, yi) + (h(xi + 1, yi) - h(xi, yi)) * fx;
    const float n1 = h(xi, yi + 1) + (h(xi + 1, yi + 1) - h(xi, yi + 1)) * fx;
    return n0 + (n1 - n0) * fy;
}

struct Rgb {
    float r, g, b;
};
Rgb lerp(const Rgb& a, const Rgb& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// 夕焼けのグラデーション。h: 0 = 地平線, 1 = 天頂
Rgb sky_gradient(float h) {
    struct Stop {
        float h;
        Rgb c;
    };
    static const Stop STOPS[] = {
        {0.00f, {1.00f, 0.96f, 0.80f}},  // 白っぽい黄色
        {0.15f, {1.00f, 0.78f, 0.35f}},  // 黄色
        {0.40f, {0.85f, 0.42f, 0.52f}},  // 赤紫
        {0.65f, {0.42f, 0.24f, 0.60f}},  // 紫
        {1.00f, {0.08f, 0.12f, 0.42f}},  // 青
    };
    for (size_t i = 1; i < sizeof(STOPS) / sizeof(STOPS[0]); ++i) {
        if (h <= STOPS[i].h) {
            return lerp(STOPS[i - 1].c, STOPS[i].c, (h - STOPS[i - 1].h) / (STOPS[i].h - STOPS[i - 1].h));
        }
    }
    return STOPS[sizeof(STOPS) / sizeof(STOPS[0]) - 1].c;
}

Rgb unpack_swapped(uint16_t v) {
    const uint16_t c = __builtin_bswap16(v);
    return {(c >> 11) / 31.0f, ((c >> 5) & 63) / 63.0f, (c & 31) / 31.0f};
}

void generate_texture() {
    for (int v = 0; v < SKY_ROWS; ++v) {
        const float h = 1.0f - (v + 0.5f) / SKY_ROWS;  // sqrt(sin e)
        const Rgb base = sky_gradient(h);
        for (int u = 0; u < TEX_W; ++u) {
            Rgb c = base;
            // 太陽の光 (地平線近く、u = 64 を中心に)
            const float du = (u - 64) / 28.0f, dv = h / 0.18f;
            const float glow = expf(-(du * du + dv * dv));
            c = lerp(c, {1.0f, 0.95f, 0.75f}, glow * 0.8f);
            // 雲: 2 オクターブの値ノイズ。地平線近くは平たく、天頂ほど少ない
            const float n = value_noise(u / 16.0f, v / 5.0f, TEX_W / 16) * 0.65f +
                            value_noise(u / 8.0f, v / 2.5f, TEX_W / 8) * 0.35f;
            float cloud = (n - 0.5f) * 3.5f * (1.0f - h * 0.5f);
            if (cloud > 0) {
                if (cloud > 1) cloud = 1;
                // 下の方の雲ほど夕日に照らされて明るい
                const Rgb cc = lerp({1.0f, 0.97f, 0.92f}, {0.55f, 0.42f, 0.62f}, h);
                c = lerp(c, cc, cloud);
            }
            env_pixels[v * TEX_W + u] = pack_swapped(c.r, c.g, c.b);
        }
    }
    // 水面: 空を上下反転し、真下に向かうほど深い青に寄せる (地平線付近ほどよく映る)。
    // LCD の最暗色 (消灯 = 鮮やかな青紫) より暗い色はすべて同じ階調に潰れるので、深い色もその程度の明るさにする
    const Rgb deep = {0.50f, 0.32f, 0.95f};
    for (int k = 0; k < SKY_ROWS; ++k) {
        const float f = 0.92f - 0.35f * (float)k / (SKY_ROWS - 1);
        for (int u = 0; u < TEX_W; ++u) {
            const Rgb c = lerp(deep, unpack_swapped(env_pixels[(SKY_ROWS - 1 - k) * TEX_W + u]), f);
            env_pixels[(SKY_ROWS + k) * TEX_W + u] = pack_swapped(c.r, c.g, c.b);
        }
    }
}

// ---------------------------------------------------------------------------
// 背景 (空と水面) をバックバッファに直接描く。カメラの見下ろし角は固定なので、
// 画面の行 → 仰角、列 → 方位 は起動時に表にしておく (仰角は画面中央の列で代表させる)

constexpr float FOCAL = (BB_H / 2) / 0.57735027f;  // tan(FOV_Y / 2) = tan 30°
const float EYE_H = DIST * sinf(PITCH);

int32_t col_u[BB_W];     // 列 → 方位 (テクスチャの u, Q8)
int16_t row_v[BB_H];     // 行 → テクスチャの v (波なし)
int32_t row_phase[BB_H];  // 水面の行 → 波の位相 (1 周 = 65536)。カメラから水面上の点までの距離に比例
int16_t row_amp[BB_H];   // 水面の行 → 波の振幅の係数 (Q8)。地平線に近いほど小さい
int horizon_row = BB_H;  // 最初の水面の行
int8_t sin_tab[256];

// 波 (2 つの正弦波の和)。距離 1 あたりの位相 [周]、1 秒あたりの位相 [周]
constexpr float WAVE1_K = 1.0f / 0.9f, WAVE1_W = 0.55f;
constexpr float WAVE2_K = 1.0f / 0.37f, WAVE2_W = 0.9f;
constexpr int WAVE_TEX_AMP = 4;     // 水面のテクスチャ参照をずらす量 [テクセル]
constexpr int WAVE_SCREEN_AMP = 3;  // 水面下のキューブの像をずらす量 [画面の行]

int8_t row_wave[BB_H];  // 毎フレームの波の高さ (-127..127)

void init_tables() {
    for (int i = 0; i < 256; ++i) sin_tab[i] = (int8_t)lroundf(127.0f * sinf(i * 2 * PI / 256));
    for (int x = 0; x < BB_W; ++x) {
        col_u[x] = (int32_t)lroundf(atanf((x + 0.5f - BB_W / 2) / FOCAL) * TEX_U_PER_RAD * 256.0f);
    }
    horizon_row = BB_H;
    for (int y = 0; y < BB_H; ++y) {
        const float e = atanf((BB_H / 2 - (y + 0.5f)) / FOCAL) - PITCH;  // 仰角
        if (e > 0) {
            int v = (int)sky_row_f(sinf(e));
            row_v[y] = (int16_t)(v > SKY_ROWS - 1 ? SKY_ROWS - 1 : v);
            row_phase[y] = 0;
            row_amp[y] = 0;
        } else {
            if (horizon_row == BB_H) horizon_row = y;
            const float s = sinf(-e);
            int v = TEX_H - 1 - (int)sky_row_f(s);
            row_v[y] = (int16_t)(v < SKY_ROWS ? SKY_ROWS : v);
            const float d = EYE_H / (tanf(-e) + 1e-4f);  // 水面上の点までの水平距離
            row_phase[y] = (int32_t)fmodf(d * 65536.0f, 65536.0f * 4096.0f);
            const float a = s * 4.0f;
            row_amp[y] = (int16_t)lroundf((a > 1.0f ? 1.0f : a) * 256.0f);
        }
    }
}

void update_waves(float t) {
    const int32_t p1 = (int32_t)(t * WAVE1_W * 65536.0f), p2 = (int32_t)(t * WAVE2_W * 65536.0f);
    const int32_t k1 = (int32_t)(WAVE1_K * 256.0f), k2 = (int32_t)(WAVE2_K * 256.0f);
    for (int y = horizon_row; y < BB_H; ++y) {
        // 位相 (Q16 の距離 × Q8 の波数) を上位 8bit にまとめて表を引く
        const uint32_t ph1 = (uint32_t)(((int64_t)row_phase[y] * k1 >> 8) - p1) >> 8;
        const uint32_t ph2 = (uint32_t)(((int64_t)row_phase[y] * k2 >> 8) - p2) >> 8;
        const int w = (sin_tab[ph1 & 255] * 3 + sin_tab[ph2 & 255] * 2) / 5;
        row_wave[y] = (int8_t)(w * row_amp[y] >> 8);
    }
}

SCENE_HOT_ATTR void draw_background(uint16_t* dst, float yaw) {
    const int32_t yaw_u = (int32_t)(yaw * TEX_U_PER_RAD * 256.0f);
    for (int y = 0; y < BB_H; ++y) {
        int v = row_v[y];
        if (y >= horizon_row) {
            v += row_wave[y] * WAVE_TEX_AMP / 127;
            v = v < SKY_ROWS ? SKY_ROWS : (v > TEX_H - 1 ? TEX_H - 1 : v);
        }
        const uint16_t* src = env_pixels + v * TEX_W;
        uint16_t* out = dst + y * BB_W;
        for (int x = 0; x < BB_W; ++x) out[x] = src[((yaw_u + col_u[x]) >> 8) & (TEX_W - 1)];
    }
}

// ---------------------------------------------------------------------------
// 3D

const g3::Material MAT_CUBE = {
    {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, &env_tex, g3::BlendMode::NONE,
    0, 0, 0, g3::MaterialFlags::TEXTURE | g3::MaterialFlags::ENV_MAP,
};
// 上下反転すると三角形の向きが裏返るので、両面を描く (奥の面は深度で隠れる)
const g3::Material MAT_CUBE_MIRROR = {
    {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, &env_tex, g3::BlendMode::NONE,
    0, 0, 0,
    g3::MaterialFlags::TEXTURE | g3::MaterialFlags::ENV_MAP | g3::MaterialFlags::DOUBLE_SIDED,
};

alignas(8) uint8_t arena[20 * 1024];
g3::Graphics3D g3d;
g3::Stats last_stats = {};

void build_scene(float t, float yaw, bool mirror) {
    g3d.setPerspectiveProjection(FOV_Y, ASPECT, 0.3f, 50.0f);
    g3d.beginScene();
    g3d.loadIdentity();
    // 光源の向きは、カメラの変換を掛ける前に与えて視点座標系で固定する
    g3d.enableParallelLight({LIGHT_X, mirror ? LIGHT_Y : -LIGHT_Y, LIGHT_Z}, LIGHT_COL);
    g3d.enableEnvironmentLight(AMBIENT_COL);

    g3d.translate(0, 0, -DIST);
    g3d.rotate(PITCH, 1, 0, 0);
    g3d.rotate(yaw, 0, 1, 0);

    if (mirror) g3d.scale(1, -1, 1);  // 水面 (y = 0) で鏡映
    g3d.translate(0, CUBE_H + 0.08f * sinf(t * 1.3f), 0);
    g3d.rotate(t * 0.7f, 0.3f, 1.0f, 0.2f);
    g3d.rotate(0.6f + 0.25f * sinf(t * 0.45f), 1.0f, 0.0f, 0.4f);
    g3d.setMaterial(mirror ? MAT_CUBE_MIRROR : MAT_CUBE);
    g3d.putCube({0, 0, 0}, {CUBE_SIZE, CUBE_SIZE, CUBE_SIZE});
    g3d.endScene();
}

}  // namespace

void init(ClockFn clock) {
    if (clock) now_us = clock;
    generate_texture();
    init_tables();
    g3d.init(BB_W, BB_H, arena, sizeof(arena));
    g3d.disableClear();  // 背景の上に重ねて描く
}

void render(uint16_t* dst, float t, Timing& tm) {
    const g2::Surface surf = g2::makeSurface(g2::PixelFormat::RGB565_SWAPPED, BB_W, BB_H, dst);
    const float yaw = t * YAW_SPEED;

    const uint32_t t0 = now_us();
    update_waves(t);
    draw_background(dst, yaw);  // 1. 空 (上半球) と 2. 水面 (下半球)
    const uint32_t t1 = now_us();

    // 3. 水面下のキューブの像: 1 ラインずつ、参照する行を波の高さだけずらして描く
    build_scene(t, yaw, true);
    g3d.beginRender();
    for (int y = horizon_row; y < BB_H; ++y) {
        const int src_y = y + row_wave[y] * WAVE_SCREEN_AMP / 127;
        if (src_y < 0 || src_y >= BB_H) continue;
        g3d.render(0, (int16_t)src_y, BB_W, 1, surf, 0, (int16_t)y);
    }
    g3d.endRender();
    const uint32_t t2 = now_us();

    // 4. 水面上のキューブ
    build_scene(t, yaw, false);
    g3d.beginRender();
    g3d.render(0, 0, BB_W, BB_H, surf);
    g3d.endRender();
    last_stats = g3d.getStats();
    const uint32_t t3 = now_us();

    tm.background = t1 - t0;
    tm.reflection = t2 - t1;
    tm.cube = t3 - t2;
    tm.total = t3 - t0;
}

void print_stats() {
    const g3::Stats s = last_stats;
    printf("gfx3d: tri=%d dropped=%d spanPeak=%d/%d spanDropped=%d arena=%u/%u\n", s.triCount,
           s.triDropped, s.spanPeak, s.spanCapacity, s.spanDropped, (unsigned)s.arenaUsed,
           (unsigned)s.arenaSize);
}

}  // namespace scene
