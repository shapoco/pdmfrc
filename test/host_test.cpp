// Host test: compares Pdm with straightforward reference implementations of SPEC.md
// SPDX-License-Identifier: MIT

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "pdmfrc/configs/aqm1248a.hpp"
#include "pdmfrc/configs/tg12864b.hpp"
#include "pdmfrc/pdmfrc.hpp"

using namespace pdmfrc;

namespace {

int failures = 0;

#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                            \
    }                                                        \
  } while (0)

uint32_t rng = 12345;
uint32_t rnd() {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
int rndRange(int n) { return n > 0 ? (int)(rnd() % (uint32_t)n) : 0; }

// ---- reference implementation ----

uint32_t refPixel(PixelFormat fmt, const uint8_t* img, size_t stride, int u, int v) {
  const uint8_t* p = img + (size_t)v * stride + (size_t)u * bytesPerPixel(fmt);
  if (fmt == PixelFormat::GRAY8) return p[0];
  uint32_t c = p[0] | (p[1] << 8);  // little endian host
  if (fmt == PixelFormat::RGB565_SWAPPED) c = ((c >> 8) | (c << 8)) & 0xFFFF;
  return c;
}

uint8_t refGray565(uint32_t c) {
  const uint32_t r5 = c >> 11, g6 = (c >> 5) & 63, b5 = c & 31;
  const uint32_t r = (r5 << 3) | (r5 >> 2), g = (g6 << 2) | (g6 >> 4), b = (b5 << 3) | (b5 >> 2);
  return (uint8_t)((r * 77 + g * 150 + b * 29 + 128) >> 8);
}

uint32_t refLutIndex(uint32_t c) {
  const uint32_t r = (c >> 11) >> 1, g = ((c >> 5) & 63) >> 1, b = (c & 31) >> 1;  // R4G5B4
  return (r << 9) | (g << 4) | b;
}

uint8_t refLevel(PixelFormat fmt, const uint8_t* img, size_t stride, int u, int v, bool dbl,
                 const uint8_t* lut) {
  if (fmt == PixelFormat::GRAY8) {
    uint32_t g;
    if (dbl) {
      g = (refPixel(fmt, img, stride, 2 * u, 2 * v) + refPixel(fmt, img, stride, 2 * u + 1, 2 * v) +
           refPixel(fmt, img, stride, 2 * u, 2 * v + 1) +
           refPixel(fmt, img, stride, 2 * u + 1, 2 * v + 1) + 2) >>
          2;
    } else {
      g = refPixel(fmt, img, stride, u, v);
    }
    return lut ? lut[g] : (uint8_t)g;
  }
  uint32_t c;
  if (dbl) {
    uint32_t s[3] = {0, 0, 0};
    for (int k = 0; k < 4; ++k) {
      const uint32_t p = refPixel(fmt, img, stride, 2 * u + (k & 1), 2 * v + (k >> 1));
      s[0] += p >> 11;
      s[1] += (p >> 5) & 63;
      s[2] += p & 31;
    }
    c = (((s[0] + 2) >> 2) << 11) | (((s[1] + 2) >> 2) << 5) | ((s[2] + 2) >> 2);
  } else {
    c = refPixel(fmt, img, stride, u, v);
  }
  return lut ? lut[refLutIndex(c)] : refGray565(c);
}

// levels of a whole screen, row major
struct RefScreen {
  int w, h;
  std::vector<uint8_t> level, acc;
};

void refPush(RefScreen& s, PixelFormat fmt, const uint8_t* img, size_t stride, int dx, int dy,
             int dw, int dh, uint8_t flags, const uint8_t* lut) {
  const bool sw = flags & PUSH_FLAG_SWAP_XY, xf = flags & PUSH_FLAG_X_FLIP,
             yf = flags & PUSH_FLAG_Y_FLIP, dbl = flags & PUSH_FLAG_DOUBLE;
  for (int y = 0; y < dh; ++y) {
    for (int x = 0; x < dw; ++x) {
      const int x1 = xf ? dw - 1 - x : x, y1 = yf ? dh - 1 - y : y;
      const int u = sw ? y1 : x1, v = sw ? x1 : y1;
      s.level[(dy + y) * s.w + dx + x] =
          refLevel(fmt, img, stride, u, v, dbl, (flags & PUSH_FLAG_USE_LUT) ? lut : nullptr);
    }
  }
}

// ---- tests ----

void testPush(uint8_t cfgFlags, PixelFormat fmt) {
  const int W = 48, H = 40;
  Config cfg;
  getDefaultConfig(&cfg, W, H, fmt, cfgFlags | CFG_FLAG_USE_LUT);
  std::vector<uint32_t> arenaWords(calcRequiredArenaSize(cfg) / 4 + 1);
  Pdm pdm;
  CHECK(pdm.init(cfg, (uint8_t*)arenaWords.data(), arenaWords.size() * 4));

  // arbitrary LUT
  std::vector<uint8_t> lut(calcLutSize(fmt));
  for (auto& v : lut) v = (uint8_t)rnd();
  CHECK(pdm.setLut(lut.data()));

  RefScreen ref = {W, H, std::vector<uint8_t>(W * H, 0), {}};
  const size_t bpp = bytesPerPixel(fmt);

  for (int iter = 0; iter < 400; ++iter) {
    const uint8_t flags = (uint8_t)(rnd() & 0x1F);
    const int dw = 1 + rndRange(W), dh = 1 + rndRange(H);
    const int dx = rndRange(W - dw + 1), dy = rndRange(H - dh + 1);
    const int s = (flags & PUSH_FLAG_DOUBLE) ? 2 : 1;
    const bool sw = flags & PUSH_FLAG_SWAP_XY;
    const int iw = (sw ? dh : dw) * s, ih = (sw ? dw : dh) * s;
    const bool tight = rnd() & 1;
    const size_t stride = (iw + (tight ? 0 : 1 + rndRange(5))) * bpp;
    std::vector<uint16_t> img((stride * ih + 1) / 2 + 1);
    uint8_t* bytes = (uint8_t*)img.data();
    for (size_t i = 0; i < stride * ih; ++i) bytes[i] = (uint8_t)rnd();

    CHECK(pdm.push(bytes, tight ? 0 : stride, dx, dy, dw, dh, flags));
    refPush(ref, fmt, bytes, stride, dx, dy, dw, dh, flags, lut.data());

    if (rndRange(8) == 0) {
      const int fw = 1 + rndRange(W), fh = 1 + rndRange(H);
      const int fx = rndRange(W - fw + 1), fy = rndRange(H - fh + 1);
      const uint8_t lv = (uint8_t)rnd();
      CHECK(pdm.fill(lv, fx, fy, fw, fh));
      for (int y = fy; y < fy + fh; ++y)
        for (int x = fx; x < fx + fw; ++x) ref.level[y * W + x] = lv;
    }
  }

  // read the levels back through pop with the phase at 0: a pixel of level
  // l is ON in the first frame only for l == 255 ... so compare over 256
  // frames: the number of ON frames equals l + (l >> 7) clipped to 256
  pdm.resetPhase(Phase::ZERO);
  const bool v8 = cfgFlags & CFG_FLAG_PACK_V8;
  const size_t outSize = v8 ? (size_t)W * (H / 8) : (size_t)(W / 8) * H;
  std::vector<uint8_t> out(outSize);
  std::vector<int> count(W * H, 0);
  for (int f = 0; f < 256; ++f) {
    CHECK(pdm.pop(out.data()));
    for (int y = 0; y < H; ++y) {
      for (int x = 0; x < W; ++x) {
        int bit;
        if (v8) {
          const uint8_t b = out[(y / 8) * W + x];
          bit = (cfgFlags & CFG_FLAG_LSB1ST) ? (b >> (y & 7)) & 1 : (b >> (7 - (y & 7))) & 1;
        } else {
          const uint8_t b = out[y * (W / 8) + x / 8];
          bit = (cfgFlags & CFG_FLAG_LSB1ST) ? (b >> (x & 7)) & 1 : (b >> (7 - (x & 7))) & 1;
        }
        if (cfgFlags & CFG_FLAG_INVERT) bit ^= 1;
        count[y * W + x] += bit;
      }
    }
  }
  int bad = 0;
  for (int i = 0; i < W * H; ++i) {
    const int l = ref.level[i];
    if (count[i] != l + (l >> 7)) ++bad;
  }
  if (bad) printf("  push mismatch: flags=0x%02x fmt=%d bad=%d\n", cfgFlags, (int)fmt, bad);
  CHECK(bad == 0);
}

// pop of rectangles (with stride) against a full-screen reference model
// Anti-ghost amount of each pixel (SPEC.md 3.14) for the levels img
void refAntiGhost(uint8_t cfgFlags, int strength, int gammaQ8, const std::vector<uint8_t>& img,
                  int W, int H, std::vector<uint8_t>& amount) {
  uint32_t f[256];
  for (int l = 0; l < 256; ++l) {
    f[l] = (uint32_t)(255.0f * powf((float)l / 255.0f, (float)gammaQ8 / 256.0f) + 0.5f);
  }
  amount.assign(W * H, 0);
  const bool col = cfgFlags & CFG_FLAG_ANTIGHOST_COL;
  if (!(cfgFlags & (CFG_FLAG_ANTIGHOST_ROW | CFG_FLAG_ANTIGHOST_COL))) return;
  const int lines = col ? W : H, len = col ? H : W;
  for (int i = 0; i < lines; ++i) {
    uint32_t sum = 0;
    for (int j = 0; j < len; ++j) sum += f[col ? img[j * W + i] : img[i * W + j]];
    const uint32_t den = (uint32_t)len * 255;
    const uint8_t a = (uint8_t)((sum * (uint32_t)strength + den / 2) / den);
    for (int j = 0; j < len; ++j) amount[col ? j * W + i : i * W + j] = a;
  }
}

void testPop(uint8_t cfgFlags, int strength, int gammaQ8 = 256) {
  const int W = 64, H = 32;
  Config cfg;
  getDefaultConfig(&cfg, W, H, PixelFormat::GRAY8, cfgFlags);
  std::vector<uint32_t> arena(calcRequiredArenaSize(cfg) / 4);
  Pdm pdm;
  CHECK(pdm.init(cfg, (uint8_t*)arena.data(), arena.size() * 4));
  std::vector<uint8_t> img(W * H);
  for (auto& v : img) v = (uint8_t)rnd();
  CHECK(pdm.push(img.data()));
  const bool ag = cfgFlags & (CFG_FLAG_ANTIGHOST_ROW | CFG_FLAG_ANTIGHOST_COL);
  CHECK(pdm.setAntiGhost((uint8_t)strength, (uint16_t)gammaQ8) == ag);
  if (!ag) {
    strength = 0;
    gammaQ8 = 256;
  }
  CHECK(pdm.getAntiGhost() == strength);
  CHECK(pdm.getAntiGhostGamma() == gammaQ8);
  pdm.resetPhase(Phase::RANDOM, 777);
  std::vector<uint8_t> amount;

  // reference accumulators: same RANDOM sequence (y then x)
  std::vector<uint8_t> acc(W * H);
  uint32_t r = 777;
  for (int i = 0; i < W * H; ++i) {
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    acc[i] = (uint8_t)(r >> 24);
  }

  const bool v8 = cfgFlags & CFG_FLAG_PACK_V8;
  for (int frame = 0; frame < 50; ++frame) {
    // changes some levels (and the strength) between the frames
    if (frame % 3 == 1) {
      const int dx = rndRange(W), dy = rndRange(H);
      const int dw = 1 + rndRange(W - dx), dh = 1 + rndRange(H - dy);
      const uint8_t level = (uint8_t)rnd();
      CHECK(pdm.fill(level, (pos_t)dx, (pos_t)dy, (pos_t)dw, (pos_t)dh));
      for (int y = dy; y < dy + dh; ++y)
        for (int x = dx; x < dx + dw; ++x) img[y * W + x] = level;
    }
    if (ag && frame == 25 && strength) {
      strength = rndRange(256);
      gammaQ8 = gammaQ8 == 256 ? 51 + rndRange(700) : 256;  // also back to linear
      CHECK(pdm.setAntiGhost((uint8_t)strength, (uint16_t)gammaQ8));
    }
    refAntiGhost(cfgFlags, strength, gammaQ8, img, W, H, amount);
    // one frame as 4 quadrants with arbitrary order
    const int hw = W / 2, hh = H / 2;
    int order[4] = {0, 1, 2, 3};
    for (int i = 3; i > 0; --i) {
      const int j = rndRange(i + 1);
      const int t = order[i];
      order[i] = order[j];
      order[j] = t;
    }
    for (int q : order) {
      const int sx = (q & 1) * hw, sy = (q >> 1) * hh;
      const size_t tight = v8 ? hw : hw / 8;
      const size_t stride = tight + 3;
      const int rows = v8 ? hh / 8 : hh;
      std::vector<uint8_t> out(stride * rows, 0xA5);
      CHECK(pdm.pop(out.data(), stride, sx, sy, hw, hh));
      for (int y = sy; y < sy + hh; ++y) {
        for (int x = sx; x < sx + hw; ++x) {
          const int la = (int)img[y * W + x] - amount[y * W + x];
          const uint32_t l = la > 0 ? (uint32_t)la : 0;
          const uint32_t s = acc[y * W + x] + l + (l >> 7);
          acc[y * W + x] = (uint8_t)s;
          int bit = (int)(s >> 8);
          if (cfgFlags & CFG_FLAG_INVERT) bit ^= 1;
          int got;
          if (v8) {
            const uint8_t b = out[(y - sy) / 8 * stride + (x - sx)];
            got = (cfgFlags & CFG_FLAG_LSB1ST) ? (b >> (y & 7)) & 1 : (b >> (7 - (y & 7))) & 1;
          } else {
            const uint8_t b = out[(y - sy) * stride + (x - sx) / 8];
            got = (cfgFlags & CFG_FLAG_LSB1ST) ? (b >> (x & 7)) & 1 : (b >> (7 - (x & 7))) & 1;
          }
          if (got != bit) {
            ++failures;
            printf("FAIL pop flags=0x%02x strength=%d frame=%d (%d,%d)\n", cfgFlags, strength,
                   frame, x, y);
            return;
          }
        }
        // padding bytes must stay untouched
      }
      for (int rr = 0; rr < rows; ++rr)
        for (size_t c = tight; c < stride; ++c) CHECK(out[rr * stride + c] == 0xA5);
    }
  }
}

void testErrors() {
  Config cfg;
  getDefaultConfig(&cfg, 20, 16, PixelFormat::GRAY8, 0);  // width not a multiple of 8
  uint32_t arena[256];
  Pdm pdm;
  CHECK(!pdm.init(cfg, (uint8_t*)arena, sizeof(arena)));
  cfg.flags = CFG_FLAG_PACK_V8;  // height 16: OK
  CHECK(pdm.init(cfg, (uint8_t*)arena, sizeof(arena)));
  CHECK(!pdm.init(cfg, (uint8_t*)arena + 1, sizeof(arena) - 1));  // misaligned
  CHECK(!pdm.init(cfg, (uint8_t*)arena, 20 * 16 * 2 - 1));        // too small
  CHECK(pdm.init(cfg, (uint8_t*)arena, sizeof(arena)));
  uint8_t img[64 * 64] = {};
  CHECK(!pdm.push(img, 0, 10, 0, 11, 1));                   // outside
  CHECK(!pdm.push(img, 0, 0, 0, 1, 1, PUSH_FLAG_USE_LUT));  // no LUT
  CHECK(!pdm.initLut(configs::TG12864B.palette));           // no CFG_FLAG_USE_LUT
  uint8_t out[64];
  CHECK(!pdm.pop(out, 0, 0, 4, 20, 8));     // sy not a multiple of 8
  CHECK(!pdm.pop(out, 0, 0, 0, 20, 8, 1));  // flags
  CHECK(pdm.pop(out, 0, 0, 8, 20, 8));
  CHECK(pdm.push(img, 0, 0, 0, 0, 0));  // empty
}

// OKLab of an sRGB color (0..255), double precision
void oklab(const double* rgb, double* out) {
  double lin[3];
  for (int i = 0; i < 3; ++i) {
    const double c = rgb[i] / 255.0;
    lin[i] = c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
  }
  const double l = cbrt(0.4122214708 * lin[0] + 0.5363325363 * lin[1] + 0.0514459929 * lin[2]);
  const double m = cbrt(0.2119034982 * lin[0] + 0.6806995451 * lin[1] + 0.1073969566 * lin[2]);
  const double s = cbrt(0.0883024619 * lin[0] + 0.2817188376 * lin[1] + 0.6299787005 * lin[2]);
  out[0] = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
  out[1] = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
  out[2] = 0.0259066487 * l + 0.7827717662 * m - 0.8086783821 * s;
}

double dist(const double* a, const double* b) {
  return sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) +
              (a[2] - b[2]) * (a[2] - b[2]));
}

int center8(int index, int bits, int fullBits) {
  const int w = 1 << (fullBits - bits);
  const int num = (2 * index * w + w - 1) * 255;
  const int den = 2 * ((1 << fullBits) - 1);
  int q = num / den;
  const int r2 = 2 * (num % den);
  if (r2 > den || (r2 == den && (q & 1))) ++q;
  return q;
}

void testLutFiles(const char* lut565Path, const char* lutGrayPath) {
  // compares initLut with the LUTs of tools/mklut.py (raw bytes)
  for (int k = 0; k < 2; ++k) {
    const char* path = k ? lutGrayPath : lut565Path;
    if (!path) continue;
    const PixelFormat fmt = k ? PixelFormat::GRAY8 : PixelFormat::RGB565_SWAPPED;
    FILE* f = fopen(path, "rb");
    if (!f) {
      printf("cannot open %s\n", path);
      ++failures;
      continue;
    }
    std::vector<uint8_t> want(calcLutSize(fmt));
    const size_t n = fread(want.data(), 1, want.size(), f);
    fclose(f);
    CHECK(n == want.size());

    Config cfg;
    getDefaultConfig(&cfg, 8, 8, fmt, CFG_FLAG_USE_LUT);
    std::vector<uint32_t> arena(calcRequiredArenaSize(cfg) / 4 + 1);
    Pdm pdm;
    CHECK(pdm.init(cfg, (uint8_t*)arena.data(), arena.size() * 4));
    CHECK(pdm.initLut(configs::TG12864B.palette));
    // Where the levels differ, both must be (almost) equally near to the input
    const uint8_t(*pal)[3] = configs::TG12864B.palette;
    int diff = 0;
    double worst = 0;
    for (size_t i = 0; i < want.size(); ++i) {
      const int got = pdm.getLut()[i];
      if (got == want[i]) continue;
      ++diff;
      double in[3];
      if (k) {
        in[0] = in[1] = in[2] = (double)i;
      } else {
        in[0] = center8((int)(i >> 9), 4, 5);
        in[1] = center8((int)(i >> 4) & 31, 5, 6);
        in[2] = center8((int)i & 15, 4, 5);
      }
      double x[3], a[3], b[3];
      oklab(in, x);
      const double pa[3] = {(double)pal[got][0], (double)pal[got][1], (double)pal[got][2]};
      const double pb[3] = {(double)pal[want[i]][0], (double)pal[want[i]][1],
                            (double)pal[want[i]][2]};
      oklab(pa, a);
      oklab(pb, b);
      const double da = k ? fabs(a[0] - x[0]) : dist(a, x);
      const double db = k ? fabs(b[0] - x[0]) : dist(b, x);
      if (fabs(da - db) > worst) worst = fabs(da - db);
    }
    printf("LUT %s: %d / %zu entries differ from mklut.py, distance difference max %.6f\n",
           k ? "gray8" : "rgb565", diff, want.size(), worst);
    CHECK(worst < 0.0005);  // ties in fixed point only
  }
}

}  // namespace

// The recommended settings of the displays: init, LUT, and the LUT of a gray
// palette that saturates (AQM1248A: white below 22, black from 128)
void testAntiGhostConfig() {
  Config cfg;
  getDefaultConfig(&cfg, 64, 32, PixelFormat::GRAY8, CFG_FLAG_PACK_V8);
  const size_t base = calcRequiredArenaSize(cfg);
  cfg.flags |= CFG_FLAG_ANTIGHOST_COL;
  CHECK(calcRequiredArenaSize(cfg) == base + 256 + 64);  // curve + amounts
  cfg.flags = CFG_FLAG_ANTIGHOST_ROW;
  CHECK(calcRequiredArenaSize(cfg) == base + 256 + 32);
  cfg.width = 40;
  cfg.height = 5;  // 5 rows -> 8 bytes (4-byte multiple)
  CHECK(calcRequiredArenaSize(cfg) == (size_t)40 * 5 * 2 + 256 + 8);
  std::vector<uint32_t> arena(2048);
  Pdm pdm;
  getDefaultConfig(&cfg, 64, 32, PixelFormat::GRAY8,
                   CFG_FLAG_ANTIGHOST_ROW | CFG_FLAG_ANTIGHOST_COL);
  CHECK(!pdm.init(cfg, (uint8_t*)arena.data(), arena.size() * 4));  // not both
  cfg.flags = CFG_FLAG_ANTIGHOST_COL;
  CHECK(pdm.init(cfg, (uint8_t*)arena.data(), arena.size() * 4));
  CHECK(pdm.getAntiGhost() == 0);
  CHECK(pdm.getAntiGhostGamma() == 256);
  CHECK(!pdm.setAntiGhost(10, 0));  // gamma 0
  CHECK(pdm.fill(255));
  CHECK(pdm.setAntiGhost(100));
  // all 255 -> each level 255 - 100
  std::vector<uint8_t> out(64 * 32 / 8), acc(64 * 32, 0);
  pdm.resetPhase(Phase::ZERO);
  int ones = 0;
  for (int f = 0; f < 256; ++f) {
    CHECK(pdm.pop(out.data()));
    for (uint8_t b : out)
      for (int k = 0; k < 8; ++k) ones += (b >> k) & 1;
  }
  CHECK(ones == 156 * 64 * 32);  // level 155 -> 156 / 256
}

void testModules() {
  const LcdModule* modules[] = {&configs::TG12864B, &configs::AQM1248A};
  for (const LcdModule* m : modules) {
    std::vector<uint32_t> arena(calcRequiredArenaSize(m->config) / 4 + 1);
    Pdm pdm;
    CHECK(pdm.init(m->config, (uint8_t*)arena.data(), arena.size() * 4));
    CHECK(pdm.initLut(m->palette));
  }
  Config cfg = configs::AQM1248A.config;
  cfg.inputFormat = PixelFormat::GRAY8;
  std::vector<uint32_t> arena(calcRequiredArenaSize(cfg) / 4 + 1);
  Pdm pdm;
  CHECK(pdm.init(cfg, (uint8_t*)arena.data(), arena.size() * 4));
  CHECK(pdm.initLut(configs::AQM1248A.palette));
  const uint8_t* lut = pdm.getLut();
  CHECK(lut[255] == 0);  // white: the smallest of the equally white levels
  CHECK(lut[0] == 238);  // black: the smallest of the equally black levels
  for (int g = 1; g < 256; ++g) CHECK(lut[g] <= lut[g - 1]);  // monotonic
}

int main(int argc, char** argv) {
  const PixelFormat fmts[] = {PixelFormat::GRAY8, PixelFormat::RGB565, PixelFormat::RGB565_SWAPPED};
  for (uint8_t f = 0; f < 8; ++f) {
    for (PixelFormat fmt : fmts) testPush(f, fmt);
    testPop(f, 0);
    const int strengths[] = {0, 1, 40, 255};
    const int gammas[] = {256, 128, 179, 512};
    for (int st : strengths) {
      for (int g : gammas) {
        testPop(f | CFG_FLAG_ANTIGHOST_ROW, st, g);
        testPop(f | CFG_FLAG_ANTIGHOST_COL, st, g);
      }
    }
  }
  testAntiGhostConfig();
  testErrors();
  testModules();
  if (argc >= 3) testLutFiles(argv[1], argv[2]);
  printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
