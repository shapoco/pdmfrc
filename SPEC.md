# PDM-FRC 仕様

Temporal Dithering Using Pulse Density Modulation.

モノクロ液晶の画素を 1 次 ΔΣ 変調 (PDM) で高速に点滅させ、液晶の応答遅れ (残像) によって
中間調・擬似カラーを得るためのライブラリ。入力画像 (グレースケール / RGB565) を画素ごとの
階調値に変換して保持し、毎フレーム 1bit/画素 のビットマップを生成する。
LCD への転送はライブラリの範囲外 (アプリ側の責任)。

## 1. 用語

| 用語 | 意味 |
|---|---|
| 階調値 (level) | 0..255。画素が ON になるフレームの割合 (ON デューティ)。0 = 常時 OFF, 255 = 常時 ON |
| パレット | 階調値 → 実機での見かけの色 (RGB888) の表。256 色。ディスプレイごとに実測して作る |
| LUT | 入力画素値 → 階調値 の表。パレットから生成する |
| デューティ面 | 画素ごとの階調値 (push で書き込む) |
| 状態面 | 画素ごとの ΔΣ アキュムレータ (pop で進む) |
| ON | LCD のドットが駆動された状態。出力ビット 1 (`CFG_FLAG_INVERT` 指定時は 0) |

## 2. ディレクトリ構成

```
pdmfrc/
├── include/pdmfrc/
│   ├── pdmfrc.hpp               # 公開ヘッダ (これだけインクルードすればよい)
│   ├── version.hpp
│   ├── palettes/<display>.hpp   # assets/palettes/<display>.png から生成したパレット
│   └── configs/<display>.hpp    # ディスプレイの推奨設定 (LcdModule。tg12864b, sg12864aslb, aqm1248a, nokia5110)
├── src/                        # ライブラリ実装 (アーキテクチャ非依存)
├── cmake/esp_idf.cmake         # ESP-IDF コンポーネントとしての登録
├── test/                       # ホスト用テスト (run.sh)
├── assets/
│   ├── palettes/<display>.png   # 256x1 px パレット画像 (<display>_raw.png は palfix.py で補正する前の撮影結果)
│   └── images/<display>.png     # image サンプル用の画像 (画面サイズ)
├── tools/                      # Python スクリプト (要 Python3 + Pillow + NumPy)
├── examples/
│   ├── core/                   # サンプルのアーキテクチャ非依存部分
│   │   ├── gradient/
│   │   ├── image/
│   │   └── gfx3d/              # ShapoGFX に依存 (このディレクトリのみ)
│   └── rp2/                    # RP2040 / RP2350 向けビルド環境
│       ├── common/             # ディスプレイドライバ・HAL
│       ├── gradient/
│       ├── image/
│       └── gfx3d/
├── SPEC.md / README.md / README.ja.md / LICENSE / .clang-format
├── CMakeLists.txt / library.json / idf_component.yml / Kconfig
└── .gitignore
```

## 3. ライブラリ

### 3.1 言語・コーディング規約

- C++11 以上でビルドできること (AVR 等の古いツールチェーンを考慮)。標準ライブラリは `<stdint.h>` `<stddef.h>` `<string.h>` `<math.h>` のみ使う。
- 動的メモリ確保・例外・RTTI を使わない。作業領域はすべてユーザーが渡す arena から取る。
- 命名・書式は ShapoGFX に準拠する。
  - 型・クラス: `UpperCamelCase`
  - 関数・メソッド・変数・メンバ: `lowerCamelCase`
  - 定数・enum 値・マクロ: 大文字スネーク (`CFG_FLAG_PACK_V8`, `PixelFormat::RGB565`)
  - インデントは半角スペース 2 個。`.clang-format` を同梱する。
- 名前空間 `pdmfrc`。

### 3.2 コンパイル時設定

マクロで指定する。CMake ではキャッシュ変数 (同名)、ESP-IDF では Kconfig (`CONFIG_PDMFRC_*`) で指定し、
`CMakeLists.txt` / `cmake/esp_idf.cmake` がマクロに変換する。`PDMFRC_POS_8BIT` 以外はライブラリ内部 (`src/`) でのみ参照する。

| マクロ | 既定 | 意味 |
|---|---|---|
| `PDMFRC_POS_8BIT` | 未定義 | 定義すると座標型 `pos_t` を `uint8_t` にする (画面は最大 255x255) |
| `PDMFRC_LUT565_R_BITS` | 4 | RGB565 LUT のインデックスに使う R の上位ビット数 (1..5) |
| `PDMFRC_LUT565_G_BITS` | 5 | 同 G (1..6) |
| `PDMFRC_LUT565_B_BITS` | 4 | 同 B (1..5) |
| `PDMFRC_FORMAT_GRAY8` | 1 | 入力形式 GRAY8 を組み込む (0 で除外) |
| `PDMFRC_FORMAT_RGB565` | 1 | 同 RGB565 |
| `PDMFRC_FORMAT_RGB565_SWAPPED` | 1 | 同 RGB565_SWAPPED |
| `PDMFRC_HOT_ATTR` | 空 | push / pop の内部ループに付ける属性。RP2 では RAM 配置のセクション (`.time_critical.*`) を指定する。ESP-IDF では Kconfig で IRAM 配置を選べる |

- 内部ループの関数は、属性がインライン展開で失われないよう `noinline` にしている。
- push の内部ループは (入力形式 × 2 倍 × LUT) の組み合わせごとに 1 関数あり、RGB565 系 1 形式で約 6KB (RP2040)。
  内部ループを RAM に置く場合は、使わない形式を `PDMFRC_FORMAT_*` で除外すると RAM を節約できる。

> **`PDMFRC_STATE_8BIT` について**: 検討の結果、不要と判断した。
> 現行実装はデューティ面を 16bit (0..256) で持っていたが、これは PWM モードとガンマ補正のためだった。
> PDM のみ・ガンマなしでは、階調値 `l` (8bit) をそのまま保持し、pop 時に `l + (l >> 7)` で 0..256 に
> 伸ばせば現行の `round(l * 256 / 255)` と完全に一致する。よって既定で 1 画素 2 バイト
> (デューティ 8bit + アキュムレータ 8bit) となり、128x64 で 16KB。

### 3.3 型・定数

```cpp
namespace pdmfrc {

#ifdef PDMFRC_POS_8BIT
using pos_t = uint8_t;
#else
using pos_t = uint16_t;
#endif

enum class PixelFormat : uint8_t {
  GRAY8,           // 1 バイト/画素
  RGB565,          // uint16_t (ネイティブバイト順)
  RGB565_SWAPPED,  // uint16_t をバイトスワップしたもの (メモリ上で RRRRRGGG, GGGBBBBB の順)
};

enum class Phase : uint8_t {
  ZERO,    // 全画素 0
  RANDOM,  // xorshift32 による乱数
  BAYER,   // 8x8 Bayer 行列による規則的なずらし
};

// Config のフラグ
constexpr uint8_t CFG_FLAG_PACK_V8 = 0x01;  // 縦 8px を 1 バイトにパック (未指定時は横 8px)
constexpr uint8_t CFG_FLAG_LSB1ST = 0x02;   // パックしたバイトの LSB を左 (横) / 上 (縦) にする (未指定時は MSB)
constexpr uint8_t CFG_FLAG_INVERT = 0x04;   // 出力ビットを反転する (ON = 0)
constexpr uint8_t CFG_FLAG_USE_LUT = 0x08;  // arena に LUT の領域を確保する (initLut() に必要)
constexpr uint8_t CFG_FLAG_ANTIGHOST_ROW = 0x10;  // 行方向のアンチゴースト (3.14)
constexpr uint8_t CFG_FLAG_ANTIGHOST_COL = 0x20;  // 列方向のアンチゴースト (3.14)。ROW と同時には指定できない

// push() のフラグ
constexpr uint8_t PUSH_FLAG_X_FLIP = 0x01;   // X 座標の走査方向を反転
constexpr uint8_t PUSH_FLAG_Y_FLIP = 0x02;   // Y 座標の走査方向を反転
constexpr uint8_t PUSH_FLAG_SWAP_XY = 0x04;  // X 座標と Y 座標を交換
constexpr uint8_t PUSH_FLAG_DOUBLE = 0x08;   // 2 倍オーバーサンプリング (入力 2x2 画素の平均を 1 画素とする)
constexpr uint8_t PUSH_FLAG_USE_LUT = 0x10;  // LUT で階調値に変換 (未指定時はグレースケール値をそのまま階調値とする)

struct Config {
  pos_t width;              // 画面 (状態面) の幅
  pos_t height;             // 画面 (状態面) の高さ
  PixelFormat inputFormat;  // push() に渡す画像の形式
  uint8_t flags;            // CFG_FLAG_*
};

// デバイスごとの推奨設定
struct LcdModule {
  Config config;                // Config の推奨値
  uint32_t fpsQ8;               // 推奨フレームレート (小数部 8bit の固定小数点。TG12864B では 103 << 8)
  const uint8_t (*palette)[3];  // パレット (256 色, RGB888)
  uint8_t antiGhost;            // アンチゴーストの強さの推奨値 (setAntiGhost())
  uint16_t antiGhostGammaQ8;    // アンチゴーストのガンマの推奨値 (小数部 8bit。256 = 1.0)
};

}  // namespace pdmfrc
```

### 3.4 関数

```cpp
namespace pdmfrc {

// 汎用の Config を作る
void getDefaultConfig(Config* cfg, pos_t w, pos_t h,
                      PixelFormat fmt = PixelFormat::RGB565_SWAPPED, uint8_t flags = 0);

// cfg に必要な arena のバイト数
size_t calcRequiredArenaSize(const Config& cfg);

// 入力形式 fmt の LUT のバイト数 (GRAY8: 256, RGB565 系: 2^(R+G+B bits) = 既定 8192)
size_t calcLutSize(PixelFormat fmt);

// 入力形式 fmt の 1 画素のバイト数
size_t bytesPerPixel(PixelFormat fmt);

class Pdm {
 public:
  // arena を割り付け、デューティ面を 0、状態面を Phase::RANDOM (seed 1) で初期化する。
  // arena は 4 バイト境界。Pdm の生存中はユーザーが保持する。
  bool init(const Config& cfg, uint8_t* arena, size_t arenaSize);
  const Config& getConfig() const;
  bool isInitialized() const;

  // パレット (256 色, RGB888) から inputFormat 用の LUT を arena 内に生成する (CFG_FLAG_USE_LUT が必要)
  bool initLut(const uint8_t (*palette)[3]);
  // 生成済みの LUT (tools/mklut.py の出力など、calcLutSize() バイト) を使う。
  // lut はコピーせず参照するので、フラッシュ上の配列を渡せる。CFG_FLAG_USE_LUT は不要
  bool setLut(const uint8_t* lut);
  const uint8_t* getLut() const;  // 使用中の LUT (未設定なら nullptr)

  // 入力画像を階調値に変換してデューティ面の矩形 (dx, dy, dw, dh) に書き込む
  bool push(const void* image, size_t stride, pos_t dx, pos_t dy, pos_t dw, pos_t dh,
            uint8_t flags = 0);
  bool push(const void* image, uint8_t flags = 0);  // 画面全体

  // デューティ面の矩形を階調値 level で埋める (余白の塗りつぶし用)
  bool fill(uint8_t level, pos_t dx, pos_t dy, pos_t dw, pos_t dh);
  bool fill(uint8_t level);  // 画面全体

  // アンチゴーストの強さとガンマ (3.14)。init() 直後は強さ 0 (無効)、ガンマ 1.0。CFG_FLAG_ANTIGHOST_* が必要
  bool setAntiGhost(uint8_t strength, uint16_t gammaQ8 = 256);
  uint8_t getAntiGhost() const;
  uint16_t getAntiGhostGamma() const;

  // 状態面を初期化する
  void resetPhase(Phase phase, uint32_t seed = 1);

  // 状態面の矩形を 1 フレーム進め、出力ビットをパックして dest に書く
  bool pop(uint8_t* dest, size_t stride, pos_t sx, pos_t sy, pos_t sw, pos_t sh,
           uint8_t flags = 0);
  bool pop(uint8_t* dest, uint8_t flags = 0);  // 画面全体
};

}  // namespace pdmfrc
```

各関数は引数が不正なとき何もせず `false` を返す。

| 関数 | `false` になる条件 |
|---|---|
| `init` | 幅・高さが 0、パック方向の辺が 8 の倍数でない、arena 不足、arena が 4 バイト境界でない、入力形式が組み込まれていない、`CFG_FLAG_ANTIGHOST_ROW` と `_COL` の両方を指定 |
| `initLut` | `CFG_FLAG_USE_LUT` なし、palette が null |
| `push` | 未初期化、矩形が画面外にはみ出す、image が null、`PUSH_FLAG_USE_LUT` 指定時に LUT 未設定 |

幅または高さが 0 の矩形への push / fill は何もせず `true` を返す。
| `fill` | 未初期化、矩形が画面外にはみ出す |
| `pop` | 未初期化、矩形が画面外にはみ出す、パック方向の座標・サイズが 8 の倍数でない、flags ≠ 0 |
| `setAntiGhost` | 未初期化、`CFG_FLAG_ANTIGHOST_*` なし、`gammaQ8` = 0 |

### 3.5 arena の内訳

| 領域 | バイト数 |
|---|---|
| デューティ面 | `width * height` |
| 状態面 | `width * height` |
| LUT (`CFG_FLAG_USE_LUT` 時) | `calcLutSize(inputFormat)` |
| アンチゴーストの曲線と補正量 (`CFG_FLAG_ANTIGHOST_*` 時) | 256 + (列方向は `width`、行方向は `height` を 4 の倍数に切り上げた値) |

面の並び (どちらの面も同じ):

- `CFG_FLAG_PACK_V8`: `[y / 8][x][y % 8]`。縦 8px が連続し、pop で 1 バイト分を連続アクセスで作れる。`height` は 8 の倍数。
- 未指定 (横パック): `[y][x]` (行優先)。`width` は 8 の倍数。

### 3.6 push の座標対応

出力矩形内の座標 `(x, y)` (`0 ≤ x < dw`, `0 ≤ y < dh`) に対して、入力上の座標 `(u, v)` を次の順に求める。

1. 反転: `x' = X_FLIP ? dw-1-x : x`, `y' = Y_FLIP ? dh-1-y : y`
2. 交換: `(u, v) = SWAP_XY ? (y', x') : (x', y')`
3. 2 倍: `DOUBLE` なら入力の `(2u, 2v)` 〜 `(2u+1, 2v+1)` の 2x2 画素の平均、そうでなければ `(u, v)` の画素

- 入力矩形の大きさは `(SWAP_XY ? dh : dw, SWAP_XY ? dw : dh)` で、`DOUBLE` なら縦横 2 倍。
- `image` は入力矩形の左上画素を指す。`stride` は 1 行のバイト数。0 なら入力矩形の幅 × 画素のバイト数。
- 全体版 `push(image, flags)` は `(0, 0, width, height)` への push で、`stride = 0`。

表示器の向きとフラグの対応 (表示器を回して設置したとき、画像が正立して見えるように送る):

| 表示器の向き | フラグ |
|---|---|
| そのまま | なし |
| 反時計回りに 90° | `SWAP_XY \| X_FLIP` |
| 180° | `X_FLIP \| Y_FLIP` |
| 時計回りに 90° | `SWAP_XY \| Y_FLIP` |

画像を中央に置くなど、画面の一部だけを push する場合は、残りの領域を `fill()` で埋めるのはユーザーの責任。
全画素がフレームごとに更新される必要はない (更新しなかった画素は前の階調値を保つ)。

### 3.7 画素値 → 階調値

| 入力形式 | `PUSH_FLAG_USE_LUT` なし | あり |
|---|---|---|
| GRAY8 | 画素値をそのまま | `lut[画素値]` |
| RGB565 系 | グレースケール値 | `lut[R 上位 rb ビット : G 上位 gb ビット : B 上位 bb ビット]` |

- RGB565 のグレースケール値: 各成分を 8bit に伸ばし (`r8 = r5 << 3 | r5 >> 2` 等)、`(77 r8 + 150 g8 + 29 b8 + 128) >> 8`。
- `DOUBLE` の平均は変換前に行う。GRAY8 は `(a + b + c + d + 2) >> 2`、RGB565 は成分ごとに同様に四捨五入する。

### 3.8 LUT の生成 (`initLut`)

- パレット 256 色と入力色を OKLab (固定小数点, スケール 8192) に変換する。
- RGB565 系: LUT の各セル (RGB565 の上位ビットで区切られた範囲) の中心色に、OKLab 距離が最も近いパレットの階調値を割り当てる。
  距離が同じなら小さい階調値を選ぶ。
- GRAY8: 入力 `g` を `(g, g, g)` とみなし、**OKLab の L (明度) のみ**が最も近い階調値を割り当てる
  (色の付いたパレットでも、明るさの順に対応させるため)。
- 浮動小数点 (`powf`, `cbrtf`) を使うため、FPU のない 8bit マイコンでは非常に遅い。その場合は `tools/mklut.py` で
  生成した LUT を `setLut()` で使う。
- 作業領域としてパレットの OKLab 値 (int16 × 3 × 256 = 1.5KB) をスタックに置く。
- `tools/mklut.py` (倍精度浮動小数点) とは、入力から等距離に近い 2 色の選び方だけが異なる
  (TG12864B のパレットで RGB565 LUT の約 1 割、距離の差は最大 0.00013)。

### 3.9 PDM (pop)

画素ごとに、デューティ面の階調値 `l` と状態面のアキュムレータ `a` (8bit) から、

```
s = a + l + (l >> 7)   // l を 0..256 に伸ばす (255 で常時 ON)
out = s >> 8
a = s & 0xFF
```

`out` を `CFG_FLAG_INVERT` に応じて反転し、指定の方向・ビット順にパックする。

- 出力バイトの並び:
  - 縦パック: バイト `(x, y/8)` が `dest[(y - sy)/8 * stride + (x - sx)]`。`stride = 0` なら `sw`。
  - 横パック: バイト `(x/8, y)` が `dest[(y - sy) * stride + (x - sx)/8]`。`stride = 0` なら `sw / 8`。
- **pop は状態を進める**。矩形に分けて呼ぶ場合でも、各画素の pop はフレームごとにちょうど 1 回にすること。
- **pop は一定周期で呼ぶこと**。周期が揺らぐと、各画素の点灯時間が揺らいで砂嵐状のノイズになる。
  重い push を pop の直前に挟まない、などはアプリ側で配慮する。

### 3.10 初期位相 (`resetPhase`)

同じ階調の画素が同時に点滅すると面全体のちらつきになるため、アキュムレータの初期値をばらつかせる。

- `ZERO`: 0
- `RANDOM`: xorshift32 (seed 0 は 1 とみなす) の上位 8bit を、y → x の順に割り当てる
- `BAYER`: `BAYER8[y % 8][x % 8] * 4 + 2`

### 3.11 スレッド安全性

- push / fill と pop は別のコア・スレッドから同時に呼んでよい。デューティ面の読み書きはバイト単位なので、
  途中の値を読んでもその画素が新旧どちらかの値になるだけ (ティアリングは許容)。
- `init`, `initLut`, `setLut`, `resetPhase` は pop と同時に呼ばないこと。
- `setAntiGhost` は pop と同時に呼んでよい (補正量の更新もバイト単位)。曲線の作り直しには float の `powf` を
  256 回使う (FPU のないマイコンでは数 ms かかることがある)。push / fill と同時に呼ぶと、
  次にその行・列を push / fill するまで、一部の補正量が古い強さのまま残ることがある。
- 同じ Pdm に対して push 同士、pop 同士を同時に呼ばないこと。

### 3.12 性能

- push の内部は (入力形式 × DOUBLE × LUT の有無) ごとにテンプレートで展開し、関数の入口で 1 回だけ分岐する。
- 縦パックで `dy`, `dh` が 8 の倍数のときは、縦 8px の列単位で処理する高速経路を通る (現行 lcd_convert 相当)。
- 内部ループには `PDMFRC_HOT_ATTR` を付ける。
- RP2040 (250MHz) で現行実装と同等以上の速度であることを、サンプルの計測値で確認する。

### 3.13 制限

- AVR などハーバードアーキテクチャの `const` 配列は RAM に置かれる。パレット (768 バイト) や
  生成済み LUT をフラッシュに置く `PROGMEM` 対応は今回は行わない。
- 状態は 1 画素 2 バイトのため、128x64 で 16KB の RAM が必要。

### 3.14 アンチゴースト

パッシブマトリクス液晶では、ON の画素が多いライン (行または列) 全体が濃く見えることがある (ゴースト)。
これを打ち消すため、ラインの階調値の合計に比例する量 (補正量) を、そのラインの全画素の階調値から差し引いて PDM する。

- 方向は `CFG_FLAG_ANTIGHOST_COL` (列 = 画面の x ごと、長さ `height`) か `CFG_FLAG_ANTIGHOST_ROW`
  (行 = y ごと、長さ `width`) で選ぶ。座標は状態面 (表示器本来の向き) のもので、push の回転とは無関係。
- ライン `i` の補正量: `a_i = (S_i * strength + D / 2) / D`。`S_i` はラインの画素の `f(階調値)` の合計、
  `D = 長さ * 255`。つまり `strength` は「全画素 255 のラインから差し引く量」。
- `f(l) = floor(255 * powf(l / 255, γ) + 0.5)` (float で計算、`γ = gammaQ8 / 256`)。256 要素の表として
  arena に置き、ガンマが変わったときだけ作り直す。γ = 1 のときは表を引かずに階調値をそのまま足す。
  γ < 1 は中間の階調値を重く数える。ゴーストが ON の画素数だけでなく ON / OFF の切り替わり
  (中間の階調値で多い) にも依存する場合に合わせるためのもの。
- pop は 3.9 の `l` の代わりに `max(l - a_i, 0)` を使う。デューティ面の階調値そのものは変えない。
- 階調値は「ON の割合」なので、ポジ / ネガ、`CFG_FLAG_INVERT` によらず同じ式でよい。
- 補正量は arena に保持し、push / fill のたびに、書き換えた矩形が交わるラインだけ計算し直す。
  `setAntiGhost` は全ラインを計算し直す。したがって補正は同じフレームから効き、遅れない。
  (合計は補正前の階調値で計算する。補正で実際のゴーストが減る分は strength の調整で吸収する。)
- 白 (階調値 0) より明るくはできないため、背景が白でも暗くなるほどのゴーストは打ち消せない。
- 性能: 補正なしの pop は従来どおりの経路を通る。補正ありでは、Arm の DSP 拡張 (`__ARM_FEATURE_SIMD32`。
  RP2350 の Cortex-M33 など) があれば `UQSUB8` で 4 画素ずつ飽和減算し、なければ画素ごとに分岐なしで減算する。

## 4. パレット・推奨設定ヘッダ

### `include/pdmfrc/palettes/tg12864b.hpp`

`tools/pal2cpp.py` で `assets/palettes/tg12864b.png` から生成する (生成物もリポジトリに含め、利用者は Python 不要)。

```cpp
namespace pdmfrc { namespace palettes {
static const uint8_t TG12864B[256][3] = { {78, 32, 255}, ... };
}}
```

### `include/pdmfrc/configs/tg12864b.hpp`

```cpp
#include "pdmfrc/palettes/tg12864b.hpp"
namespace pdmfrc { namespace configs {
static const LcdModule TG12864B = {
  {128, 64, PixelFormat::RGB565_SWAPPED, CFG_FLAG_PACK_V8 | CFG_FLAG_LSB1ST | CFG_FLAG_USE_LUT},
  103 << 8,
  palettes::TG12864B,
  0,    // アンチゴーストなし
  256,
};
}}
```

(KS0108 は縦 8px パック・LSB が上・ビット 1 で点灯。)

### `include/pdmfrc/configs/sg12864aslb.hpp`

SG12864ASLB (128x64, KS0108 互換, STN ポジ)。インタフェースは TG12864B と同じだが、ビット 1 で黒くなる
(パレットは階調値 0 が白、255 が黒)。

```cpp
static const LcdModule SG12864ASLB = {
  {128, 64, PixelFormat::RGB565_SWAPPED, CFG_FLAG_PACK_V8 | CFG_FLAG_LSB1ST | CFG_FLAG_USE_LUT},
  73 << 8,
  palettes::SG12864ASLB,
  0,    // アンチゴーストなし
  256,
};
```

- TG12864B のプログラムで試して目視で最適だったフレームレート 73 fps を推奨値とする。
- TG12864B のような色の付いた残像は見えず単調な濃淡になるため、パレットはグレースケール。
- パレットは 73 fps の gradient を撮影した写真を元に作成した (`sg12864aslb_raw.png`)。`palfix.py --normalize` で
  明度を単調にし、両端が白・黒になるように明度方向にノーマライズしたものが `sg12864aslb.png`。
  階調値 60 付近まではほぼ白、200 付近からはほぼ黒。

### `include/pdmfrc/configs/aqm1248a.hpp`

AQM1248A (128x48, ST7565R, FSTN ポジ)。ST7565R は縦 8px パック・LSB が上・ビット 1 で黒。

```cpp
static const LcdModule AQM1248A = {
  {128, 48, PixelFormat::RGB565_SWAPPED,
   CFG_FLAG_PACK_V8 | CFG_FLAG_LSB1ST | CFG_FLAG_USE_LUT | CFG_FLAG_ANTIGHOST_COL},
  73 << 8,
  palettes::AQM1248A,
  107,  // アンチゴーストの強さ
  115,  // アンチゴーストのガンマ 0.45
};
```

- 液晶は内蔵発振で走査し同期できないため、フレームレートは目視で決めた (73 fps。走査周波数の約半分)。
- 暗い画素が多い列 (SEG。短辺方向の 48px) 全体が暗く見えるため、列方向のアンチゴーストを有効にする。
  強さ 107、ガンマ 0.45 は gradient の調整用パターンを目視で合わせて決めた。
- パレットは 73 fps、V0 レギュレータ抵抗比 4、電子ボリューム 22 (V0 約 7.8 V)、上記のアンチゴーストを有効にした
  gradient を撮影した写真を元に作成した (`aqm1248a_raw.png`)。暗い側で明度が戻る 2 か所 (階調値 187..203,
  209..228) を `palfix.py` で補正したものが `aqm1248a.png`。階調値 0..76 は白、77..237 がグラデーション
  (190 付近からはほぼ黒)、238 以上は黒。電子ボリューム等は LcdModule の範囲外なので、RP2 のドライバの初期値としている。

### `include/pdmfrc/configs/nokia5110.hpp`

Nokia 5110 LCD モジュール (84x48, PCD8544, ポジ)。PCD8544 は縦 8px パック・LSB が上・ビット 1 で黒。

```cpp
static const LcdModule NOKIA5110 = {
  {84, 48, PixelFormat::RGB565_SWAPPED, CFG_FLAG_PACK_V8 | CFG_FLAG_LSB1ST | CFG_FLAG_USE_LUT},
  79 << 8,
  palettes::NOKIA5110,
  0,    // アンチゴーストなし
  256,
};
```

- 内蔵発振で走査し同期できないため、フレームレートは目視で決めた (79 fps)。
- パレットはグレースケール。79 fps、Vop 64、バイアス 1/48 の gradient を撮影した写真を元に作成した
  (`nokia5110_raw.png`)。幅 84 px の表示器では 1 列が約 3 階調分になり、ドットと隙間の明暗が周期 3 の縞として
  写るので、`palfix.py --smooth 3 --normalize` で平滑化・単調化・ノーマライズしたものが `nokia5110.png`。

## 5. ツール (`tools/`)

| スクリプト | 機能 |
|---|---|
| `pal2cpp.py` | 256x1 px のパレット画像 → `uint8_t [256][3]` (RGB888) の C++ ヘッダ |
| `palfix.py` | パレット画像の明度 (OKLab L) を単調にする。明度が戻る箇所を、その手前 (戻った先と同じ値の平坦部の直前) から、その値を超えて進む最初の階調値までの RGB の直線で置き換える。向きは両端から自動判定 (`--direction` で指定可)。`--smooth N` で最初に N 階調の移動平均をとる (256 px より狭い表示器の写真では 1 列が 256 / 幅 階調分になり、ドットと隙間が周期的な縞として写る。N にその周期を指定する)。`--normalize` で明度 (OKLab L) を線形に伸ばし、最も暗い階調値が黒・最も明るい階調値が白になるようにする (色相・彩度は維持し、sRGB の範囲に切り詰める。写真は純粋な黒・白にならないため、そのままでは LUT がパレットの両端より暗い / 明るい入力色を飽和させてしまう)。出力を省略すると置き換える範囲の表示のみ |
| `mklut.py` | パレット画像 → 生成済み LUT の C++ ヘッダ (入力形式・ビット数を指定)。`initLut` と同じ算法だが、浮動小数点の差で最近傍がほぼ等距離の色はまれに異なる |
| `img2cpp.py` | PNG → GRAY8 / RGB565 / RGB565_SWAPPED の C++ ヘッダ (現行の `img2cpp.py` と `img2rgb565.py` を統合) |
| `preview.py` | 実機と同じ変換 (RGB565 化 → LUT → パレット色) で見かけの色を予測した PNG を出力 |
| `colorlib.py` | 上記の共通処理 (OKLab 変換など) |

## 6. サンプル

### 6.1 構成

- `examples/core/<name>/`: アーキテクチャ非依存の部分 (画像の生成、3D シーンの描画など)。HAL には依存しない。
- `examples/rp2/common/`: RP2040 / RP2350 共通の HAL。
  - `display.hpp`: ディスプレイの抽象。ディスプレイはビルド時に CMake の `PDMFRC_DISPLAY` で 1 つ選び、
    対応する `display_<driver>.cpp` だけをリンクする (マクロ `PDMFRC_DISPLAY_<NAME>` が定義される。
    SG12864ASLB は TG12864B と同じ KS0108 互換なのでドライバを共用し、`PDMFRC_DISPLAY_SG12864ASLB` で
    名前と推奨設定だけを切り替える)。
    提供するもの: 画面サイズ (`WIDTH`, `HEIGHT`)、推奨設定 (`module()`)、初期化、次フレームの pop (`prepare()`。
    転送バッファは 2 面でドライバが持つ)、転送開始 (`startTransfer()`)、転送時間、固有キー・ステータス・ヘルプ。
  - `tg12864b.*`, `tg12864b_write.pio`, `display_tg12864b.cpp`: TG12864B / SG12864ASLB ドライバ (GPIO 初期化,
    PIO + DMA 転送, Busy 待ち / 固定サイクル)。`fillStream()` で pop の出力 (縦パック, LSB が上) を転送ストリームに詰める。
  - `aqm1248a.*`, `aqm1248a.pio`, `display_aqm1248a.cpp`: AQM1248A (128x48, ST7565R, FSTN ポジ) ドライバ
    (実験的。4 線 SPI を PIO + DMA で転送。転送ストリームは 1 バイト 1 エントリで RS ビットを含み、コマンドと
    データを 1 回の DMA で送る)。液晶は内蔵発振 (データシート上 約 128..179 Hz) で走査し、同期できない。
    電子ボリュームと V0 レギュレータ抵抗比の初期値は、パレットを作ったときの値 (22, 4)。
  - `pcd8544.*`, `display_nokia5110.cpp`: Nokia 5110 (84x48, PCD8544, ポジ) ドライバ。AQM1248A と同じピン配置・
    同じ PIO プログラム (`aqm1248a.pio`) を使う。水平アドレッシングで X が自動的に次ページへ折り返すため、
    転送ストリームは [Set Y 0, Set X 0, データ 504 バイト] の 1 本。SCLK は最大 4 MHz。Vop (コントラスト) と
    バイアスをキーで調整できる。
  - ディスプレイを追加するには、`display_<name>.cpp` (と必要ならドライバ) を書き、`rp2_common.cmake` の
    `PDMFRC_DISPLAYS` とドライバの対応 (`PDMFRC_DISPLAY_DRIVER`)・ソースの追加、`display.hpp` の画面サイズに登録する。
  - `app.*`: クロック設定 (133MHz 超でコア電圧 1.20V)、フレームループ、USB シリアルの共通操作。
    サンプル固有の処理は `app::Hooks` (キー処理、pop 直後の処理、ステータス、ヘルプ) で差し込む。
  - `rp2_common.cmake`: ライブラリの追加 (内部ループを RAM に配置)、ディスプレイの選択と共通のビルド設定。
    各サンプルは使う入力形式だけを `PDMFRC_FORMAT_*` で組み込む。
    パレットや画面サイズに依存するサンプルは `pdmfrc_rp2_require_display()` で対応するディスプレイを限定する
    (image, gfx3d はすべてのディスプレイ。gfx3d は縦置き 64x128 の画像を中央で切り取り、AQM1248A では
    左右 8px ずつ削った 48x128、Nokia 5110 では 48x84 になる。タイトルは表示の幅と高さに収まるレイアウトを選ぶ)。
- フレームループ (core 0):
  1. 目標時刻 (`基準時刻 + n / FPS`) までシリアル入力をポーリングしながら待つ
  2. 用意しておいたバッファの転送を開始 (`display::startTransfer()`。開始の遅れと間隔をジッタ統計に記録)
  3. もう一方のバッファに次フレームを pop (`display::prepare()`)
  4. `Hooks::afterPop` (サンプルの push など)。待ち時間の先頭で行い、次の開始時刻を乱さない

  目標時刻を 1 フレーム以上過ぎたら遅延として数え、基準時刻を取り直す。FPS の初期値は `LcdModule::fpsQ8`。
- 共通操作:

  | キー | 機能 |
  |---|---|
  | `f` / `F` | FPS プリセットを下げる / 上げる |
  | `[` / `]` | FPS -1 / +1 (1..700) |
  | `{` / `}` | FPS -0.1 / +0.1 (0.1 fps 単位に丸める) |
  | `a` / `A` | アンチゴーストの強さ -1 / +1 (`CFG_FLAG_ANTIGHOST_*` のある表示器のみ。初期値は `LcdModule::antiGhost`) |
  | `e` / `E` | アンチゴーストのガンマ -0.05 / +0.05 (0.20..3.00。同上、初期値は `LcdModule::antiGhostGammaQ8`) |
  | `r` / `R` | 初期位相モード切替 (zero → random → bayer) してリセット / 現在のモードでリセット |
  | `s` | ステータス表示 (統計をリセット) |
  | `h` / `?` | ヘルプ |

- ディスプレイ固有の操作:

  | ディスプレイ | キー | 機能 |
  |---|---|---|
  | TG12864B | `b` / `B` | バスサイクルを短く / 長く (300..20000ns) |
  | TG12864B | `w` | Busy 待ち / 固定サイクル切替 |
  | AQM1248A | `c` / `C` | SCLK を下げる / 上げる (1..20 MHz) |
  | AQM1248A | `k` / `K` | コントラスト (電子ボリューム) -1 / +1 |
  | AQM1248A | `x` / `X` | V0 レギュレータ抵抗比 -1 / +1 |
  | AQM1248A | `u` | 180° 回転 |

- `examples/rp2/<name>/`: `CMakeLists.txt`, `pico_sdk_import.cmake`, `build.sh`, `main.cpp`。
  `build.sh [board] [display]` で `PICO_BOARD` (既定 `pico`、RP2350 は `pico2`) とディスプレイ
  (既定 `tg12864b`。`sg12864aslb` は TG12864B と同じ接続) を選ぶ。ビルドディレクトリは `build/<board>-<display>`。RP2350 は Arm コアのみ対象。
- ハードウェア接続: TG12864B は現行 (GPIO 2..15) と同じ。AQM1248A は RST=GPIO16, CS=GPIO17, SCK=GPIO18,
  SDI=GPIO19, RS=GPIO20。

### 6.2 gradient

- 左から右へ階調値 0 → 255 に変化するグラデーションを、起動時に列ごとの `fill()` でデューティ面に書く
  (大きな画面でも画像バッファが要らない)。Config は選択したディスプレイの推奨値から入力形式を GRAY8 にし、
  `CFG_FLAG_USE_LUT` を外したもの。全ディスプレイに対応する。
- 新しいディスプレイの発色を撮影してパレットを作るために使う。
- `t` でアンチゴースト調整用パターンに切り替える: 全面を背景の階調値 (初期値 64、`l` / `L` で ±4) で塗り、
  上辺中央 (幅の 1/4、高さの 1/2) に階調値 255 の四角を置く。四角の下 (列方向) や左右 (行方向) が
  ほかの部分と同じ濃さに見えるように強さを調整する。
- 操作は共通操作と `t`, `l` / `L` のみ (現行の PWM / 階調数 / ガンマ / 反転 / パターン切替は削除)。
  Config のアンチゴーストのフラグは推奨値のまま残す (パターンで強さを調整できるように)。

### 6.3 image

- `assets/images/<display>.png` (画面サイズの画像) をビルド時に `tools/img2cpp.py` で RGB565_SWAPPED のヘッダ (`image_data.hpp`) に変換し、
  LUT を通して表示する (`examples/core/image/image.cmake`)。
- タイトル画面は削除。ShapoGFX に依存しない。
- 操作: `g` で LUT 変換とグレースケール変換 (`PUSH_FLAG_USE_LUT` なし) を切り替えて見比べる。
  旧版の横グラデーション表示は、入力形式が RGB565 固定で階調値を直接指定できないため削除した (gradient で代替)。

### 6.4 gfx3d

- demo3d と同じ (夕焼けの環境マップ、波打つ水面、水面に映る金属キューブ)。
- core: シーン構築・テクスチャ生成・背景描画・フレーム描画 (128x256 RGB565_SWAPPED のバックバッファへ)。
  時刻取得関数は `scene::init()` で受け取り、背景描画ループの属性は `SCENE_HOT_ATTR` で指定する。
- core: タイトル画面 (`title.cpp`)。縦長 64x128 (表示器の解像度) に、黒地に白文字で中央寄せして描く。
  「PDM-FRC」を ShapoSansP_s12c09a01w02、「Monochrome / LCD」「3D-GFX / Demo」を ShapoSansP_s08c07 で、間に空行を入れる。
- rp2:
  - core1 が描画、core0 が push / pop / LCD 転送。フレーム完了と ACK で同期し、push は pop 直後にのみ行う。
  - 起動時、core0 はタイトルを LUT なし (黒 = 階調値 0、白 = 255) で 1 回 push してからフレームループを始める。
    その間に core1 が LUT を構築し (`initLut`)、シーンを初期化する。3 秒経過し、かつ core1 の初期化が済んだらデモを始める。
  - 表示器は反時計回りに 90° 回して設置 (`SWAP_XY | X_FLIP | DOUBLE`)。`o` で向きを切り替える。
  - 画像は画面中央に置き、はみ出す部分は中央を切り出す。余白は向き・倍率を変えたときだけ `fill(0)` で埋める。
  - 操作: `y` 同期 on/off、`o` 向き、`z` 1/2 縮小 ⇔ 等倍、`p` アニメーション一時停止。
  - システムクロック 250MHz。
- ShapoGFX は環境変数 `SHAPOGFX_PATH` で参照する (未設定ならエラー)。環境マップの修正を含む v1.12.0 以降が必要。

## 7. パッケージング

- `CMakeLists.txt`:
  - ESP-IDF (`ESP_PLATFORM`) では `cmake/esp_idf.cmake` で `idf_component_register()` する。
  - それ以外では静的ライブラリ `pdmfrc` (公開インクルード `include/`, C++11 以上)。
  - キャッシュ変数 `PDMFRC_POS_8BIT` (PUBLIC)、`PDMFRC_LUT565_{R,G,B}_BITS`, `PDMFRC_FORMAT_*`, `PDMFRC_HOT_ATTR` (PRIVATE) をマクロ定義として渡す。
- `library.json` (PlatformIO): 名前 `pdmfrc`、バージョン 1.0.0、MIT、作者 Shapoco、`frameworks: *`, `platforms: *`。
  `examples/rp2` と `tools` はパッケージから除外する。
- `idf_component.yml`: バージョン、説明、MIT、`url: https://github.com/shapoco/pdmfrc`。
- `Kconfig`: `PDMFRC_POS_8BIT`, `PDMFRC_FORMAT_*`, `PDMFRC_HOT_IN_IRAM` (bool), `PDMFRC_LUT565_{R,G,B}_BITS` (int)。
- `.gitignore`: `build/`, `.pio/`, `managed_components/`, `__pycache__/` 等。
- ライセンス: MIT (`LICENSE`, Copyright (c) 2026 Shapoco)。

## 8. テスト

`test/run.sh` でホスト (g++) 向けにビルドして実行する (C++11 / C++17 の両方)。

- push: 全入力形式 × 面の並び・ビット順・反転の全組み合わせで、ランダムな矩形・stride・フラグ・fill を
  SPEC の式どおりの参照実装と比較する (256 フレーム分の ON 回数 = `l + (l >> 7)` で階調値を読み戻す)。
- pop: 4 分割した矩形を任意の順に pop し、参照モデルの ΔΣ と比較する (stride の余白を書き換えないことも確認)。
- アンチゴースト: 行・列方向 × 面の並び・ビット順・反転の全組み合わせ × 複数の強さ・ガンマで、pop を参照モデルと比較する
  (フレームの合間に fill と強さ・ガンマの変更を挟み、補正量と曲線の更新も確認する)。Arm DSP 用の `UQSUB8` 経路は、
  命令をエミュレートしたビルド (`PDMFRC_EMULATE_UQSUB8`) で同じテストを通す。arena サイズと設定の検査も行う。
- 引数の検査 (`false` になる条件)。
- LUT: `initLut` と `tools/mklut.py` の結果を比較し、異なるエントリは入力からの距離がほぼ等しいことを確認する。

ライブラリ化の前の実装 (sg12864_experiment) とは、LUT が全エントリ一致、
回転 (反時計回り 90° / 時計回り 90° / 180°) を含む変換結果のデューティ面が全画素一致することを確認済み。
