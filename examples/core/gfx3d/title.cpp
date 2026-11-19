// SPDX-License-Identifier: MIT
#include "title.hpp"

#include "shapoco/gfx2d/font/ShapoSansP_s08c07.h"
#include "shapoco/gfx2d/font/ShapoSansP_s12c09a01w02.h"
#include "shapoco/gfx2d/gfx2d.hpp"

namespace title {
namespace {

namespace g2 = shapoco::gfx2d;

struct Line {
  const g2::GFXfont* font;
  const char* text;  // nullptr: blank line (height of the small font)
};

const g2::GFXfont* const LARGE = &g2::ShapoSansP_s12c09a01w02;
const g2::GFXfont* const SMALL = &g2::ShapoSansP_s08c07;

const Line LINES_WIDE[] = {
    {LARGE, "PDM-FRC"}, {SMALL, nullptr},  {SMALL, "Monochrome"}, {SMALL, "LCD"},
    {SMALL, nullptr},   {SMALL, "3D-GFX"}, {SMALL, "Demo"},       {nullptr, nullptr},
};

// For a display narrower than 64 px (48 px wide or more)
const Line LINES_NARROW[] = {
    {SMALL, "PDM-FRC"}, {SMALL, nullptr},  {SMALL, "Mono-"}, {SMALL, "chrome"},  {SMALL, "LCD"},
    {SMALL, nullptr},   {SMALL, "3D-GFX"}, {SMALL, "Demo"},  {nullptr, nullptr},
};

// For a display shorter than the layouts above (48 px wide, 84 px high)
const Line LINES_SHORT[] = {
    {SMALL, "PDM-FRC"}, {SMALL, nullptr}, {SMALL, "LCD"},
    {SMALL, "3D-GFX"},  {SMALL, "Demo"},  {nullptr, nullptr},
};

const Line* const LAYOUTS[] = {LINES_WIDE, LINES_NARROW, LINES_SHORT};

// Height of the block of lines
int block_height(g2::Graphics2D& g, const Line* lines) {
  int total = 0;
  for (const Line* l = lines; l->font; ++l) {
    g.setFont(l->font);
    total += g.textMetrics("A").lineAdvance;
  }
  return total;
}

bool fits(g2::Graphics2D& g, const Line* lines, int width, int height) {
  if (block_height(g, lines) > height) return false;
  for (const Line* l = lines; l->font; ++l) {
    if (!l->text) continue;
    g.setFont(l->font);
    if (g.textMetrics(l->text).width > width) return false;
  }
  return true;
}

}  // namespace

void draw(uint16_t* dst, uint32_t stride, int width, int height) {
  if (width > WIDTH) width = WIDTH;
  if (height > HEIGHT) height = HEIGHT;
  const g2::Surface surf =
      g2::makeSurface(g2::PixelFormat::RGB565_SWAPPED, width, HEIGHT, dst, stride);
  g2::Graphics2D g(surf);
  g.clear(g2::Colors::BLACK);
  g.setTextColor(g2::Colors::WHITE);

  // The first layout that fits (the last one otherwise)
  const int n = (int)(sizeof(LAYOUTS) / sizeof(LAYOUTS[0]));
  const Line* lines = LAYOUTS[n - 1];
  for (int i = 0; i < n - 1; ++i) {
    if (fits(g, LAYOUTS[i], width, height)) {
      lines = LAYOUTS[i];
      break;
    }
  }

  // Centered vertically (in HEIGHT: the crop to height is centered too)
  int y = (HEIGHT - block_height(g, lines)) / 2;
  for (const Line* l = lines; l->font; ++l) {
    g.setFont(l->font);
    const g2::TextMetrics m = g.textMetrics(l->text ? l->text : "A");
    if (l->text) g.drawString((width - m.width) / 2, y, l->text);
    y += m.lineAdvance;
  }
}

}  // namespace title
