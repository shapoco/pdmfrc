// PDM-FRC: Temporal Dithering Using Pulse Density Modulation
// SPDX-License-Identifier: MIT
//
// Blinks the pixels of a monochrome LCD with first-order delta-sigma
// modulation (PDM) so that the slow response of the liquid crystal produces
// intermediate tones and pseudo colors. See SPEC.md.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pdmfrc/version.hpp"

namespace pdmfrc {

#ifdef PDMFRC_POS_8BIT
using pos_t = uint8_t;
#else
using pos_t = uint16_t;
#endif

// Format of the images given to Pdm::push()
enum class PixelFormat : uint8_t {
  GRAY8,           // 1 byte per pixel
  RGB565,          // uint16_t in native byte order
  RGB565_SWAPPED,  // uint16_t byte-swapped (RRRRRGGG, GGGBBBBB in memory)
};

// Initial values of the accumulators (Pdm::resetPhase())
enum class Phase : uint8_t {
  ZERO,    // all zero
  RANDOM,  // xorshift32
  BAYER,   // 8x8 Bayer matrix
};

// Config::flags
constexpr uint8_t CFG_FLAG_PACK_V8 =
    0x01;  // pack 8 vertical pixels into a byte (default: 8 horizontal)
constexpr uint8_t CFG_FLAG_LSB1ST = 0x02;   // the LSB is the left / top pixel (default: the MSB)
constexpr uint8_t CFG_FLAG_INVERT = 0x04;   // output bit 0 for ON
constexpr uint8_t CFG_FLAG_USE_LUT = 0x08;  // reserve a LUT in the arena (needed by Pdm::initLut())

// flags of Pdm::push()
constexpr uint8_t PUSH_FLAG_X_FLIP = 0x01;   // reverse the X scan direction
constexpr uint8_t PUSH_FLAG_Y_FLIP = 0x02;   // reverse the Y scan direction
constexpr uint8_t PUSH_FLAG_SWAP_XY = 0x04;  // swap X and Y
constexpr uint8_t PUSH_FLAG_DOUBLE = 0x08;   // 2x oversampling: average 2x2 input pixels
constexpr uint8_t PUSH_FLAG_USE_LUT =
    0x10;  // convert through the LUT (default: gray value = level)

struct Config {
  pos_t width;              // width of the screen
  pos_t height;             // height of the screen
  PixelFormat inputFormat;  // format of the images given to push()
  uint8_t flags;            // CFG_FLAG_*
};

// Recommended settings of a display
struct LcdModule {
  Config config;                // recommended Config
  uint32_t fpsQ8;               // recommended frame rate, 8 fractional bits
  const uint8_t (*palette)[3];  // level -> apparent color (256 entries, RGB888)
};

// A Config with the given values
void getDefaultConfig(Config* cfg, pos_t w, pos_t h, PixelFormat fmt = PixelFormat::RGB565_SWAPPED,
                      uint8_t flags = 0);

// Bytes of the arena cfg needs
size_t calcRequiredArenaSize(const Config& cfg);

// Bytes of the LUT for the input format fmt
size_t calcLutSize(PixelFormat fmt);

// Bytes per pixel of fmt
size_t bytesPerPixel(PixelFormat fmt);

class Pdm {
 public:
  // Lays out the arena (4-byte aligned, kept by the caller while in use),
  // clears the levels to 0 and resets the phase to Phase::RANDOM (seed 1)
  bool init(const Config& cfg, uint8_t* arena, size_t arenaSize);
  const Config& getConfig() const { return cfg_; }
  bool isInitialized() const { return duty_ != nullptr; }

  // Builds the LUT for the input format in the arena from a palette
  // (256 entries, RGB888). Needs CFG_FLAG_USE_LUT.
  bool initLut(const uint8_t (*palette)[3]);
  // Uses a prebuilt LUT of calcLutSize() bytes (not copied; may be in flash)
  bool setLut(const uint8_t* lut);
  const uint8_t* getLut() const { return lut_; }

  // Converts an image to levels and writes them to the rectangle (dx, dy, dw, dh).
  // image points to the top-left pixel of the input rectangle, stride is the
  // bytes of an input line (0: the width of the input rectangle)
  bool push(const void* image, size_t stride, pos_t dx, pos_t dy, pos_t dw, pos_t dh,
            uint8_t flags = 0);
  bool push(const void* image, uint8_t flags = 0);

  // Fills a rectangle with a level
  bool fill(uint8_t level, pos_t dx, pos_t dy, pos_t dw, pos_t dh);
  bool fill(uint8_t level);

  // Initializes the accumulators
  void resetPhase(Phase phase, uint32_t seed = 1);

  // Advances the rectangle (sx, sy, sw, sh) by one frame and writes the packed
  // output bits to dest. stride is the bytes of a packed line (0: tight)
  bool pop(uint8_t* dest, size_t stride, pos_t sx, pos_t sy, pos_t sw, pos_t sh, uint8_t flags = 0);
  bool pop(uint8_t* dest, uint8_t flags = 0);

 private:
  Config cfg_ = {0, 0, PixelFormat::GRAY8, 0};
  uint8_t* duty_ = nullptr;  // levels
  uint8_t* acc_ = nullptr;   // accumulators
  uint8_t* lutBuf_ = nullptr;
  const uint8_t* lut_ = nullptr;

  bool inside(pos_t x, pos_t y, pos_t w, pos_t h) const;
};

}  // namespace pdmfrc
