# PDM-FRC サンプル (RP2040 / RP2350)

[English](README.md)

Raspberry Pi Pico (RP2040) または Pico 2 (RP2350) で液晶を駆動するサンプルです。アプリケーション側のコードは [`examples/core/`](../core) にあります。

ディスプレイはビルド時に選びます ([ビルド](#ビルド) を参照)。

| ディスプレイ | `PDMFRC_DISPLAY` | 備考 |
|---|---|---|
| TG12864B (128x64、KS0108 互換) | `tg12864b` (既定) | |
| SG12864ASLB (128x64、KS0108 互換) | `sg12864aslb` | 接続・ドライバは TG12864B と同じ。73 fps |
| AQM1248A (128x48、ST7565R) | `aqm1248a` | gfx3d は 48x128 に切り取り |
| Nokia 5110 (84x48、PCD8544) | `nokia5110` | 接続は AQM1248A と同じ。79 fps。gfx3d は 48x84 に切り取り |

| ディレクトリ | 内容 | システムクロック | ディスプレイ |
|---|---|---|---|
| [gradient](gradient) | 階調値 0..255 のグラデーション (パレット作成用) | 125 MHz | すべて |
| [image](image) | パレット LUT を通した静止画 (`assets/images/<display>.png`) | 125 MHz | すべて |
| [gfx3d](gfx3d) | 波打つ水面に浮かぶ金属キューブのリアルタイム 3D ([ShapoGFX](https://github.com/shapoco/shapo-gfx) が必要) | 250 MHz | すべて |

LCD への書き込みは PIO + DMA で行います。`Pdm::pop()` が出力したビットマップを、毎フレーム一定のレートで送ります。TG12864B では 103 fps、SG12864ASLB では 73 fps です。

## 接続 (TG12864B、SG12864ASLB)

![](../../image/wiring_rp2_tg12864b.png)

| Pico | TG12864B | 備考 |
|---|---|---|
| GPIO2 | D/I (RS) | |
| GPIO3 | R/W | PIO が駆動するのは Busy 待ちモードのときだけです。固定サイクルモードでは Low 固定です。 |
| GPIO4 | E | |
| GPIO5 | DB0 | |
| GPIO6 | DB1 | |
| GPIO7 | DB2 | |
| GPIO8 | DB3 | |
| GPIO9 | DB4 | |
| GPIO10 | DB5 | |
| GPIO11 | DB6 | |
| GPIO12 | DB7 | Busy フラグの読み取りにも使います。 |
| GPIO13 | CS1 | 左半分 (x = 0..63)、アクティブ High |
| GPIO14 | CS2 | 右半分 (x = 64..127)、アクティブ High |
| GPIO15 | /RST | アクティブ Low |
| GND | VSS | |

- PIO は GPIO2..14 を連続した 1 ブロックとして使います。そのため、ピンを 1 本ずつ入れ替えることはできません。変更する場合は、[common/tg12864b.cpp](common/tg12864b.cpp) の `PIN_*` と [common/tg12864b.hpp](common/tg12864b.hpp) のビット配置 (`BIT_*`) を合わせて書き換えてください。
- VDD、コントラスト電圧 (VO / VEE)、バックライトは、LCD モジュールのデータシートに従って供給してください。
- モジュールのロジック電圧を確認してください。既定の Busy 待ちモードでは、ステータス読み出しの間 LCD が DB0..DB7 を駆動します。5 V で動くモジュールの場合は、レベルシフタを入れてください。または、固定サイクルモード (`w` キー) に切り替え、R/W を GND に固定してください。

## 接続 (AQM1248A)

| Pico | AQM1248A | 備考 |
|---|---|---|
| GPIO16 | RST | アクティブ Low |
| GPIO17 | CS | アクティブ Low |
| GPIO18 | SCK | |
| GPIO19 | SDI | |
| GPIO20 | RS | |
| 3V3 (OUT) | VDD | 標準 3.0 V、最大 3.6 V |
| GND | GND | |

- 液晶は内蔵発振 (データシート上 約 128..179 Hz) で走査しており、調整も同期もできません。ノイズが少なくなるフレームレートを目視で探してください (`[` / `]`、`{` / `}`)。
- 暗い画素が多い列 (短辺方向の 48 px) は、列全体が暗く見えます (ゴースト)。列方向のアンチゴーストでこれを打ち消します。初期値は強さ 107、ガンマ 0.45 です (パレットはこの設定で作成)。調整する場合は、gradient サンプルの調整用パターン (`t`) を表示し、黒い四角の下の領域がほかの部分と同じに見えるまで強さ (`a` / `A`) とガンマ (`e` / `E`) を変えてください。背景の階調値 (`l` / `L`) の両端では差が出にくいので、強さは背景 40..64 付近で、ガンマは 128 付近で合わせ、これを繰り返します。パレットはこれらの値に依存します。

## 接続 (Nokia 5110)

| Pico | Nokia 5110 | 備考 |
|---|---|---|
| GPIO16 | RST | アクティブ Low |
| GPIO17 | CE | アクティブ Low |
| GPIO18 | CLK | 最大 4 MHz |
| GPIO19 | DIN | |
| GPIO20 | DC | |
| 3V3 (OUT) | VCC | |
| GND | GND | |
| (未接続) | LIGHT | バックライト。モジュールにより異なる (不要) |

- 液晶は内蔵発振で走査しており、同期できません。79 fps が最も良く見えました。必要ならノイズが少なくなるフレームレートを目視で探してください (`[` / `]`、`{` / `}`)。
- コントラスト (Vop、`k` / `K`) はモジュールごとに最適値が異なります。パレットは Vop 64、バイアス (`x` / `X`) 1/48 で作成しました。

## ビルド済みバイナリ

ビルド済みバイナリは [Releases](https://github.com/shapoco/pdmfrc/releases) ページからダウンロードできます。zip ファイルの中の `rp2/<display>/<サンプル名>/<board>/<サンプル名>.uf2` が書き込むファイルです (`pico`: RP2040、`pico2`: RP2350)。BOOTSEL ボタンを押しながら Pico を接続し、現れたドライブに `.uf2` をコピーしてください。

## ビルド

必要なもの: [Pico SDK](https://github.com/raspberrypi/pico-sdk) (RP2350 には 2.x が必要)、CMake、Ninja、Arm GNU ツールチェイン

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
export SHAPOGFX_PATH=/path/to/shapo-gfx   # gfx3d のみ (v1.12.0 以降)

examples/rp2/image/build.sh            # Pico (RP2040)
examples/rp2/image/build.sh pico2      # Pico 2 (RP2350)
examples/rp2/image/build.sh pico sg12864aslb   # SG12864ASLB
examples/rp2/gradient/build.sh pico aqm1248a   # AQM1248A
```

`build.sh [board] [display] [options...]` は次のコマンドを実行します。ディスプレイの既定は `tg12864b` です。それより後の引数は CMake に渡されます。

```sh
cmake -S . -B build/<board>-<display> -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=<board> -DPDMFRC_DISPLAY=<display> [options...]
ninja -C build/<board>-<display>
```

各サンプルのディレクトリに `build/<board>-<display>/<サンプル名>.uf2` ができます。BOOTSEL ボタンを押しながら Pico を接続し、現れたドライブに `.uf2` をコピーしてください。

システムクロックは次のように変更できます。133 MHz を超える場合は、コア電圧を 1.20 V に上げます。

```sh
examples/rp2/gfx3d/build.sh pico2 -DSYS_CLOCK_KHZ=200000
```

## シリアルコマンド

サンプルは USB CDC (USB 経由の stdio) で通信します。端末ソフトでシリアルポートを開き、`h` を押すとヘルプが表示されます。

| キー | 動作 |
|---|---|
| `f` / `F` | フレームレートのプリセットを下げる / 上げる |
| `[` / `]` | フレームレート -1 / +1 fps |
| `{` / `}` | フレームレート -0.1 / +0.1 fps |
| `a` / `A` | アンチゴーストの強さ -1 / +1 (アンチゴーストを有効にした表示器のみ。AQM1248A) |
| `e` / `E` | アンチゴーストのガンマ -0.05 / +0.05 (同上) |
| `r` | 位相モードを切り替えて、位相をリセットする |
| `R` | 位相をリセットする |
| `s` | ステータスを表示する (統計もリセットする) |
| `h` / `?` | ヘルプ |

ディスプレイ固有のキー:

| ディスプレイ | キー | 動作 |
|---|---|---|
| TG12864B、SG12864ASLB | `b` / `B` | バスサイクルを短く / 長くする |
| TG12864B、SG12864ASLB | `w` | Busy 待ち / 固定サイクルを切り替える |
| AQM1248A | `c` / `C` | SCLK を下げる / 上げる (1..20 MHz) |
| AQM1248A | `k` / `K` | コントラスト (電子ボリューム) -1 / +1 |
| AQM1248A | `x` / `X` | V0 レギュレータ抵抗比 -1 / +1 |
| AQM1248A | `u` | 180° 回転 |
| Nokia 5110 | `c` / `C` | SCLK を下げる / 上げる (0.5..4 MHz) |
| Nokia 5110 | `k` / `K` | Vop (コントラスト) -1 / +1 |
| Nokia 5110 | `x` / `X` | バイアス -1 / +1 |

サンプル固有のキー:

| サンプル | キー | 動作 |
|---|---|---|
| gradient | `t` | グラデーション / アンチゴースト調整用パターンを切り替える |
| gradient | `l` / `L` | 調整用パターンの背景の階調値 -4 / +4 |
| image | `g` | LUT 変換 / グレー変換を切り替える |
| gfx3d | `y` | フレーム同期のオン / オフ |
| gfx3d | `o` | 回転 (0 / ccw90 / 180 / cw90) |
| gfx3d | `z` | 縮小 1/2 / 等倍を切り替える |
| gfx3d | `p` | アニメーションを一時停止する |
