// font.h - vector fonts rasterised by the program itself (no font library)
//
// Glyph outlines (lines + quadratic Bezier curves) come from src/fontdata.h.
// At start-up font::init() turns them into anti-aliased textures:
//   1. Bezier flattening       : adaptive de Casteljau subdivision -> polygon edges
//   2. Scan-line polygon fill  : edge table + active edge table, non-zero winding rule
//   3. Anti-aliasing           : 4 sub-scanlines per pixel x exact horizontal span coverage
//   4. Mipmaps                 : 2x2 box-filter pyramid built in software
// The logo gets extra image passes: dilation (outline), separable box blur (glow),
// and an offset difference mask (bevel highlight).

#ifndef FONT_H
#define FONT_H

#include "gfx.h"

namespace font
{

void  init();                                                     // needs a GL context

// UI text. size = CAP HEIGHT in world units. y = baseline. align: 0 left, 1 centre, 2 right
float width(const char* s, float cap);
void  draw(float x, float y, float cap, const gfx::Color& c, const char* s, int align = 0, bool shadow = true);

// The "Enchanted World" logo. t = seconds since it started to appear (>= 3 => fully shown),
// fade = 0..1 overall opacity, grow = extra scale while it leaves, glowBoost = extra glow.
void  drawLogo(float cx, float cy, float t, float fade, float grow, float glowBoost);

// software rasteriser, exposed for tests: coverage of a closed polygon (pixel units, y down)
struct Edge { float x0, y0, x1, y1; int dir; };
void  rasterize(const Edge* edges, int n, int w, int h, unsigned char* out);

}   // namespace font

#endif
