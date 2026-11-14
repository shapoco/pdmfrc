# pdmfrc examples for RP2040 / RP2350

[日本語](README.ja.md)

These examples drive a TG12864B (128x64, KS0108 compatible) from a Raspberry Pi
Pico (RP2040) or Pico 2 (RP2350). The application-side code is in
[`examples/core/`](../core).

| Directory | Contents | System clock |
|---|---|---|
| [gradient](gradient) | Gradient of levels 0..255, for making a palette | 125 MHz |
| [image](image) | A still image through the palette LUT | 125 MHz |
| [gfx3d](gfx3d) | Real-time 3D: a metal cube over wavy water (requires [ShapoGFX](https://github.com/shapoco/shapo-gfx)) | 250 MHz |

The LCD is written by PIO + DMA. Every frame, the bitmap from `Pdm::pop()` is
sent at a fixed rate (103 fps for the TG12864B).

## Wiring

| Pico | TG12864B | Notes |
|---|---|---|
| GPIO2 | D/I (RS) | |
| GPIO3 | R/W | Driven by PIO only in busy wait mode; held Low in fixed cycle mode |
| GPIO4 | E | |
| GPIO5 | DB0 | |
| GPIO6 | DB1 | |
| GPIO7 | DB2 | |
| GPIO8 | DB3 | |
| GPIO9 | DB4 | |
| GPIO10 | DB5 | |
| GPIO11 | DB6 | |
| GPIO12 | DB7 | Also read as the busy flag |
| GPIO13 | CS1 | Left half (x = 0..63), active High |
| GPIO14 | CS2 | Right half (x = 64..127), active High |
| GPIO15 | /RST | Active Low |
| GND | VSS | |

- The PIO uses GPIO2..14 as one contiguous block, so these pins cannot be moved
  one by one. To change them, edit `PIN_*` in
  [common/tg12864b.cpp](common/tg12864b.cpp) and the bit layout (`BIT_*`) in
  [common/tg12864b.hpp](common/tg12864b.hpp) together.
- Supply VDD, the contrast voltage (VO / VEE) and the backlight as given in the
  LCD module's datasheet.
- Check the logic level of your module. In busy wait mode (the default), the
  LCD drives DB0..DB7 during status reads. If the module runs at 5 V, use a
  level shifter, or switch to fixed cycle mode (`w` key) and tie R/W to GND.

## Build

Requirements: [Pico SDK](https://github.com/raspberrypi/pico-sdk) (2.x for
RP2350), CMake, Ninja and the Arm GNU toolchain.

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
export SHAPOGFX_PATH=/path/to/shapo-gfx   # gfx3d only (v1.12.0 or later)

examples/rp2/image/build.sh            # Pico (RP2040)
examples/rp2/image/build.sh pico2      # Pico 2 (RP2350)
```

`build.sh` runs the following. Extra arguments after the board are passed to
CMake:

```sh
cmake -S . -B build/<board> -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=<board> [options...]
ninja -C build/<board>
```

The output is `build/<board>/<example>.uf2` in each example's directory. Hold
BOOTSEL while connecting the Pico, then copy the `.uf2` to the drive that
appears.

To change the system clock (above 133 MHz the core voltage is raised to 1.20 V):

```sh
examples/rp2/gfx3d/build.sh pico2 -DSYS_CLOCK_KHZ=200000
```

## Serial commands

The examples talk over USB CDC (stdio over USB). Open the serial port with a
terminal and press `h` for help.

| Key | Action |
|---|---|
| `f` / `F` | Frame rate preset down / up |
| `[` / `]` | Frame rate -1 / +1 fps |
| `r` | Next phase mode, then reset the phase |
| `R` | Reset the phase |
| `b` / `B` | Bus cycle shorter / longer |
| `w` | Busy wait / fixed cycle |
| `s` | Status (and reset the statistics) |
| `h` / `?` | Help |

Keys of each example:

| Example | Key | Action |
|---|---|---|
| image | `g` | LUT / gray conversion |
| gfx3d | `y` | Frame sync on / off |
| gfx3d | `o` | Rotation (0 / ccw90 / 180 / cw90) |
| gfx3d | `z` | Scale 1/2 / 1/1 |
| gfx3d | `p` | Pause the animation |
