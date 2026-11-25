#include "ui.h"

#include "font.h"

#include <cctype>
#include <cmath>
#include <cstring>

using namespace gfx;

namespace ui
{
namespace
{

void buttonBase(float cx, float cy, float r, bool hov)
{
    glColor4f(0.0f, 0.0f, 0.0f, 0.28f);
    circle(cx + 0.006f, cy - 0.008f, r, 28);
    if (hov) glColor4f(0.16f, 0.30f, 0.52f, 0.96f);
    else     glColor4f(0.08f, 0.14f, 0.26f, 0.78f);
    circle(cx, cy, r, 28);
    glColor4f(1.0f, 1.0f, 1.0f, hov ? 0.95f : 0.65f);
    glLineWidth(2.0f);
    ring(cx, cy, r, r, 36);
    glLineWidth(1.0f);
}

}   // namespace

void endFrame()
{
    mouse.pressed  = false;
    mouse.released = false;
}

// Text uses the vector font (font.cpp). `px` keeps its old meaning: the size of one
// pixel of the former 5x7 font, so a capital letter is 7 px tall.
float textWidth(const char* s, float px)
{
    return font::width(s, 7.0f * px);
}

void drawText(float x, float y, float px, const Color& c, const char* s, bool centered)
{
    font::draw(x, y, 7.0f * px, c, s, centered ? 1 : 0, true);
}

bool hover(float cx, float cy, float r)
{
    if (!mouse.valid) return false;
    const float dx = mouse.x - cx, dy = mouse.y - cy;
    return dx * dx + dy * dy <= r * r;
}

// RESTART: circular-arrow icon (a 300-degree arc built as a thick triangle strip, plus an
// arrow head placed on the arc's tangent at its end point)
bool backButton()
{
    const float cx = -WORLD_W + 0.095f, cy = 0.895f, r = 0.055f;
    const bool  hov = hover(cx, cy, r);
    buttonBase(cx, cy, r, hov);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    const float R = 0.026f, w = 0.0065f, a0 = 1.25f, a1 = a0 + 5.2f;          // radians
    const int   N = 28;
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= N; i++)
    {
        const float a = a0 + (a1 - a0) * i / N, c = std::cos(a), s = std::sin(a);
        glVertex2f(cx + (R - w) * c, cy + (R - w) * s);
        glVertex2f(cx + (R + w) * c, cy + (R + w) * s);
    }
    glEnd();
    // arrow head at the end of the arc, pointing along the tangent (direction of travel)
    const float ex = cx + R * std::cos(a1), ey = cy + R * std::sin(a1);
    const float tx = -std::sin(a1), ty = std::cos(a1), nx = std::cos(a1), ny = std::sin(a1), h = 0.017f;
    tri(ex + tx * h, ey + ty * h, ex + nx * h * 0.95f, ey + ny * h * 0.95f, ex - nx * h * 0.95f, ey - ny * h * 0.95f);
    if (hov) drawText(cx + r + 0.02f, cy - 0.02f, 0.0068f, rgb(1, 1, 1), "RESTART");
    return hov && mouse.pressed;
}

bool mapButton()
{
    const float cx = WORLD_W - 0.11f, cy = 0.88f, r = 0.07f;
    const bool  hov = hover(cx, cy, r);
    buttonBase(cx, cy, r, hov);
    // tri-fold map
    glColor4f(0.96f, 0.90f, 0.68f, 1.0f);
    quad(cx - 0.044f, cy - 0.030f, cx - 0.015f, cy - 0.040f, cx - 0.015f, cy + 0.030f, cx - 0.044f, cy + 0.040f);
    glColor4f(0.88f, 0.80f, 0.56f, 1.0f);
    quad(cx - 0.015f, cy - 0.040f, cx + 0.015f, cy - 0.030f, cx + 0.015f, cy + 0.040f, cx - 0.015f, cy + 0.030f);
    glColor4f(0.96f, 0.90f, 0.68f, 1.0f);
    quad(cx + 0.015f, cy - 0.030f, cx + 0.044f, cy - 0.040f, cx + 0.044f, cy + 0.030f, cx + 0.015f, cy + 0.040f);
    glColor4f(0.85f, 0.18f, 0.18f, 1.0f);                           // pin
    circle(cx + 0.004f, cy + 0.008f, 0.011f, 12);
    tri(cx - 0.007f, cy + 0.002f, cx + 0.015f, cy + 0.002f, cx + 0.004f, cy - 0.022f);
    if (hov) drawText(cx - r - 0.02f - textWidth("MAP", 0.0075f), cy - 0.02f, 0.0075f, rgb(1, 1, 1), "MAP");
    return hov && mouse.pressed;
}

}   // namespace ui
