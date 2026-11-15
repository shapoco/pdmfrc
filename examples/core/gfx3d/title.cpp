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

const Line LINES[] = {
    {LARGE, "PDM-FRC"},
    {SMALL, nullptr},
    {SMALL, "Monochrome"},
    {SMALL, "LCD"},
    {SMALL, nullptr},
    {SMALL, "3D-GFX"},
    {SMALL, "Demo"},
};

}  // namespace

void draw(uint16_t* dst, uint32_t stride) {
  const g2::Surface surf =
      g2::makeSurface(g2::PixelFormat::RGB565_SWAPPED, WIDTH, HEIGHT, dst, stride);
  g2::Graphics2D g(surf);
  g.clear(g2::Colors::BLACK);
  g.setTextColor(g2::Colors::WHITE);

  // Height of the block, to center it vertically
  int total = 0;
  for (const Line& l : LINES) {
    g.setFont(l.font);
    total += g.textMetrics("A").lineAdvance;
  }

  int y = (HEIGHT - total) / 2;
  for (const Line& l : LINES) {
    g.setFont(l.font);
    const g2::TextMetrics m = g.textMetrics(l.text ? l.text : "A");
    if (l.text) g.drawString((WIDTH - m.width) / 2, y, l.text);
    y += m.lineAdvance;
  }
}

}  // namespace title
