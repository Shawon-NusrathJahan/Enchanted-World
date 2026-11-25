#include "village.h"
#include "cgmath.h"
#include "gfx.h"
#include "theme.h"
#include "ui.h"
#include "world.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

using namespace gfx;

namespace
{

struct Style { Color wall, roof, shutter; };

const Style STYLES[4] = {
    {rgb(0.96f, 0.90f, 0.76f), rgb(0.74f, 0.22f, 0.18f), rgb(0.25f, 0.45f, 0.35f)},   // cream / red / green
    {rgb(0.97f, 0.80f, 0.66f), rgb(0.82f, 0.32f, 0.20f), rgb(0.25f, 0.40f, 0.62f)},   // peach / orange / blue
    {rgb(0.93f, 0.93f, 0.88f), rgb(0.62f, 0.18f, 0.20f), rgb(0.45f, 0.28f, 0.18f)},   // white / dark red / brown
    {rgb(0.82f, 0.88f, 0.92f), rgb(0.80f, 0.38f, 0.24f), rgb(0.65f, 0.22f, 0.22f)},   // blue-grey / terracotta / red
};

// ---------------------------------------------------------------------------
// Local drawing in "house units": house is 1.0 wide, walls 0..0.7, roof to 1.3
// ---------------------------------------------------------------------------
void drawWindow(float cx, float cy, float w, float h, bool lit, const Color& shutter, bool flowerBox)
{
    // shutters
    setColor(shutter);
    rect(cx - w * 0.5f - w * 0.5f, cy - h * 0.5f, cx - w * 0.5f - 0.008f, cy + h * 0.5f);
    rect(cx + w * 0.5f + 0.008f, cy - h * 0.5f, cx + w * 0.5f + w * 0.5f, cy + h * 0.5f);
    setColor(shade(shutter, 0.75f));
    for (int i = 1; i < 4; i++)                       // louvres
    {
        const float yy = cy - h * 0.5f + h * i / 4.0f;
        rect(cx - w - 0.0f, yy - 0.004f, cx - w * 0.5f - 0.008f, yy + 0.004f);
        rect(cx + w * 0.5f + 0.008f, yy - 0.004f, cx + w, yy + 0.004f);
    }
    // frame + glass
    setColor(rgb(0.95f, 0.92f, 0.85f));
    rect(cx - w * 0.5f - 0.014f, cy - h * 0.5f - 0.014f, cx + w * 0.5f + 0.014f, cy + h * 0.5f + 0.014f);
    if (lit || theme::cur().windowsLit > 0.5f)
    {
        const bool oldTint = gfx::useTint;          // lit windows glow: no night tint
        gfx::useTint = false;
        rectV(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f, rgb(0.98f, 0.70f, 0.32f), rgb(1.0f, 0.90f, 0.58f));
        gfx::useTint = oldTint;
    }
    else     rectV(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f, rgb(0.42f, 0.66f, 0.86f), rgb(0.74f, 0.89f, 0.97f));
    // cross bars
    setColor(rgb(0.95f, 0.92f, 0.85f));
    rect(cx - 0.006f, cy - h * 0.5f, cx + 0.006f, cy + h * 0.5f);
    rect(cx - w * 0.5f, cy - 0.006f, cx + w * 0.5f, cy + 0.006f);
    // sill
    setColor(rgb(0.72f, 0.70f, 0.66f));
    rect(cx - w * 0.5f - 0.03f, cy - h * 0.5f - 0.035f, cx + w * 0.5f + 0.03f, cy - h * 0.5f - 0.012f);
    if (flowerBox)
    {
        setColor(rgb(0.45f, 0.28f, 0.16f));
        rect(cx - w * 0.5f - 0.01f, cy - h * 0.5f - 0.07f, cx + w * 0.5f + 0.01f, cy - h * 0.5f - 0.03f);
        static const Color FC[3] = {rgb(0.90f, 0.20f, 0.30f), rgb(1.0f, 0.80f, 0.20f), rgb(0.95f, 0.45f, 0.70f)};
        for (int i = 0; i < 3; i++)
        {
            setColor(rgb(0.20f, 0.55f, 0.25f));
            circle(cx - w * 0.33f + i * w * 0.33f, cy - h * 0.5f - 0.025f, 0.022f, 10);
            setColor(FC[i]);
            circle(cx - w * 0.33f + i * w * 0.33f, cy - h * 0.5f - 0.012f, 0.017f, 10);
        }
    }
}

void drawSmoke(float x, float y, double time)
{
    for (int i = 0; i < 4; i++)
    {
        const float p = fract((float)(time * 0.35) + i * 0.25f);
        const float a = 0.55f * (1.0f - p) * smoothstep(0.0f, 0.12f, p);
        setColor(rgb(0.93f, 0.93f, 0.96f, a));
        circle(x + 0.30f * p + 0.06f * std::sin(p * 6.0f + i), y + 0.9f * p, 0.05f + 0.13f * p, 16);
    }
}

void drawTower(float x, float y, float w, double time)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(w, w, 1.0f);

    setColor(rgb(0.0f, 0.0f, 0.0f, 0.16f));
    ellipse(0.0f, 0.0f, 0.75f, 0.09f);

    rectV(-0.5f, 0.0f, 0.5f, 2.2f, rgb(0.66f, 0.64f, 0.62f), rgb(0.90f, 0.87f, 0.82f));
    setColor(rgb(0.0f, 0.0f, 0.0f, 0.10f));
    rect(0.15f, 0.0f, 0.5f, 2.2f);                                   // shaded side
    setColor(rgb(0.30f, 0.30f, 0.38f));
    for (int i = 0; i < 2; i++)                                      // arched slit windows
    {
        const float yy = 0.9f + i * 0.65f;
        rect(-0.07f, yy, 0.07f, yy + 0.30f);
        circle(0.0f, yy + 0.30f, 0.07f, 12);
    }
    setColor(rgb(0.55f, 0.52f, 0.50f));
    rect(-0.56f, 2.14f, 0.56f, 2.26f);                               // ledge
    setColor(rgb(0.78f, 0.22f, 0.18f));
    tri(-0.68f, 2.24f, 0.68f, 2.24f, 0.0f, 3.5f);                    // cone roof
    setColor(rgb(0.0f, 0.0f, 0.0f, 0.18f));
    tri(0.0f, 2.24f, 0.68f, 2.24f, 0.0f, 3.5f);
    setColor(rgb(0.3f, 0.2f, 0.15f));
    rect(-0.01f, 3.45f, 0.01f, 3.95f);                               // flag pole
    setColor(rgb(0.95f, 0.75f, 0.15f));
    const float fl = 0.04f * std::sin((float)time * 4.0f);
    tri(0.01f, 3.92f, 0.01f, 3.72f, 0.30f + fl, 3.82f);              // waving flag
    glPopMatrix();
}

}   // namespace

// ---------------------------------------------------------------------------
void drawHouse(float x, float y, float w, int style, bool smoke, double time)
{
    const Style& st = STYLES[style & 3];
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(w, w, 1.0f);

    // ground shadow
    setColor(rgb(0.0f, 0.0f, 0.0f, 0.18f));
    ellipse(0.0f, 0.0f, 0.72f, 0.08f);

    // chimney (behind the roof)
    setColor(rgb(0.62f, 0.30f, 0.24f));
    rect(0.20f, 0.85f, 0.34f, 1.20f);
    setColor(rgb(0.45f, 0.20f, 0.16f));
    for (int i = 0; i < 4; i++) rect(0.20f, 0.92f + i * 0.08f, 0.34f, 0.935f + i * 0.08f);
    setColor(rgb(0.50f, 0.48f, 0.46f));
    rect(0.18f, 1.20f, 0.36f, 1.25f);

    // walls + stone base + corner posts
    rectV(-0.48f, 0.08f, 0.48f, 0.70f, shade(st.wall, 0.88f), st.wall);
    setColor(rgb(0.0f, 0.0f, 0.0f, 0.07f));
    rect(0.30f, 0.08f, 0.48f, 0.70f);                                 // soft side shade
    setColor(rgb(0.56f, 0.53f, 0.50f));
    rect(-0.50f, 0.0f, 0.50f, 0.10f);
    setColor(rgb(0.66f, 0.63f, 0.60f));
    rect(-0.50f, 0.075f, 0.50f, 0.10f);
    setColor(rgb(0.40f, 0.27f, 0.18f));
    rect(-0.48f, 0.10f, -0.45f, 0.70f);
    rect(0.45f, 0.10f, 0.48f, 0.70f);

    // roof: alternating tile bands
    for (int k = 0; k < 6; k++)
    {
        const float k0 = k / 6.0f, k1 = (k + 1) / 6.0f;
        const float y0 = 0.68f + 0.62f * k0, y1 = 0.68f + 0.62f * k1;
        const float h0 = 0.62f * (1.0f - k0), h1 = 0.62f * (1.0f - k1);
        setColor(shade(st.roof, (k & 1) ? 0.86f : 1.0f));
        quad(-h0 - 0.0f, y0, h0, y0, h1, y1, -h1, y1);
    }
    setColor(rgb(0.0f, 0.0f, 0.0f, 0.13f));
    tri(0.0f, 0.68f, 0.62f, 0.68f, 0.0f, 1.30f);                      // roof shade (right slope)
    setColor(shade(st.roof, 0.55f));
    rect(-0.64f, 0.66f, 0.64f, 0.71f);                                // eave

    // attic window
    setColor(rgb(0.95f, 0.92f, 0.85f));
    circle(0.0f, 0.90f, 0.085f, 20);
    setColor(rgb(0.45f, 0.70f, 0.88f));
    circle(0.0f, 0.90f, 0.065f, 20);
    setColor(rgb(0.95f, 0.92f, 0.85f));
    rect(-0.005f, 0.835f, 0.005f, 0.965f);
    rect(-0.065f, 0.895f, 0.065f, 0.905f);

    // windows
    drawWindow(-0.30f, 0.42f, 0.16f, 0.22f, false, st.shutter, true);
    drawWindow(0.30f, 0.42f, 0.16f, 0.22f, (style & 1) == 1, st.shutter, true);

    // door with arch, planks, knob, step
    setColor(rgb(0.95f, 0.92f, 0.85f));
    rect(-0.115f, 0.08f, 0.115f, 0.40f);
    circle(0.0f, 0.40f, 0.115f, 20);
    setColor(rgb(0.36f, 0.22f, 0.12f));
    rect(-0.09f, 0.08f, 0.09f, 0.40f);
    circle(0.0f, 0.40f, 0.09f, 20);
    setColor(rgb(0.26f, 0.15f, 0.08f));
    rect(-0.005f, 0.08f, 0.005f, 0.49f);
    rect(-0.05f, 0.08f, -0.045f, 0.42f);
    rect(0.045f, 0.08f, 0.05f, 0.42f);
    setColor(rgb(0.95f, 0.78f, 0.25f));
    circle(0.055f, 0.26f, 0.013f, 10);
    setColor(rgb(0.62f, 0.60f, 0.57f));
    rect(-0.15f, 0.045f, 0.15f, 0.085f);

    if (smoke) drawSmoke(0.27f, 1.26f, time);

    glPopMatrix();
}


// ===========================================================================
// The village seen from an elevated aerial angle (opening film, after the reveal).
// Everything is placed with world.h, so the world map shows the same layout.
// Painter's algorithm: buildings, trees, people and the train are collected into one list
// and drawn far -> near (larger y first) every frame.
// ===========================================================================
namespace
{
using world::depthScale;

const Color SKYB = rgb(0.55f, 0.80f, 1.0f);

// ---- small helpers ---------------------------------------------------------
void flowerDots(float x, float y, float w, float h, int n, uint32_t seed, float r = 0.035f)
{
    const Color fc[5] = {rgb(1.0f, 0.35f, 0.45f), rgb(1.0f, 0.82f, 0.25f), rgb(0.95f, 0.55f, 0.95f), rgb(1.0f, 1.0f, 1.0f), rgb(1.0f, 0.55f, 0.20f)};
    for (int k = 0; k < n; k++)
    {
        setColor(fc[(seed + k) % 5]);
        circle(x + w * cg::hashU(seed * 7u + k * 3u), y + h * cg::hashU(seed * 5u + k * 11u), r, 6);
    }
}

// striped awning in house units (x0..x1 at height y, hanging down by d)
void awning(float x0, float x1, float y, float d, const Color& a, const Color& b)
{
    const int n = 6;
    for (int i = 0; i < n; i++)
    {
        const float u0 = x0 + (x1 - x0) * i / n, u1 = x0 + (x1 - x0) * (i + 1) / n;
        setColor((i & 1) ? b : a);
        quad(u0, y, u1, y, u1 + 0.02f, y - d, u0 + 0.02f, y - d);
        circle((u0 + u1) * 0.5f + 0.02f, y - d, (u1 - u0) * 0.5f, 8);
    }
}

void gableRoof(float hw, float y0, float h, const Color& c)
{
    setColor(c);                     tri(-hw, y0, hw, y0, 0.0f, y0 + h);
    setColor(rgb(0, 0, 0, 0.14f));   tri(0.0f, y0, hw, y0, 0.0f, y0 + h);
    setColor(shade(c, 0.6f));        rect(-hw - 0.02f, y0 - 0.03f, hw + 0.02f, y0 + 0.01f);
}

void smallWindow(float cx, float cy, float w, float h, bool lit)
{
    setColor(rgb(0.95f, 0.92f, 0.85f)); rect(cx - w / 2 - 0.02f, cy - h / 2 - 0.02f, cx + w / 2 + 0.02f, cy + h / 2 + 0.02f);
    setColor(lit && theme::cur().windowsLit > 0.5f ? rgb(1.0f, 0.85f, 0.45f) : rgb(0.45f, 0.68f, 0.88f));
    rect(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
}

// ---- buildings (house units: width ~1, ground at 0) ------------------------
void drawShop(float x, float y, float w, int style, const Color& aw, double time)
{
    const Style& st = STYLES[style & 3];
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 0.70f, 0.08f);
    rectV(-0.48f, 0.0f, 0.48f, 0.72f, shade(st.wall, 0.88f), st.wall);
    setColor(rgb(0, 0, 0, 0.07f)); rect(0.30f, 0.0f, 0.48f, 0.72f);
    gableRoof(0.62f, 0.70f, 0.50f, st.roof);
    setColor(rgb(0.40f, 0.62f, 0.85f)); rect(-0.38f, 0.10f, 0.05f, 0.42f);            // big shop window
    setColor(rgb(1, 1, 1, 0.35f)); tri(-0.36f, 0.12f, -0.20f, 0.12f, -0.36f, 0.36f);
    flowerDots(-0.37f, 0.11f, 0.40f, 0.08f, 6, (uint32_t)(x * 100));                     // goods on display
    setColor(rgb(0.36f, 0.22f, 0.12f)); rect(0.14f, 0.0f, 0.36f, 0.40f);              // door
    awning(-0.46f, 0.44f, 0.52f, 0.10f, aw, rgb(1, 1, 1));
    setColor(shade(aw, 0.8f)); rect(-0.30f, 0.56f, 0.30f, 0.66f);                      // sign board
    setColor(rgb(1, 1, 1, 0.9f)); rect(-0.22f, 0.60f, 0.22f, 0.62f);
    (void)time;
    glPopMatrix();
}

void drawTavern(float x, float y, float w, double time)
{
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 0.85f, 0.09f);
    rectV(-0.62f, 0.0f, 0.62f, 1.05f, rgb(0.88f, 0.80f, 0.64f), rgb(0.98f, 0.92f, 0.78f));
    setColor(rgb(0.45f, 0.28f, 0.16f));                                                // timber frame
    rect(-0.62f, 0.50f, 0.62f, 0.55f); rect(-0.62f, 1.0f, 0.62f, 1.05f);
    for (float bx : {-0.62f, -0.21f, 0.21f, 0.57f}) rect(bx, 0.0f, bx + 0.05f, 1.05f);
    gableRoof(0.80f, 1.03f, 0.55f, rgb(0.52f, 0.30f, 0.22f));
    for (float wx : {-0.42f, 0.0f, 0.40f}) smallWindow(wx, 0.78f, 0.16f, 0.16f, true);
    smallWindow(-0.42f, 0.28f, 0.18f, 0.16f, true);
    smallWindow(0.40f, 0.28f, 0.18f, 0.16f, true);
    setColor(rgb(0.36f, 0.22f, 0.12f)); rect(-0.09f, 0.0f, 0.09f, 0.36f); circle(0.0f, 0.36f, 0.09f, 12);
    // hanging sign that swings
    const float sw = 0.12f * std::sin((float)time * 1.7f);
    setColor(rgb(0.30f, 0.20f, 0.12f)); rect(0.62f, 0.62f, 0.86f, 0.65f);
    glPushMatrix(); glTranslatef(0.80f, 0.62f, 0); glRotatef(sw * 57.3f, 0, 0, 1);
    setColor(rgb(0.95f, 0.72f, 0.20f)); rect(-0.10f, -0.26f, 0.10f, -0.04f);
    setColor(rgb(0.60f, 0.30f, 0.10f)); circle(0.0f, -0.15f, 0.05f, 10);
    glPopMatrix();
    // flower boxes
    for (float wx : {-0.42f, 0.40f}) { setColor(rgb(0.45f, 0.28f, 0.16f)); rect(wx - 0.12f, 0.66f, wx + 0.12f, 0.70f); flowerDots(wx - 0.12f, 0.70f, 0.24f, 0.04f, 5, (uint32_t)(wx * 50 + 9)); }
    glPopMatrix();
}

void star5(float x, float y, float r)
{
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x, y);
    for (int i = 0; i <= 10; i++) { const float a = 1.5708f + 6.2832f * i / 10.0f, rr = (i & 1) ? r * 0.45f : r; glVertex2f(x + std::cos(a) * rr, y + std::sin(a) * rr); }
    glEnd();
}

// light bulb that glows in the evening / at night (additive)
void bulb(float x, float y, float r, const Color& c, bool lit)
{
    setColor(lit ? mix(c, rgb(1, 1, 1), 0.4f) : shade(c, 0.85f));
    circle(x, y, r, 8);
    if (lit)
    {
        const bool old = gfx::useTint; gfx::useTint = false;
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glow(x, y, r * 4.0f, withAlpha(c, 0.35f), 12);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gfx::useTint = old;
    }
}
bool eveningLights() { return theme::cur().windowsLit > 0.5f; }

// THE village landmark: a tall clock tower with a bell in an open belfry, on a small hall
void drawClockTower(float x, float y, float w, double time)
{
    const bool lit = eveningLights();
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.20f)); ellipse(0, 0, 1.15f, 0.12f);
    // hall at the base with columns
    rectV(-0.95f, 0.0f, 0.95f, 0.75f, rgb(0.84f, 0.78f, 0.68f), rgb(0.97f, 0.93f, 0.85f));
    setColor(rgb(0.70f, 0.30f, 0.24f)); quad(-1.05f, 0.73f, 1.05f, 0.73f, 0.85f, 0.98f, -0.85f, 0.98f);
    setColor(rgb(1, 1, 1)); for (float cx = -0.80f; cx <= 0.81f; cx += 0.26f) if (std::fabs(cx) > 0.3f) rect(cx - 0.04f, 0.06f, cx + 0.04f, 0.66f);
    setColor(rgb(0.80f, 0.74f, 0.66f)); rect(-0.98f, 0.0f, 0.98f, 0.06f); rect(-0.98f, 0.64f, 0.98f, 0.70f);
    // the tower
    rectV(-0.36f, 0.0f, 0.36f, 3.30f, rgb(0.82f, 0.74f, 0.62f), rgb(0.96f, 0.90f, 0.80f));
    setColor(rgb(0, 0, 0, 0.10f)); rect(0.14f, 0.0f, 0.36f, 3.30f);
    setColor(rgb(0.70f, 0.62f, 0.52f)); for (float yy : {0.80f, 1.70f, 2.10f, 2.95f}) rect(-0.40f, yy, 0.40f, yy + 0.07f);
    setColor(rgb(0.36f, 0.22f, 0.12f)); rect(-0.14f, 0.0f, 0.14f, 0.48f); circle(0.0f, 0.48f, 0.14f, 16);       // big door
    for (float yy : {1.05f, 1.40f}) { setColor(rgb(0.30f, 0.36f, 0.55f)); rect(-0.07f, yy, 0.07f, yy + 0.22f); circle(0.0f, yy + 0.22f, 0.07f, 10); }
    // clock face (real local time) - glows at night
    const float cy = 2.52f, R = 0.30f;
    if (lit) { const bool o = gfx::useTint; gfx::useTint = false; glBlendFunc(GL_SRC_ALPHA, GL_ONE); glow(0.0f, cy, 0.75f, rgb(1.0f, 0.90f, 0.55f, 0.45f), 24); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); gfx::useTint = o; }
    setColor(rgb(0.85f, 0.68f, 0.25f)); circle(0.0f, cy, R + 0.05f, 32);
    setColor(lit ? rgb(1.0f, 0.97f, 0.82f) : rgb(0.99f, 0.97f, 0.92f)); circle(0.0f, cy, R, 32);
    setColor(rgb(0.25f, 0.20f, 0.18f));
    for (int k = 0; k < 12; k++) { const float a = 6.2832f * k / 12.0f; rect(std::cos(a) * R * 0.82f - 0.015f, cy + std::sin(a) * R * 0.82f - 0.015f, std::cos(a) * R * 0.82f + 0.015f, cy + std::sin(a) * R * 0.82f + 0.015f); }
    const float hrs = (float)std::fmod(theme::localHour(), 12.0f), mn = (theme::localHour() - std::floor(theme::localHour())) * 60.0f;
    const float ah = 1.5708f - hrs / 12.0f * 6.2832f, am = 1.5708f - mn / 60.0f * 6.2832f;
    thickLine(0.0f, cy, 0.16f * std::cos(ah), cy + 0.16f * std::sin(ah), 0.045f);
    thickLine(0.0f, cy, 0.25f * std::cos(am), cy + 0.25f * std::sin(am), 0.028f);
    circle(0.0f, cy, 0.03f, 10);
    // open belfry with the bell (swings gently for a few seconds at every full hour)
    setColor(rgb(0.30f, 0.24f, 0.22f)); rect(-0.24f, 3.02f, 0.24f, 3.30f); circle(0.0f, 3.30f, 0.24f, 20);
    const float minute = mn, swing = (minute < 0.25f || minute > 59.9f) ? 0.35f * std::sin((float)time * 6.0f) : 0.04f * std::sin((float)time * 1.2f);
    glPushMatrix(); glTranslatef(0.0f, 3.42f, 0); glRotatef(swing * 57.3f, 0, 0, 1);
    setColor(rgb(0.90f, 0.72f, 0.22f)); quad(-0.10f, -0.08f, 0.10f, -0.08f, 0.17f, -0.32f, -0.17f, -0.32f); circle(0.0f, -0.08f, 0.10f, 12);
    setColor(rgb(0.70f, 0.52f, 0.12f)); rect(-0.18f, -0.34f, 0.18f, -0.30f); circle(0.0f, -0.37f, 0.03f, 8);
    glPopMatrix();
    setColor(rgb(0.82f, 0.74f, 0.62f)); rect(-0.40f, 3.55f, 0.40f, 3.66f);
    // spire with a golden finial and a flag
    setColor(rgb(0.22f, 0.36f, 0.62f)); tri(-0.44f, 3.64f, 0.44f, 3.64f, 0.0f, 4.75f);
    setColor(rgb(0, 0, 0, 0.18f)); tri(0.0f, 3.64f, 0.44f, 3.64f, 0.0f, 4.75f);
    setColor(rgb(0.95f, 0.78f, 0.20f)); circle(0.0f, 4.78f, 0.05f, 10);
    setColor(rgb(0.30f, 0.20f, 0.15f)); rect(-0.012f, 4.78f, 0.012f, 5.15f);
    const float fl = 0.05f * std::sin((float)time * 4.0f);
    setColor(rgb(0.95f, 0.25f, 0.30f)); tri(0.012f, 5.14f, 0.012f, 4.96f, 0.40f + fl, 5.05f);
    glPopMatrix();
}

// ---- the fair ------------------------------------------------------------------
void drawFerrisWheel(float x, float y, float w, float t)
{
    const bool lit = eveningLights();
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 0.9f, 0.10f);
    const float hy = 1.25f, R = 1.05f;
    setColor(rgb(0.85f, 0.88f, 0.95f)); thickLine(-0.55f, 0.0f, 0.0f, hy, 0.07f); thickLine(0.55f, 0.0f, 0.0f, hy, 0.07f);
    const float rot = -t * 0.25f;
    setColor(rgb(0.95f, 0.95f, 1.0f)); ring(0.0f, hy, R, R, 48); ring(0.0f, hy, R * 0.96f, R * 0.96f, 48);
    for (int k = 0; k < 12; k++) { const float a = rot + 6.2832f * k / 12.0f; thickLine(0.0f, hy, std::cos(a) * R, hy + std::sin(a) * R, 0.018f); }
    const Color gc[6] = {rgb(1.0f, 0.35f, 0.35f), rgb(1.0f, 0.80f, 0.25f), rgb(0.40f, 0.75f, 1.0f), rgb(0.55f, 0.85f, 0.45f), rgb(0.90f, 0.50f, 0.95f), rgb(1.0f, 0.55f, 0.25f)};
    for (int k = 0; k < 12; k++)                                  // gondolas hang straight down (they rotate back)
    {
        const float a = rot + 6.2832f * (k + 0.5f) / 12.0f, gx = std::cos(a) * R, gy = hy + std::sin(a) * R;
        setColor(rgb(0.4f, 0.4f, 0.45f)); rect(gx - 0.01f, gy - 0.10f, gx + 0.01f, gy);
        setColor(gc[k % 6]); quad(gx - 0.10f, gy - 0.10f, gx + 0.10f, gy - 0.10f, gx + 0.08f, gy - 0.26f, gx - 0.08f, gy - 0.26f);
        setColor(rgb(1, 1, 1, 0.6f)); rect(gx - 0.10f, gy - 0.12f, gx + 0.10f, gy - 0.10f);
    }
    for (int k = 0; k < 24; k++) { const float a = rot + 6.2832f * k / 24.0f; bulb(std::cos(a) * R, hy + std::sin(a) * R, 0.025f, gc[k % 6], lit); }
    setColor(rgb(0.95f, 0.75f, 0.20f)); circle(0.0f, hy, 0.09f, 14);
    glPopMatrix();
}

void drawCarousel(float x, float y, float w, float t)
{
    const bool lit = eveningLights();
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 0.95f, 0.14f);
    setColor(rgb(0.85f, 0.30f, 0.35f)); ellipse(0.0f, 0.06f, 0.85f, 0.16f, 28);
    setColor(rgb(0.98f, 0.88f, 0.60f)); ellipse(0.0f, 0.09f, 0.82f, 0.14f, 28);
    const Color hc[4] = {rgb(1, 1, 1), rgb(0.95f, 0.80f, 0.40f), rgb(0.70f, 0.50f, 0.35f), rgb(0.85f, 0.85f, 0.95f)};
    for (int pass = 0; pass < 2; pass++)                          // back half first, then the front half
        for (int k = 0; k < 8; k++)
        {
            const float a = t * 0.9f + 6.2832f * k / 8.0f, sn = std::sin(a);
            if ((pass == 0) != (sn > 0)) continue;
            const float hx = std::cos(a) * 0.62f, hz = sn * 0.10f, bob = 0.08f * std::sin(t * 3.0f + k);
            setColor(rgb(0.95f, 0.80f, 0.30f)); rect(hx - 0.015f, 0.10f - hz, hx + 0.015f, 0.95f);
            setColor(hc[k % 4]); ellipse(hx, 0.42f - hz + bob, 0.15f, 0.07f, 12); circle(hx + (std::sin(a) > 0 ? -0.13f : 0.13f) * (std::cos(a) > 0 ? -1 : 1), 0.50f - hz + bob, 0.05f, 8);
        }
    setColor(rgb(0.95f, 0.80f, 0.30f)); rect(-0.05f, 0.10f, 0.05f, 0.95f);
    // striped canopy
    for (int i = 0; i < 8; i++)
    {
        const float x0 = -0.95f + 0.2375f * i, x1 = x0 + 0.2375f;
        setColor(i & 1 ? rgb(1, 1, 1) : rgb(0.90f, 0.25f, 0.30f));
        tri(x0, 0.95f, x1, 0.95f, 0.0f, 1.45f);
        circle((x0 + x1) * 0.5f, 0.95f, 0.11f, 10);
    }
    for (int i = 0; i <= 8; i++) bulb(-0.95f + 0.2375f * i, 0.90f, 0.03f, rgb(1.0f, 0.85f, 0.40f), lit);
    setColor(rgb(0.95f, 0.75f, 0.20f)); circle(0.0f, 1.48f, 0.06f, 10);
    glPopMatrix();
}

void drawBigTop(float x, float y, float w, float t)
{
    const bool lit = eveningLights();
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 1.25f, 0.12f);
    for (int i = 0; i < 10; i++)                                  // tent wall stripes
    {
        const float x0 = -1.1f + 0.22f * i;
        setColor(i & 1 ? rgb(1.0f, 0.97f, 0.92f) : rgb(0.90f, 0.20f, 0.25f));
        rect(x0, 0.0f, x0 + 0.22f, 0.55f);
    }
    for (int i = 0; i < 10; i++)                                  // conical roof (two peaks)
    {
        const float x0 = -1.2f + 0.24f * i;
        setColor(i & 1 ? rgb(1.0f, 0.97f, 0.92f) : rgb(0.90f, 0.20f, 0.25f));
        const float px = i < 5 ? -0.45f : 0.45f;
        tri(x0, 0.55f, x0 + 0.24f, 0.55f, px, 1.25f);
    }
    setColor(rgb(0.25f, 0.18f, 0.30f)); quad(-0.18f, 0.0f, 0.18f, 0.0f, 0.10f, 0.42f, -0.10f, 0.42f);     // entrance
    setColor(rgb(0.95f, 0.80f, 0.25f)); for (float px : {-0.45f, 0.45f}) { rect(px - 0.01f, 1.25f, px + 0.01f, 1.50f);
        const float fl = 0.04f * std::sin(t * 4.0f + px); tri(px + 0.01f, 1.50f, px + 0.01f, 1.36f, px + 0.28f + fl, 1.43f); }
    for (int i = 0; i < 11; i++) bulb(-1.1f + 0.22f * i, 0.57f, 0.03f, i & 1 ? rgb(1.0f, 0.9f, 0.4f) : rgb(1.0f, 0.4f, 0.4f), lit);
    glPopMatrix();
}

void drawChairSwing(float x, float y, float w, float t)
{
    const bool lit = eveningLights();
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 0.8f, 0.10f);
    setColor(rgb(0.30f, 0.55f, 0.90f)); rect(-0.06f, 0.0f, 0.06f, 1.40f);
    const float spin = t * 1.6f;
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < 10; k++)
        {
            const float a = spin + 6.2832f * k / 10.0f, sn = std::sin(a);
            if ((pass == 0) != (sn > 0)) continue;
            const float sx = std::cos(a) * 0.80f, sy = 0.65f - sn * 0.10f;
            setColor(rgb(0.6f, 0.6f, 0.65f)); thickLine(std::cos(a) * 0.45f, 1.30f - sn * 0.06f, sx, sy + 0.05f, 0.012f);
            setColor(k & 1 ? rgb(1.0f, 0.80f, 0.25f) : rgb(0.95f, 0.35f, 0.40f)); rect(sx - 0.06f, sy - 0.04f, sx + 0.06f, sy + 0.04f);
        }
    setColor(rgb(0.95f, 0.80f, 0.30f)); ellipse(0.0f, 1.38f, 0.55f, 0.10f, 20);
    setColor(rgb(0.30f, 0.55f, 0.90f)); tri(-0.55f, 1.40f, 0.55f, 1.40f, 0.0f, 1.70f);
    for (int k = 0; k < 8; k++) bulb(-0.5f + k / 7.0f, 1.38f, 0.03f, rgb(1.0f, 0.95f, 0.5f), lit);
    glPopMatrix();
}

void drawHelterSkelter(float x, float y, float w, float t)
{
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 0.6f, 0.08f);
    for (int i = 0; i < 6; i++) { setColor(i & 1 ? rgb(1, 1, 1) : rgb(0.25f, 0.55f, 0.95f)); quad(-0.30f + 0.02f * i, 0.30f * i, 0.30f - 0.02f * i, 0.30f * i, 0.28f - 0.02f * i, 0.30f * (i + 1), -0.28f + 0.02f * i, 0.30f * (i + 1)); }
    setColor(rgb(0.95f, 0.30f, 0.30f)); tri(-0.32f, 1.80f, 0.32f, 1.80f, 0.0f, 2.30f);
    setColor(rgb(1.0f, 0.80f, 0.25f));                              // the spiral slide (front half of each turn)
    for (int i = 0; i < 4; i++) { const float y0 = 1.65f - 0.42f * i; quad(-0.42f, y0, 0.42f, y0 - 0.30f, 0.42f, y0 - 0.20f, -0.42f, y0 + 0.10f); }
    const float u = fract(t * 0.25f), ky = 1.70f - 1.65f * u, kx = 0.38f * std::sin(u * 25.0f);
    setColor(rgb(0.9f, 0.3f, 0.5f)); circle(kx, ky, 0.07f, 8); setColor(rgb(0.98f, 0.82f, 0.68f)); circle(kx, ky + 0.10f, 0.05f, 8);
    glPopMatrix();
}

void drawStage(float x, float y, float w, float t)
{
    const bool lit = eveningLights();
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 0.9f, 0.10f);
    setColor(rgb(0.55f, 0.36f, 0.20f)); rect(-0.80f, 0.0f, 0.80f, 0.25f);
    setColor(rgb(0.70f, 0.20f, 0.30f)); rect(-0.80f, 0.25f, -0.70f, 1.10f); rect(0.70f, 0.25f, 0.80f, 1.10f);
    setColor(rgb(0.85f, 0.25f, 0.35f)); quad(-0.90f, 1.10f, 0.90f, 1.10f, 0.75f, 1.35f, -0.75f, 1.35f);
    for (int i = 0; i < 7; i++) bulb(-0.70f + i * 0.233f, 1.08f, 0.03f, rgb(1.0f, 0.9f, 0.4f), lit);
    const Color mc[3] = {rgb(0.95f, 0.75f, 0.20f), rgb(0.30f, 0.60f, 0.95f), rgb(0.45f, 0.80f, 0.40f)};
    for (int m = 0; m < 3; m++)                                     // the band: bobbing to the music
    {
        const float mx = -0.40f + m * 0.40f, b = 0.04f * std::fabs(std::sin(t * 4.0f + m));
        setColor(mc[m]); rect(mx - 0.09f, 0.25f + b, mx + 0.09f, 0.60f + b);
        setColor(rgb(0.98f, 0.82f, 0.68f)); circle(mx, 0.70f + b, 0.08f, 10);
        setColor(rgb(0.85f, 0.65f, 0.25f)); thickLine(mx + 0.05f, 0.45f + b, mx + 0.22f, 0.55f + b, 0.04f);
    }
    for (int n = 0; n < 3; n++)                                      // music notes drifting up
    {
        const float ph = fract(t * 0.5f + n / 3.0f);
        setColor(rgb(0.15f, 0.15f, 0.25f, 1.0f - ph)); circle(-0.3f + 0.3f * n + 0.1f * std::sin(ph * 6.0f), 1.0f + 0.8f * ph, 0.05f, 8);
        rect(-0.3f + 0.3f * n + 0.1f * std::sin(ph * 6.0f) + 0.04f, 1.0f + 0.8f * ph, -0.3f + 0.3f * n + 0.1f * std::sin(ph * 6.0f) + 0.055f, 1.20f + 0.8f * ph);
    }
    glPopMatrix();
}

void drawFairArch(float x, float y, float w, float t)
{
    const bool lit = eveningLights();
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0.85f, 0.25f, 0.30f)); rect(-0.65f, 0.0f, -0.55f, 1.0f); rect(0.55f, 0.0f, 0.65f, 1.0f);
    setColor(rgb(0.98f, 0.82f, 0.30f));
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= 20; i++) { const float a = 3.1416f * i / 20.0f; glVertex2f(std::cos(a) * 0.62f, 1.0f + std::sin(a) * 0.30f); glVertex2f(std::cos(a) * 0.50f, 1.0f + std::sin(a) * 0.20f); }
    glEnd();
    setColor(rgb(0.85f, 0.25f, 0.30f)); rect(-0.42f, 0.92f, 0.42f, 1.12f);
    setColor(rgb(1, 1, 1)); for (int k = 0; k < 4; k++) { const float cx = -0.27f + 0.18f * k; star5(cx, 1.02f, 0.06f); }
    for (int i = 0; i <= 10; i++) { const float a = 3.1416f * i / 10.0f; bulb(std::cos(a) * 0.56f, 1.0f + std::sin(a) * 0.25f, 0.03f, (i + (int)(t * 3)) & 1 ? rgb(1.0f, 0.9f, 0.4f) : rgb(1.0f, 0.5f, 0.5f), lit); }
    glPopMatrix();
}

// ---- the establishments (future scenes) -----------------------------------------
void signBoard(float cx, float cy, float w, float h, const Color& c)
{
    setColor(shade(c, 0.6f)); rect(cx - w / 2 - 0.02f, cy - h / 2 - 0.02f, cx + w / 2 + 0.02f, cy + h / 2 + 0.02f);
    setColor(c); rect(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
}

void drawPolice(float x, float y, float w, float t)
{
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 0.95f, 0.10f);
    rectV(-0.80f, 0.0f, 0.80f, 0.95f, rgb(0.70f, 0.76f, 0.86f), rgb(0.86f, 0.90f, 0.96f));
    setColor(rgb(0.20f, 0.30f, 0.60f)); rect(-0.86f, 0.92f, 0.86f, 1.05f);                       // flat roof band
    signBoard(0.0f, 0.80f, 0.90f, 0.14f, rgb(0.18f, 0.32f, 0.70f));
    setColor(rgb(1, 1, 1)); for (int k = 0; k < 6; k++) rect(-0.36f + k * 0.13f, 0.77f, -0.30f + k * 0.13f, 0.83f);   // "POLICE" lettering
    for (float wx : {-0.55f, -0.30f, 0.30f, 0.55f}) smallWindow(wx, 0.45f, 0.16f, 0.20f, true);
    setColor(rgb(0.15f, 0.25f, 0.50f)); rect(-0.12f, 0.0f, 0.12f, 0.45f);
    // blue lamp above the door
    const bool lit = eveningLights();
    bulb(0.0f, 0.58f, 0.05f, rgb(0.30f, 0.55f, 1.0f), lit || std::fmod(t, 2.0f) < 1.0f);
    // police car
    glTranslatef(0.95f, -0.10f, 0);
    setColor(rgb(1, 1, 1)); rect(-0.30f, 0.05f, 0.30f, 0.22f); quad(-0.18f, 0.22f, 0.15f, 0.22f, 0.10f, 0.34f, -0.12f, 0.34f);
    setColor(rgb(0.15f, 0.30f, 0.70f)); rect(-0.30f, 0.11f, 0.30f, 0.15f);
    setColor(rgb(0.55f, 0.75f, 0.95f)); rect(-0.10f, 0.24f, 0.08f, 0.32f);
    const bool on = std::fmod(t * 3.0f, 1.0f) < 0.5f;
    setColor(on ? rgb(0.2f, 0.4f, 1.0f) : rgb(1.0f, 0.2f, 0.2f)); rect(-0.06f, 0.34f, 0.04f, 0.38f);
    setColor(rgb(0.12f, 0.12f, 0.14f)); circle(-0.18f, 0.05f, 0.06f, 10); circle(0.18f, 0.05f, 0.06f, 10);
    glPopMatrix();
}

void drawDetective(float x, float y, float w, float t)
{
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 0.70f, 0.08f);
    rectV(-0.50f, 0.0f, 0.50f, 1.30f, rgb(0.20f, 0.36f, 0.30f), rgb(0.28f, 0.46f, 0.38f));
    setColor(rgb(0.35f, 0.22f, 0.20f)); tri(-0.62f, 1.28f, 0.62f, 1.28f, 0.0f, 1.80f);
    setColor(rgb(0, 0, 0, 0.15f)); tri(0.0f, 1.28f, 0.62f, 1.28f, 0.0f, 1.80f);
    setColor(rgb(0.80f, 0.70f, 0.50f)); rect(-0.52f, 0.62f, 0.52f, 0.68f);
    smallWindow(-0.25f, 0.95f, 0.18f, 0.22f, true); smallWindow(0.25f, 0.95f, 0.18f, 0.22f, false);
    setColor(rgb(0.95f, 0.85f, 0.50f)); quad(-0.45f, 0.15f, -0.05f, 0.15f, -0.08f, 0.50f, -0.42f, 0.50f);   // bay window
    setColor(rgb(0.20f, 0.36f, 0.30f)); rect(-0.26f, 0.15f, -0.24f, 0.50f);
    setColor(rgb(0.18f, 0.12f, 0.08f)); rect(0.10f, 0.0f, 0.34f, 0.48f);
    // hanging sign: a magnifying glass
    setColor(rgb(0.30f, 0.20f, 0.12f)); rect(0.50f, 0.78f, 0.80f, 0.80f);
    glPushMatrix(); glTranslatef(0.72f, 0.78f, 0); glRotatef(6.0f * std::sin(t * 1.3f), 0, 0, 1);
    setColor(rgb(0.95f, 0.88f, 0.65f)); rect(-0.14f, -0.32f, 0.14f, -0.04f);
    setColor(rgb(0.25f, 0.20f, 0.18f)); ring(-0.02f, -0.15f, 0.06f, 0.06f, 14); thickLine(0.02f, -0.19f, 0.08f, -0.26f, 0.025f);
    glPopMatrix();
    glPopMatrix();
    // the detective at the door: long coat + deerstalker hat, looking around
    const float s = w * 0.85f, px = x + 0.55f * w, dir = std::sin(t * 0.5f) > 0 ? 1.0f : -1.0f;
    setColor(rgb(0.55f, 0.42f, 0.28f)); quad(px - 0.10f * s, y, px + 0.10f * s, y, px + 0.07f * s, y + 0.42f * s, px - 0.07f * s, y + 0.42f * s);
    setColor(rgb(0.98f, 0.82f, 0.68f)); circle(px, y + 0.50f * s, 0.07f * s, 10);
    setColor(rgb(0.50f, 0.38f, 0.24f)); ellipse(px, y + 0.56f * s, 0.10f * s, 0.035f * s, 10); circle(px, y + 0.58f * s, 0.055f * s, 10);
    setColor(rgb(0.25f, 0.20f, 0.18f)); ring(px + dir * 0.12f * s, y + 0.40f * s, 0.035f * s, 0.035f * s, 10);
}

void drawFireStation(float x, float y, float w, float t)
{
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 0.95f, 0.10f);
    rectV(-0.80f, 0.0f, 0.55f, 0.90f, rgb(0.70f, 0.24f, 0.20f), rgb(0.85f, 0.34f, 0.28f));
    rectV(0.55f, 0.0f, 0.85f, 1.70f, rgb(0.66f, 0.22f, 0.18f), rgb(0.80f, 0.30f, 0.25f));         // hose tower
    setColor(rgb(0.30f, 0.20f, 0.18f)); quad(0.50f, 1.70f, 0.90f, 1.70f, 0.80f, 1.85f, 0.60f, 1.85f);
    setColor(rgb(0.95f, 0.80f, 0.25f)); circle(0.70f, 1.45f, 0.08f, 12);                            // the bell
    setColor(rgb(0.95f, 0.92f, 0.86f)); rect(-0.82f, 0.86f, 0.57f, 0.96f);
    for (int d = 0; d < 2; d++)                                                                     // garage doors
    {
        const float dx = -0.68f + d * 0.62f;
        setColor(rgb(0.95f, 0.92f, 0.86f)); rect(dx - 0.02f, 0.0f, dx + 0.52f, 0.66f);
        setColor(rgb(0.25f, 0.22f, 0.24f)); rect(dx, 0.0f, dx + 0.50f, 0.64f);
        if (d == 0) { setColor(rgb(0.90f, 0.15f, 0.12f)); rect(dx + 0.06f, 0.05f, dx + 0.44f, 0.42f);             // fire truck front
            setColor(rgb(0.55f, 0.75f, 0.95f)); rect(dx + 0.10f, 0.26f, dx + 0.40f, 0.38f);
            setColor(std::fmod(t * 2.0f, 1.0f) < 0.5f ? rgb(1, 0.3f, 0.2f) : rgb(1, 0.8f, 0.2f)); rect(dx + 0.18f, 0.42f, dx + 0.32f, 0.47f); }
    }
    glPopMatrix();
}

void drawSchool(float x, float y, float w, float t)
{
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 1.0f, 0.10f);
    rectV(-0.90f, 0.0f, 0.90f, 0.85f, rgb(0.92f, 0.80f, 0.45f), rgb(0.98f, 0.90f, 0.60f));
    gableRoof(1.02f, 0.83f, 0.45f, rgb(0.30f, 0.50f, 0.75f));
    setColor(rgb(0.95f, 0.92f, 0.86f)); rect(-0.14f, 1.20f, 0.14f, 1.48f);                         // bell cupola
    setColor(rgb(0.95f, 0.78f, 0.25f)); circle(0.0f, 1.32f, 0.07f, 10);
    setColor(rgb(0.30f, 0.50f, 0.75f)); tri(-0.20f, 1.48f, 0.20f, 1.48f, 0.0f, 1.70f);
    setColor(rgb(0.98f, 0.96f, 0.90f)); circle(0.0f, 1.00f, 0.11f, 16);                           // clock
    setColor(rgb(0.2f, 0.2f, 0.2f)); rect(-0.006f, 1.0f, 0.006f, 1.08f); rect(0.0f, 0.995f, 0.06f, 1.005f);
    for (float wx : {-0.65f, -0.38f, 0.38f, 0.65f}) for (float wy : {0.25f, 0.58f}) smallWindow(wx, wy, 0.16f, 0.18f, wy < 0.4f);
    setColor(rgb(0.30f, 0.50f, 0.75f)); rect(-0.14f, 0.0f, 0.14f, 0.45f);
    setColor(rgb(0.3f, 0.2f, 0.15f)); rect(1.05f, 0.0f, 1.07f, 1.30f);                             // flag
    setColor(rgb(0.20f, 0.65f, 0.35f)); tri(1.07f, 1.30f, 1.07f, 1.12f, 1.40f + 0.05f * std::sin(t * 4.0f), 1.21f);
    glPopMatrix();
}

void drawLibrary(float x, float y, float w)
{
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 0.85f, 0.09f);
    rectV(-0.75f, 0.0f, 0.75f, 0.80f, rgb(0.80f, 0.78f, 0.74f), rgb(0.94f, 0.92f, 0.88f));
    setColor(rgb(0.70f, 0.66f, 0.60f)); rect(-0.82f, 0.0f, 0.82f, 0.08f); rect(-0.82f, 0.78f, 0.82f, 0.86f);
    setColor(rgb(0.88f, 0.85f, 0.80f)); tri(-0.85f, 0.86f, 0.85f, 0.86f, 0.0f, 1.25f);                 // pediment
    setColor(rgb(0.55f, 0.30f, 0.25f)); rect(-0.10f, 0.98f, 0.10f, 1.10f); setColor(rgb(0.95f, 0.92f, 0.85f)); rect(-0.005f, 0.98f, 0.005f, 1.10f);   // book
    setColor(rgb(1, 1, 1)); for (float cx = -0.60f; cx <= 0.61f; cx += 0.24f) rect(cx - 0.04f, 0.08f, cx + 0.04f, 0.76f);
    setColor(rgb(0.36f, 0.22f, 0.12f)); rect(-0.10f, 0.08f, 0.10f, 0.45f);
    glPopMatrix();
}

void drawPostOffice(float x, float y, float w)
{
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 0.75f, 0.08f);
    rectV(-0.60f, 0.0f, 0.60f, 0.80f, rgb(0.88f, 0.84f, 0.78f), rgb(0.98f, 0.95f, 0.90f));
    gableRoof(0.72f, 0.78f, 0.42f, rgb(0.80f, 0.20f, 0.18f));
    signBoard(0.0f, 0.68f, 0.70f, 0.13f, rgb(0.85f, 0.20f, 0.18f));
    setColor(rgb(1, 1, 1)); rect(-0.12f, 0.64f, 0.12f, 0.72f);
    setColor(rgb(0.70f, 0.15f, 0.12f)); tri(-0.12f, 0.72f, 0.12f, 0.72f, 0.0f, 0.67f);                 // envelope
    smallWindow(-0.35f, 0.32f, 0.20f, 0.22f, true); smallWindow(0.35f, 0.32f, 0.20f, 0.22f, false);
    setColor(rgb(0.36f, 0.22f, 0.12f)); rect(-0.10f, 0.0f, 0.10f, 0.42f);
    setColor(rgb(0.85f, 0.15f, 0.12f)); rect(0.70f, 0.0f, 0.86f, 0.36f); circle(0.78f, 0.36f, 0.08f, 12);   // mailbox
    glPopMatrix();
}

void drawHospitalBuilding(float x, float y, float w, double time)
{
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 1.05f, 0.10f);
    rectV(-0.95f, 0.0f, 0.95f, 0.80f, rgb(0.86f, 0.90f, 0.94f), rgb(0.98f, 0.99f, 1.0f));       // wings
    rectV(-0.38f, 0.0f, 0.38f, 1.25f, rgb(0.88f, 0.92f, 0.96f), rgb(1.0f, 1.0f, 1.0f));          // centre block
    setColor(rgb(0.25f, 0.62f, 0.66f)); rect(-0.98f, 0.78f, 0.98f, 0.86f); rect(-0.42f, 1.22f, 0.42f, 1.30f);
    for (int r = 0; r < 2; r++) for (float wx : {-0.80f, -0.60f, 0.60f, 0.80f}) smallWindow(wx, 0.24f + r * 0.32f, 0.12f, 0.14f, r == 0);
    for (float wx : {-0.20f, 0.20f}) smallWindow(wx, 0.92f, 0.14f, 0.14f, false);
    setColor(rgb(1, 1, 1)); rect(-0.14f, 1.32f, 0.14f, 1.62f);                                   // red cross sign
    const float p = 0.85f + 0.15f * std::sin((float)time * 2.5f);
    setColor(rgb(0.90f * p, 0.12f, 0.15f)); rect(-0.035f, 1.36f, 0.035f, 1.58f); rect(-0.11f, 1.435f, 0.11f, 1.505f);
    setColor(rgb(0.55f, 0.75f, 0.90f)); rect(-0.20f, 0.0f, 0.20f, 0.42f);                        // glass doors
    setColor(rgb(1, 1, 1, 0.5f)); rect(-0.01f, 0.0f, 0.01f, 0.42f);
    setColor(rgb(0.25f, 0.62f, 0.66f)); rect(-0.28f, 0.42f, 0.28f, 0.50f);
    // ambulance parked in front
    glTranslatef(0.70f, -0.12f, 0);
    setColor(rgb(1, 1, 1)); rect(-0.20f, 0.03f, 0.20f, 0.22f); setColor(rgb(0.90f, 0.15f, 0.15f)); rect(-0.20f, 0.10f, 0.20f, 0.13f);
    setColor(rgb(0.50f, 0.75f, 0.95f)); rect(0.10f, 0.14f, 0.19f, 0.21f);
    setColor(fract((float)time * 1.5f) < 0.5f ? rgb(0.2f, 0.5f, 1.0f) : rgb(1.0f, 0.2f, 0.2f)); rect(-0.04f, 0.22f, 0.04f, 0.26f);
    setColor(rgb(0.15f, 0.15f, 0.18f)); circle(-0.12f, 0.03f, 0.04f, 10); circle(0.12f, 0.03f, 0.04f, 10);
    glPopMatrix();
}

void drawStationBuilding(float x, float y, float w)
{
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0.72f, 0.70f, 0.66f)); rect(-1.30f, -0.10f, 1.30f, 0.04f);                       // platform
    setColor(rgb(0.95f, 0.85f, 0.30f)); rect(-1.30f, -0.10f, 1.30f, -0.07f);
    rectV(-0.60f, 0.04f, 0.60f, 0.70f, rgb(0.78f, 0.52f, 0.40f), rgb(0.90f, 0.64f, 0.50f));      // brick building
    for (float wx : {-0.40f, -0.14f, 0.14f, 0.40f}) { setColor(rgb(0.95f, 0.92f, 0.85f)); rect(wx - 0.08f, 0.22f, wx + 0.08f, 0.52f); circle(wx, 0.52f, 0.08f, 10);
                                                     setColor(rgb(0.45f, 0.65f, 0.85f)); rect(wx - 0.055f, 0.24f, wx + 0.055f, 0.50f); circle(wx, 0.50f, 0.055f, 10); }
    setColor(rgb(0.25f, 0.35f, 0.55f)); quad(-1.25f, 0.70f, 1.25f, 0.70f, 1.05f, 0.92f, -1.05f, 0.92f);   // long canopy roof
    setColor(rgb(0, 0, 0, 0.15f)); quad(-1.25f, 0.70f, 1.25f, 0.70f, 1.22f, 0.66f, -1.22f, 0.66f);
    setColor(rgb(0.30f, 0.30f, 0.34f)); for (float px : {-1.15f, -0.85f, 0.85f, 1.15f}) rect(px - 0.02f, 0.04f, px + 0.02f, 0.70f);
    setColor(rgb(0.98f, 0.96f, 0.90f)); circle(0.0f, 0.82f, 0.08f, 16);                          // platform clock
    setColor(rgb(0.2f, 0.2f, 0.2f)); rect(-0.004f, 0.82f, 0.004f, 0.88f); rect(0.0f, 0.818f, 0.05f, 0.826f);
    glPopMatrix();
}

void drawWindmillV(float x, float y, float w, double time)
{
    glPushMatrix(); glTranslatef(x, y, 0); glScalef(w, w, 1);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0, 0, 0.45f, 0.06f);
    setColor(rgb(0.95f, 0.88f, 0.76f)); quad(-0.30f, 0.0f, 0.30f, 0.0f, 0.18f, 1.30f, -0.18f, 1.30f);
    setColor(rgb(0, 0, 0, 0.10f)); quad(0.05f, 0.0f, 0.30f, 0.0f, 0.18f, 1.30f, 0.05f, 1.30f);
    setColor(rgb(0.78f, 0.25f, 0.20f)); tri(-0.26f, 1.28f, 0.26f, 1.28f, 0.0f, 1.62f);
    setColor(rgb(0.40f, 0.26f, 0.16f)); rect(-0.08f, 0.0f, 0.08f, 0.26f);
    glTranslatef(0.0f, 1.32f, 0); glRotatef(-(float)time * 40.0f, 0, 0, 1);
    for (int k = 0; k < 4; k++)
    {
        glRotatef(90.0f, 0, 0, 1);
        setColor(rgb(0.45f, 0.30f, 0.20f)); rect(-0.02f, 0.0f, 0.02f, 0.85f);
        setColor(rgb(0.98f, 0.92f, 0.84f)); rect(0.02f, 0.22f, 0.20f, 0.82f);
    }
    setColor(rgb(0.35f, 0.25f, 0.18f)); circle(0, 0, 0.06f, 12);
    glPopMatrix();
}

// a villager: x,y = feet, h = height; walkPh drives the legs; dir = -1 / +1
void drawPerson(float x, float y, float h, const Color& shirt, const Color& hair, float walkPh, float dir, bool child = false)
{
    const float sw = std::sin(walkPh) * 0.22f * h;
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(x, y, 0.22f * h, 0.05f * h, 10);
    setColor(rgb(0.25f, 0.25f, 0.35f));
    thickLine(x - 0.05f * h, y + 0.38f * h, x - 0.05f * h + sw, y, 0.09f * h);
    thickLine(x + 0.05f * h, y + 0.38f * h, x + 0.05f * h - sw, y, 0.09f * h);
    setColor(shirt);
    quad(x - 0.14f * h, y + 0.36f * h, x + 0.14f * h, y + 0.36f * h, x + 0.11f * h, y + 0.74f * h, x - 0.11f * h, y + 0.74f * h);
    thickLine(x - 0.11f * h, y + 0.70f * h, x - 0.17f * h - sw * 0.6f, y + 0.42f * h, 0.07f * h);
    thickLine(x + 0.11f * h, y + 0.70f * h, x + 0.17f * h + sw * 0.6f, y + 0.42f * h, 0.07f * h);
    setColor(rgb(0.98f, 0.82f, 0.68f)); circle(x, y + 0.86f * h, 0.13f * h, 12);
    setColor(hair); ellipse(x - dir * 0.02f * h, y + 0.93f * h, 0.13f * h, 0.07f * h, 10);
    (void)child;
}

// ---- villagers walking along the roads ------------------------------------
struct Road { std::vector<cg::V2> p; };
std::vector<Road> roads;

cg::V2 alongRoad(const Road& r, float u, float* dirOut = nullptr)
{
    float L = 0; for (size_t i = 1; i < r.p.size(); i++) L += cg::len(r.p[i] - r.p[i - 1]);
    float s = u * L;
    for (size_t i = 1; i < r.p.size(); i++)
    {
        const float l = cg::len(r.p[i] - r.p[i - 1]);
        if (s <= l || i + 1 == r.p.size())
        {
            if (dirOut) *dirOut = r.p[i].x >= r.p[i - 1].x ? 1.0f : -1.0f;
            return r.p[i - 1] + (r.p[i] - r.p[i - 1]) * std::min(1.0f, s / std::max(1e-5f, l));
        }
        s -= l;
    }
    return r.p.back();
}

// ---- the village layout (also drawn on the world map) ----
// districts: the fair in the centre, the clock-tower square behind it, the marketplace and the
// tavern by the cottage, a school by the playground, homes in streets at the front and back
const float HOUSES[][3] = {
    {0.04f, -0.035f, 0}, {0.13f, -0.030f, 2}, {0.31f, -0.040f, 1}, {0.70f, -0.025f, 0}, {0.78f, -0.045f, 3},          // back street
    {-0.05f, -0.16f, 1}, {0.86f, -0.43f, 2}, {0.96f, -0.56f, 1}, {0.88f, -0.62f, 3},                                    // east side
    {0.40f, -0.62f, 2}, {0.34f, -0.76f, 0}, {0.47f, -0.80f, 1}, {0.62f, -0.79f, 3}, {0.12f, -0.82f, 2},                 // south street
    {-0.10f, -0.79f, 1}, {-0.26f, -0.77f, 2}, {-0.40f, -0.69f, 0}, {-0.42f, -0.52f, 3}};                                // west street
const float SHOPS[][2] = {{-0.07f, -0.455f}, {0.15f, -0.455f}, {0.17f, -0.545f}};       // marketplace shops (bakery, grocer, florist)
const float TREES[][3] = {{-0.26f, -0.30f, 0.15f}, {0.00f, -0.10f, 0.10f}, {0.40f, -0.08f, 0.09f}, {0.62f, -0.06f, 0.09f}, {0.99f, -0.30f, 0.12f},
    {0.74f, -0.62f, 0.10f}, {0.24f, -0.84f, 0.13f}, {-0.02f, -0.66f, 0.10f}, {0.64f, -0.54f, 0.09f}, {-0.30f, -0.52f, 0.12f},
    {0.04f, -0.27f, 0.08f}, {0.80f, -0.14f, 0.09f}, {-0.52f, -0.62f, 0.12f}};
const float PINES[][2] = {{-0.92f, -0.20f}, {-0.86f, -0.34f}, {-0.94f, -0.48f}, {-0.20f, -0.10f}, {-0.30f, -0.06f}, {0.98f, -0.04f}, {-0.64f, -0.46f}, {-0.56f, -0.78f}};

// ---- the place labels for hovering ----------------------------------------
struct Spot { const char* name; float x, y, w, h; };
std::vector<Spot> spots()
{
    std::vector<Spot> v;
    for (const world::Place& p : world::PLACES)
    {
        float w = 0.16f, h = 0.16f;
        switch (p.kind)
        {
        case world::PlaceKind::Fair:       w = 0.48f; h = 0.22f; break;
        case world::PlaceKind::ClockTower: w = 0.16f; h = 0.42f; break;
        case world::PlaceKind::Playground: w = 0.30f; h = 0.12f; break;
        case world::PlaceKind::Station:    w = 0.26f; h = 0.10f; break;
        case world::PlaceKind::Hospital:   w = 0.26f; h = 0.18f; break;
        case world::PlaceKind::Market:     w = 0.30f; h = 0.10f; break;
        default: break;
        }
        v.push_back(Spot{p.name, p.x, p.y, w, h});
    }
    return v;
}

}   // namespace

// ---------------------------------------------------------------------------
void initVillage()
{
    using cg::V2;
    roads.clear();
    roads.push_back(Road{{V2(0.24f, -0.86f), V2(0.27f, -0.70f), V2(0.32f, -0.56f), V2(0.34f, -0.46f)}});                  // main street -> fair gate
    roads.push_back(Road{{V2(0.44f, -0.20f), V2(0.48f, -0.15f), V2(0.52f, -0.11f)}});                                   // fair -> station
    roads.push_back(Road{{V2(0.12f, -0.38f), V2(0.04f, -0.37f), V2(-0.10f, -0.37f)}});                                  // fair -> cottage
    roads.push_back(Road{{V2(0.12f, -0.38f), V2(0.08f, -0.50f), V2(0.06f, -0.59f)}});                                   // market street
    roads.push_back(Road{{V2(0.62f, -0.42f), V2(0.70f, -0.50f)}});                                                       // to the hospital
    roads.push_back(Road{{V2(0.28f, -0.66f), V2(0.10f, -0.70f)}});                                                       // to the playground
    roads.push_back(Road{{V2(0.60f, -0.20f), V2(0.70f, -0.215f), V2(0.80f, -0.24f), V2(0.88f, -0.30f)}});                // clock square -> police
    roads.push_back(Road{{V2(0.06f, -0.60f), V2(-0.08f, -0.62f), V2(-0.22f, -0.63f)}});                                  // to the school
    roads.push_back(Road{{V2(0.30f, -0.58f), V2(0.46f, -0.64f), V2(0.56f, -0.69f)}});                                    // to the detective
    roads.push_back(Road{{V2(0.80f, -0.24f), V2(0.86f, -0.16f), V2(0.90f, -0.13f)}});                                    // to the fire station
}

const char* villageHover(float x, float y)
{
    static const std::vector<Spot> SP = spots();
    for (const Spot& sp : SP)
    {
        const float k = depthScale(sp.y);
        if (std::fabs(x - sp.x) < sp.w * 0.5f * k && y > sp.y - 0.03f * k && y < sp.y + sp.h * k) return sp.name;
    }
    return nullptr;
}

// static ground of the village (roads, fairground, fields, railway, playground sand): display list
void drawVillageGround()
{
    using cg::V2;
    // tulip fields: colourful stripes in the front right corner
    {
        const Color fc[4] = {rgb(1.0f, 0.42f, 0.50f), rgb(1.0f, 0.85f, 0.30f), rgb(0.80f, 0.50f, 0.95f), rgb(1.0f, 0.60f, 0.30f)};
        for (int i = 0; i < 6; i++)
        {
            const float y0 = -0.70f - i * 0.03f, y1 = y0 - 0.020f, sk = 0.04f * i;
            setColor(fc[i % 4]); quad(0.70f + sk * 0.3f, y1, 0.99f, y1, 0.99f, y0, 0.72f + sk * 0.3f, y0);
            setColor(rgb(0.36f, 0.62f, 0.30f)); quad(0.70f + sk * 0.3f, y1 - 0.010f, 0.99f, y1 - 0.010f, 0.99f, y1, 0.70f + sk * 0.3f, y1);
        }
        setColor(rgb(0.86f, 0.80f, 0.55f)); quad(-0.70f, -0.06f, -0.38f, -0.06f, -0.36f, -0.14f, -0.72f, -0.14f);   // golden field behind the pond
        setColor(rgb(0.78f, 0.72f, 0.45f, 0.6f)); for (int i = 0; i < 5; i++) rect(-0.70f + 0.01f * i, -0.075f - i * 0.014f, -0.37f, -0.070f - i * 0.014f);
    }
    // roads (Catmull-Rom smoothed, width grows toward the viewer)
    for (const Road& r : roads)
    {
        glBegin(GL_TRIANGLE_STRIP);
        const int n = (int)r.p.size();
        for (int i = 0; i < n - 1; i++)
            for (int k = 0; k <= 8; k++)
            {
                const float u = k / 8.0f;
                const V2 p0 = r.p[std::max(0, i - 1)], p1 = r.p[i], p2 = r.p[i + 1], p3 = r.p[std::min(n - 1, i + 2)];
                const float u2 = u * u, u3 = u2 * u;
                const V2 q = (p0 * (-u3 + 2 * u2 - u) + p1 * (3 * u3 - 5 * u2 + 2) + p2 * (-3 * u3 + 4 * u2 + u) + p3 * (u3 - u2)) * 0.5f;
                const float w = 0.016f * depthScale(q.y);
                vtx(rgb(0.90f, 0.80f, 0.60f), q.x, q.y + w * 0.5f);
                vtx(rgb(0.84f, 0.72f, 0.52f), q.x, q.y - w * 0.5f);
            }
        glEnd();
    }
    // the fairground: packed earth with sawdust paths + the clock-tower square (cobbles)
    setColor(rgb(0.88f, 0.78f, 0.58f)); ellipse(world::FAIR_X, world::FAIR_Y, world::FAIR_RX, world::FAIR_RY, 60);
    setColor(rgb(0.94f, 0.86f, 0.66f)); ellipse(world::FAIR_X, world::FAIR_Y, world::FAIR_RX * 0.78f, world::FAIR_RY * 0.70f, 60);
    setColor(rgb(0.80f, 0.70f, 0.52f, 0.5f));
    for (int k = 0; k < 60; k++) { const float a = 6.2832f * cg::hashU(k * 3u + 1u), r = std::sqrt(cg::hashU(k * 7u + 2u)); ellipse(world::FAIR_X + std::cos(a) * world::FAIR_RX * r, world::FAIR_Y + std::sin(a) * world::FAIR_RY * r, 0.006f, 0.002f, 6); }
    setColor(rgb(0.84f, 0.80f, 0.74f)); ellipse(world::HALL_X, world::HALL_Y - 0.01f, 0.13f, 0.035f, 30);
    setColor(rgb(0.74f, 0.70f, 0.66f)); for (int k = 0; k < 30; k++) { const float a = 6.2832f * cg::hashU(k * 5u + 9u), r = std::sqrt(cg::hashU(k * 11u + 4u)); ellipse(world::HALL_X + std::cos(a) * 0.12f * r, world::HALL_Y - 0.01f + std::sin(a) * 0.03f * r, 0.007f, 0.0025f, 6); }
    // marketplace square
    setColor(rgb(0.84f, 0.78f, 0.70f)); ellipse(world::MARKET_X + 0.04f, world::MARKET_Y + 0.005f, 0.15f, 0.045f, 40);
    // railway: ballast, sleepers, rails; a tunnel into the back hills on the left
    {
        const float y = world::RAIL_Y, x0 = -0.30f, x1 = 1.40f;
        setColor(rgb(0.62f, 0.58f, 0.54f)); rect(x0, y - 0.012f, x1, y + 0.010f);
        setColor(rgb(0.45f, 0.32f, 0.22f)); for (float x = x0; x < x1; x += 0.018f) rect(x, y - 0.010f, x + 0.007f, y + 0.008f);
        setColor(rgb(0.35f, 0.35f, 0.40f)); rect(x0, y + 0.002f, x1, y + 0.005f); rect(x0, y - 0.006f, x1, y - 0.003f);
        setColor(rgb(0.50f, 0.48f, 0.52f)); ellipse(x0, y + 0.012f, 0.040f, 0.036f, 20);
        setColor(rgb(0.12f, 0.10f, 0.14f)); ellipse(x0, y + 0.008f, 0.026f, 0.026f, 20); rect(x0 - 0.026f, y - 0.012f, x0 + 0.026f, y + 0.008f);
    }
    // playground: sand + low fence; school yard
    setColor(rgb(0.96f, 0.86f, 0.60f)); ellipse(world::PLAY_X, world::PLAY_Y + 0.01f, 0.17f, 0.055f, 40);
    setColor(rgb(0.98f, 0.94f, 0.86f)); ring(world::PLAY_X, world::PLAY_Y + 0.01f, 0.175f, 0.058f, 40);
    setColor(rgb(0.80f, 0.76f, 0.70f)); ellipse(-0.24f, -0.625f, 0.10f, 0.025f, 24);
    // garden of the cottage: lawn + flower beds + fence (with the gate)
    setColor(rgb(0.50f, 0.82f, 0.38f)); ellipse(world::COTTAGE_X, world::COTTAGE_Y - 0.025f, 0.11f, 0.035f, 30);
    flowerDots(world::COTTAGE_X - 0.10f, world::COTTAGE_Y - 0.05f, 0.20f, 0.03f, 22, 7u, 0.005f);
    drawFence(world::COTTAGE_X - 0.10f, world::COTTAGE_X - 0.015f, world::COTTAGE_Y - 0.055f, 0.016f, rgb(0.88f, 0.55f, 0.40f));
    drawFence(world::COTTAGE_X + 0.015f, world::COTTAGE_X + 0.10f, world::COTTAGE_Y - 0.055f, 0.016f, rgb(0.88f, 0.55f, 0.40f));
}

// buildings, rides, trees, people, train: sorted far -> near and drawn every frame
void drawVillageLive(double time, bool still, float mx, float my)
{
    const float t = still ? 0.0f : (float)time;
    struct It { float y; std::function<void()> f; };
    std::vector<It> items;
    items.reserve(260);
    auto add = [&](float y, std::function<void()> f) { items.push_back(It{y, std::move(f)}); };
    auto S = [](float y) { return depthScale(y); };
    const Color AW[4] = {rgb(0.90f, 0.25f, 0.25f), rgb(0.25f, 0.65f, 0.35f), rgb(0.95f, 0.45f, 0.70f), rgb(0.25f, 0.50f, 0.90f)};
    const Color SH2[6] = {rgb(0.90f, 0.30f, 0.30f), rgb(0.30f, 0.55f, 0.90f), rgb(0.95f, 0.75f, 0.20f), rgb(0.40f, 0.75f, 0.40f), rgb(0.80f, 0.45f, 0.85f), rgb(0.95f, 0.55f, 0.25f)};

    // homes
    int k = 0;
    for (const auto& h : HOUSES)
    {
        const float x = h[0], y = h[1], s = 0.075f * S(y);
        const int st = (int)h[2];
        const bool sm = (k % 3) == 0;
        add(y, [=]() { drawHouse(x, y, s, st, false, time); if (sm && !still) { glPushMatrix(); glTranslatef(x, y, 0); glScalef(s, s, 1); drawSmoke(0.27f, 1.26f, time); glPopMatrix(); } });
        k++;
    }
    // marketplace: three shops, four stalls with vendors, and the tavern
    for (int i = 0; i < 3; i++) { const float x = SHOPS[i][0], y = SHOPS[i][1]; const Color aw = AW[i]; const int st = i;
        add(y, [=]() { drawShop(x, y, 0.070f * S(y), st, aw, time); }); }
    add(-0.545f, [=]() { drawTavern(-0.06f, -0.545f, 0.070f * S(-0.545f), t); });
    for (int i = 0; i < 4; i++)
    {
        const float x = world::MARKET_X - 0.06f + 0.06f * i, y = world::MARKET_Y + 0.005f - 0.012f * (i & 1);
        const Color c = AW[(i + 1) % 4];
        add(y, [=]() {
            const float s = 0.045f * S(y);
            setColor(rgb(0.55f, 0.36f, 0.20f)); rect(x - 0.5f * s, y, x + 0.5f * s, y + 0.30f * s);
            const Color fruits[3] = {rgb(1.0f, 0.3f, 0.2f), rgb(1.0f, 0.8f, 0.2f), rgb(0.4f, 0.8f, 0.3f)};
            for (int q = 0; q < 6; q++) { setColor(fruits[(q + i) % 3]); circle(x - 0.4f * s + q * 0.16f * s, y + 0.34f * s, 0.06f * s, 6); }
            setColor(rgb(0.4f, 0.3f, 0.2f)); rect(x - 0.48f * s, y, x - 0.44f * s, y + 0.75f * s); rect(x + 0.44f * s, y, x + 0.48f * s, y + 0.75f * s);
            glPushMatrix(); glTranslatef(x, y, 0); glScalef(s, s, 1); awning(-0.6f, 0.6f, 0.85f, 0.15f, c, rgb(1, 1, 1)); glPopMatrix();
            drawPerson(x + 0.25f * s, y + 0.002f, 0.75f * s, c, rgb(0.3f, 0.2f, 0.1f), 0.0f, -1.0f);
        });
    }

    // ---- landmarks and establishments ----
    add(world::HALL_Y, [=]() { drawClockTower(world::HALL_X, world::HALL_Y, 0.085f * S(world::HALL_Y), t); });
    add(world::HOSP_Y, [=]() { drawHospitalBuilding(world::HOSP_X, world::HOSP_Y, 0.085f * S(world::HOSP_Y), t); });
    add(world::STATION_Y, [=]() { drawStationBuilding(world::STATION_X, world::STATION_Y, 0.07f * S(world::STATION_Y)); });
    add(-0.21f, [=]() { drawWindmillV(-0.17f, -0.21f, 0.085f * S(-0.21f), t); });
    add(-0.035f, [=]() { drawTower(-0.12f, -0.035f, 0.036f * S(-0.035f), time); });            // old watch tower by the tunnel
    { const world::Place& p = world::place(world::PlaceKind::Police);      add(p.y, [=]() { drawPolice(p.x, p.y, 0.065f * S(p.y), t); }); }
    { const world::Place& p = world::place(world::PlaceKind::Detective);   add(p.y, [=]() { drawDetective(p.x, p.y, 0.070f * S(p.y), t); }); }
    { const world::Place& p = world::place(world::PlaceKind::FireStation); add(p.y, [=]() { drawFireStation(p.x, p.y, 0.065f * S(p.y), t); }); }
    { const world::Place& p = world::place(world::PlaceKind::School);      add(p.y, [=]() { drawSchool(p.x, p.y, 0.070f * S(p.y), t); }); }
    { const world::Place& p = world::place(world::PlaceKind::Library);     add(p.y, [=]() { drawLibrary(p.x, p.y, 0.065f * S(p.y)); }); }
    { const world::Place& p = world::place(world::PlaceKind::PostOffice);  add(p.y, [=]() { drawPostOffice(p.x, p.y, 0.062f * S(p.y)); }); }
    add(world::COTTAGE_Y, [=]() { drawHouse(world::COTTAGE_X, world::COTTAGE_Y, 0.105f, 0, false, time); if (!still) { glPushMatrix(); glTranslatef(world::COTTAGE_X, world::COTTAGE_Y, 0); glScalef(0.105f, 0.105f, 1); drawSmoke(0.27f, 1.26f, time); glPopMatrix(); } });

    // ---- the village fair ----
    add(-0.255f, [=]() { drawFerrisWheel(0.56f, -0.255f, 0.100f * S(-0.255f), t); });
    add(-0.345f, [=]() { drawCarousel(0.23f, -0.345f, 0.075f * S(-0.345f), t); });
    add(-0.415f, [=]() { drawBigTop(0.47f, -0.415f, 0.080f * S(-0.415f), t); });
    add(-0.245f, [=]() { drawChairSwing(0.36f, -0.245f, 0.062f * S(-0.245f), t); });
    add(-0.255f, [=]() { drawHelterSkelter(0.16f, -0.255f, 0.050f * S(-0.255f), t); });
    add(-0.335f, [=]() { drawStage(0.66f, -0.335f, 0.050f * S(-0.335f), t); });
    add(-0.465f, [=]() { drawFairArch(0.34f, -0.465f, 0.050f * S(-0.465f), t); });
    for (int i = 0; i < 9; i++)                                                   // food + game stalls round the edge
    {
        const float a = 3.45f + i * 0.30f;
        const float x = world::FAIR_X + std::cos(a) * world::FAIR_RX * 0.92f, y = world::FAIR_Y + std::sin(a) * world::FAIR_RY * 0.85f;
        if (std::fabs(x - 0.34f) < 0.06f && y < -0.42f) continue;              // keep the gate free
        const Color c = AW[i % 4];
        add(y, [=]() {
            const float s = 0.042f * S(y);
            setColor(rgb(0.95f, 0.92f, 0.86f)); rect(x - 0.5f * s, y, x + 0.5f * s, y + 0.40f * s);
            setColor(c); rect(x - 0.5f * s, y + 0.30f * s, x + 0.5f * s, y + 0.40f * s);
            glPushMatrix(); glTranslatef(x, y, 0); glScalef(s, s, 1); awning(-0.6f, 0.6f, 0.85f, 0.15f, c, rgb(1, 1, 1));
            setColor(rgb(0.4f, 0.3f, 0.2f)); rect(-0.55f, 0.0f, -0.50f, 0.85f); rect(0.50f, 0.0f, 0.55f, 0.85f); glPopMatrix();
        });
    }
    // bunting criss-crossing the fair, balloons drifting up
    add(world::FAIR_Y + 0.12f, [=]() {
        const Color fc[5] = {rgb(1.0f, 0.3f, 0.3f), rgb(1.0f, 0.85f, 0.2f), rgb(0.3f, 0.75f, 0.4f), rgb(0.3f, 0.55f, 1.0f), rgb(0.95f, 0.5f, 0.9f)};
        const float L[][4] = {{0.12f, -0.30f, 0.38f, -0.17f}, {0.38f, -0.17f, 0.68f, -0.28f}, {0.14f, -0.40f, 0.38f, -0.17f}, {0.38f, -0.17f, 0.66f, -0.40f}};
        for (const auto& l : L)
            for (int i = 0; i < 16; i++)
            {
                const float u = i / 15.0f, x = lerpf(l[0], l[2], u), yy = lerpf(l[1], l[3], u) + 0.06f - 0.025f * std::sin(u * 3.1416f) + 0.002f * std::sin(t * 3.0f + i);
                setColor(fc[i % 5]); tri(x - 0.006f, yy, x + 0.006f, yy, x, yy - 0.013f);
            }
        for (int b = 0; b < 6; b++)
        {
            const float ph = fract(t * 0.04f + b / 6.0f), bx = 0.20f + 0.32f * cg::hashU(b * 7u + 1u) + 0.02f * std::sin(t + b), by = -0.30f + 0.45f * ph;
            setColor(withAlpha(fc[b % 5], 1.0f - ph)); ellipse(bx, by, 0.010f, 0.013f, 10);
            setColor(rgb(0.4f, 0.4f, 0.4f, 1.0f - ph)); rect(bx - 0.0005f, by - 0.035f, bx + 0.0005f, by - 0.013f);
        }
    });
    // crowds at the fair: people wandering between the rides (each walks a slow Lissajous loop)
    for (int i = 0; i < 46; i++)
    {
        const float ax = cg::hashU(i * 3u + 1u), ay = cg::hashU(i * 5u + 2u), sp = 0.05f + 0.06f * cg::hashU(i * 7u + 3u);
        const float u = t * sp + 6.2832f * ax;
        const float px = world::FAIR_X + world::FAIR_RX * 0.75f * std::sin(u * 1.0f + ay * 3.0f) * (0.4f + 0.6f * ax);
        const float py = world::FAIR_Y + world::FAIR_RY * 0.65f * std::sin(u * 1.7f + ax * 5.0f) * (0.4f + 0.6f * ay);
        const float dir = std::cos(u * 1.0f + ay * 3.0f) >= 0 ? 1.0f : -1.0f;
        const Color sh = SH2[i % 6], hr = i & 1 ? rgb(0.35f, 0.22f, 0.12f) : rgb(0.85f, 0.70f, 0.35f);
        const bool child = (i % 4) == 0;
        add(py, [=]() { drawPerson(px, py, (child ? 0.022f : 0.030f) * S(py), sh, hr, still ? 0.0f : t * 7.0f + i, dir, child);
                        if (i % 7 == 0) { setColor(SH2[(i + 2) % 6]); circle(px + 0.008f, py + 0.05f * S(py), 0.006f * S(py), 8); } });   // a balloon in hand
    }

    // ---- playground (as before), school kids ----
    add(world::PLAY_Y + 0.04f, [=]() {
        const float x = world::PLAY_X - 0.08f, y = world::PLAY_Y + 0.03f, s = 0.06f * S(y);
        setColor(rgb(0.85f, 0.30f, 0.25f)); thickLine(x - 0.6f * s, y, x - 0.5f * s, y + 0.9f * s, 0.06f * s); thickLine(x + 0.6f * s, y, x + 0.5f * s, y + 0.9f * s, 0.06f * s);
        rect(x - 0.55f * s, y + 0.88f * s, x + 0.55f * s, y + 0.94f * s);
        for (int j = 0; j < 2; j++)
        {
            const float px = x + (j ? 0.25f : -0.25f) * s, a = 0.5f * std::sin(t * 2.2f + j * 1.7f);
            const float sx = px + std::sin(a) * 0.65f * s, sy = y + 0.9f * s - std::cos(a) * 0.65f * s;
            setColor(rgb(0.4f, 0.4f, 0.4f)); thickLine(px, y + 0.9f * s, sx, sy, 0.02f * s);
            drawPerson(sx, sy - 0.05f * s, 0.5f * s, j ? rgb(1.0f, 0.8f, 0.2f) : rgb(0.3f, 0.7f, 1.0f), rgb(0.3f, 0.2f, 0.1f), 0.0f, 1.0f, true);
        }
    });
    add(world::PLAY_Y + 0.02f, [=]() {
        const float x = world::PLAY_X + 0.09f, y = world::PLAY_Y + 0.02f, s = 0.06f * S(y);
        setColor(rgb(0.30f, 0.60f, 0.95f)); rect(x - 0.45f * s, y, x - 0.38f * s, y + 0.8f * s); rect(x - 0.55f * s, y + 0.75f * s, x - 0.30f * s, y + 0.82f * s);
        setColor(rgb(1.0f, 0.80f, 0.20f)); quad(x - 0.32f * s, y + 0.82f * s, x - 0.24f * s, y + 0.82f * s, x + 0.55f * s, y, x + 0.42f * s, y);
        const float u = fract(t * 0.35f);
        drawPerson(lerpf(x - 0.30f * s, x + 0.45f * s, u), lerpf(y + 0.82f * s, y, u), 0.5f * s, rgb(0.9f, 0.3f, 0.5f), rgb(0.2f, 0.1f, 0.05f), 0.0f, 1.0f, true);
    });
    add(world::PLAY_Y - 0.02f, [=]() {
        const float x = world::PLAY_X, y = world::PLAY_Y - 0.02f, s = 0.06f * S(y);
        setColor(rgb(0.6f, 0.4f, 0.9f)); ellipse(x, y + 0.08f * s, 0.45f * s, 0.12f * s, 20);
        for (int j = 0; j < 3; j++)
        {
            const float a = t * 1.4f + j * 2.094f;
            drawPerson(x + std::cos(a) * 0.55f * s, y + std::sin(a) * 0.14f * s, 0.5f * s, j == 0 ? rgb(0.2f, 0.8f, 0.4f) : j == 1 ? rgb(1.0f, 0.5f, 0.2f) : rgb(0.9f, 0.9f, 0.3f),
                       rgb(0.25f, 0.15f, 0.05f), t * 9.0f + j, std::sin(a) > 0 ? 1.0f : -1.0f, true);
        }
    });
    for (int j = 0; j < 4; j++)
    {
        const float a = t * 1.1f + j * 1.57f, x = -0.24f + 0.07f * std::cos(a), y = -0.63f + 0.015f * std::sin(a);
        add(y, [=]() { drawPerson(x, y, 0.022f * S(y), SH2[(j + 3) % 6], rgb(0.3f, 0.2f, 0.1f), t * 8.0f + j, std::sin(a) > 0 ? -1.0f : 1.0f, true); });
    }

    // trees
    for (const auto& tr : TREES)
    {
        const float x = tr[0], y = tr[1], h = tr[2] * S(y);
        add(y, [=]() { drawRoundTree(x, y, h, 0.4f * std::sin(t * 1.3f + x * 9.0f), rgb(0.18f, 0.48f, 0.24f), rgb(0.40f, 0.72f, 0.32f), x > 0.5f); });
    }
    for (const auto& pn : PINES) { const float x = pn[0], y = pn[1]; add(y, [=]() { drawPine(x, y, 0.15f * S(y), 0.02f * std::sin(t * 1.1f + x * 7.0f)); }); }

    // villagers walking on the roads (back and forth), plus a horse cart on the main street
    for (int i = 0; i < 26; i++)
    {
        const Road& r = roads[i % roads.size()];
        const float sp = 0.025f + 0.02f * cg::hashU(i * 5u + 1u), ph = cg::hashU(i * 9u + 3u);
        const float w = fract(t * sp + ph), u = w < 0.5f ? w * 2.0f : 2.0f - w * 2.0f;
        float dir = 1.0f;
        cg::V2 p = alongRoad(r, u, &dir);
        if (w >= 0.5f) dir = -dir;
        p.x += (cg::hashU(i * 3u) - 0.5f) * 0.012f;
        const Color sh = SH2[i % 6], hr = i & 1 ? rgb(0.35f, 0.22f, 0.12f) : rgb(0.85f, 0.70f, 0.35f);
        add(p.y, [=]() { drawPerson(p.x, p.y, 0.030f * S(p.y), sh, hr, still ? 0.0f : t * 8.0f + i, dir); });
    }
    {
        const float w = fract(t * 0.018f + 0.3f), u = w < 0.5f ? w * 2.0f : 2.0f - w * 2.0f;
        float dir = 1.0f;
        cg::V2 p = alongRoad(roads[0], u, &dir);
        if (w >= 0.5f) dir = -dir;
        add(p.y, [=]() {
            const float s = 0.05f * S(p.y), x = p.x, y = p.y;
            setColor(rgb(0, 0, 0, 0.18f)); ellipse(x, y, 0.8f * s, 0.12f * s, 14);
            setColor(rgb(0.55f, 0.36f, 0.20f)); rect(x - dir * 0.75f * s - 0.3f * s, y + 0.2f * s, x - dir * 0.75f * s + 0.3f * s, y + 0.5f * s);
            setColor(rgb(0.95f, 0.80f, 0.30f)); ellipse(x - dir * 0.75f * s, y + 0.55f * s, 0.25f * s, 0.10f * s, 10);
            setColor(rgb(0.25f, 0.18f, 0.12f)); circle(x - dir * 0.75f * s, y + 0.15f * s, 0.15f * s, 10);
            setColor(rgb(0.55f, 0.35f, 0.22f)); ellipse(x, y + 0.45f * s, 0.32f * s, 0.15f * s, 12);
            const float lg = 0.1f * s * std::sin(t * 8.0f);
            rect(x - 0.22f * s + lg, y, x - 0.16f * s + lg, y + 0.40f * s); rect(x + 0.16f * s - lg, y, x + 0.22f * s - lg, y + 0.40f * s);
            ellipse(x + dir * 0.36f * s, y + 0.62f * s, 0.12f * s, 0.09f * s, 10);
        });
    }
    // the train: comes in from the right, stops at the station, goes on into the tunnel
    {
        const float cyc = 30.0f, c = std::fmod(t, cyc);
        const float stopX = world::STATION_X - 0.02f;
        float x;
        if (c < 8.0f) x = lerpf(1.45f, stopX, smoothstep(c / 8.0f));
        else if (c < 14.0f) x = stopX;
        else x = lerpf(stopX, -0.70f, smoothstep((c - 14.0f) / 10.0f));
        const float y = world::RAIL_Y;
        add(y + 0.001f, [=]() {
            const float s = 0.05f * S(y);
            for (int car = 0; car < 4; car++)
            {
                const float cx = x + car * 1.15f * s;
                if (cx < -0.33f) continue;
                const Color body = car == 0 ? rgb(0.85f, 0.20f, 0.20f) : rgb(0.25f, 0.45f, 0.80f);
                setColor(body); rect(cx, y + 0.08f * s, cx + 1.05f * s, y + 0.60f * s);
                setColor(rgb(0.95f, 0.92f, 0.85f)); rect(cx, y + 0.52f * s, cx + 1.05f * s, y + 0.60f * s);
                setColor(rgb(0.55f, 0.78f, 0.95f)); for (int w2 = 0; w2 < 3; w2++) rect(cx + (0.12f + w2 * 0.30f) * s, y + 0.30f * s, cx + (0.32f + w2 * 0.30f) * s, y + 0.46f * s);
                setColor(rgb(0.15f, 0.15f, 0.18f)); circle(cx + 0.22f * s, y + 0.06f * s, 0.08f * s, 8); circle(cx + 0.83f * s, y + 0.06f * s, 0.08f * s, 8);
            }
            if (x > -0.33f && c > 13.0f && c < 18.0f) { setColor(rgb(1, 1, 1, 0.5f)); circle(x + 0.2f * s, y + (0.8f + 0.5f * (c - 13.0f) / 5.0f) * s, 0.15f * s, 10); }
        });
    }
    for (int i = 0; i < 5; i++)
    {
        const float x = world::STATION_X - 0.06f + 0.03f * i, y = world::STATION_Y - 0.005f;
        const Color sh = SH2[(i + 2) % 6];
        add(y - 0.002f, [=]() { drawPerson(x, y, 0.030f * S(y), sh, rgb(0.3f, 0.2f, 0.1f), 0.0f, 1.0f); });
    }

    // far -> near
    std::stable_sort(items.begin(), items.end(), [](const It& a, const It& b) { return a.y > b.y; });
    for (const It& it : items) it.f();

    // fireworks over the fair in the evening / at night (stateless particles)
    if (eveningLights() && !still)
    {
        const bool old = gfx::useTint; gfx::useTint = false;
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        for (int f = 0; f < 3; f++)
        {
            const float cyc = 3.2f + f * 0.7f, ph = std::fmod(t + f * 1.3f, cyc) / cyc;
            const float cx = 0.22f + 0.30f * cg::hashU((uint32_t)((t + f * 1.3f) / cyc) * 7u + f), cy = 0.02f + 0.06f * f;
            const Color c = SH2[(f * 2 + (int)((t + f) / cyc)) % 6];
            if (ph < 0.25f) { setColor(withAlpha(c, 0.8f)); circle(cx, -0.30f + (cy + 0.30f) * ph / 0.25f, 0.004f, 6); continue; }
            const float e = (ph - 0.25f) / 0.75f;
            for (int k2 = 0; k2 < 16; k2++)
            {
                const float a = 6.2832f * k2 / 16.0f, r = 0.07f * std::sqrt(e);
                setColor(withAlpha(c, 0.9f * (1.0f - e))); circle(cx + std::cos(a) * r, cy + std::sin(a) * r * 0.8f - 0.03f * e * e, 0.0045f, 6);
            }
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gfx::useTint = old;
    }

    // hover: a label above the place under the mouse (never in the middle of the view)
    if (const char* nm = villageHover(mx, my))
    {
        static const std::vector<Spot> SP = spots();
        for (const Spot& sp : SP)
            if (sp.name == nm)
            {
                const float kk = depthScale(sp.y), ly = sp.y + sp.h * kk + 0.02f;
                const bool old = gfx::useTint; gfx::useTint = false;
                setColor(rgb(1.0f, 0.95f, 0.6f, 0.30f)); ellipse(sp.x, sp.y, sp.w * 0.55f * kk, 0.03f * kk, 30);
                const float sx = toScreenX(sp.x), sy = toScreenY(ly);
                glPushMatrix(); glLoadIdentity();
                const float w = ui::textWidth(nm, 0.0060f) * 0.5f + 0.04f;
                setColor(rgb(0.08f, 0.10f, 0.20f, 0.80f)); rect(sx - w, sy - 0.02f, sx + w, sy + 0.06f);
                ui::drawText(sx, sy + 0.002f, 0.0060f, rgb(1.0f, 0.95f, 0.75f), nm, true);
                glPopMatrix();
                gfx::useTint = old;
            }
    }
}

const VillageMap& villageMap()
{
    static VillageMap m;
    if (m.houses.empty())
    {
        for (const auto& h : HOUSES) m.houses.push_back(cg::V2(h[0], h[1]));
        for (const auto& sp : SHOPS) m.shops.push_back(cg::V2(sp[0], sp[1]));
        for (const auto& t : TREES) m.trees.push_back(cg::V2(t[0], t[1]));
        for (const auto& t : PINES) m.trees.push_back(cg::V2(t[0], t[1]));
        for (const Road& r : roads) m.roads.push_back(r.p);
    }
    return m;
}

// ---------------------------------------------------------------------------
// World-map vignettes: each place drawn with the SAME building art as the village
// ---------------------------------------------------------------------------
void drawPlaceVignette(world::PlaceKind k, float x, float y, float s, float t, bool full)
{
    using world::PlaceKind;
    if (full)                                                      // a grassy island under the scene
    {
        setColor(rgb(0, 0, 0, 0.15f)); ellipse(x + 0.01f * s, y - 0.012f * s, 0.20f * s, 0.06f * s, 30);
        setColor(rgb(0.52f, 0.78f, 0.40f)); ellipse(x, y, 0.19f * s, 0.055f * s, 30);
        setColor(rgb(0.64f, 0.86f, 0.48f)); ellipse(x - 0.02f * s, y + 0.008f * s, 0.15f * s, 0.035f * s, 30);
    }
    switch (k)
    {
    case PlaceKind::Garden:
        if (full)
        {
            setColor(rgb(0.40f, 0.68f, 0.92f)); ellipse(x - 0.12f * s, y - 0.005f * s, 0.07f * s, 0.022f * s, 20);    // the pond
            drawWindmillV(x - 0.11f * s, y + 0.018f * s, 0.075f * s, t);
            drawRoundTree(x - 0.05f * s, y + 0.005f * s, 0.10f * s, 0.2f * std::sin(t), rgb(0.18f, 0.48f, 0.24f), rgb(0.40f, 0.72f, 0.32f));
            drawHouse(x + 0.06f * s, y + 0.000f * s, 0.11f * s, 0, true, t);
            flowerDots((x - 0.06f * s), y - 0.040f * s, 0.20f * s, 0.022f * s, 22, 3u, 0.006f * s);
            drawFence(x - 0.02f * s, x + 0.16f * s, y - 0.025f * s, 0.016f * s, rgb(0.88f, 0.55f, 0.40f));
        }
        else drawHouse(x, y, 0.06f * s, 0, false, t);
        break;
    case PlaceKind::Playground:
        if (full)
        {
            setColor(rgb(0.96f, 0.86f, 0.60f)); ellipse(x, y, 0.16f * s, 0.045f * s, 30);
            const float sx = x - 0.08f * s, sy = y - 0.005f * s, q = 0.10f * s;               // swings
            setColor(rgb(0.85f, 0.30f, 0.25f)); thickLine(sx - 0.6f * q, sy, sx - 0.5f * q, sy + 0.9f * q, 0.07f * q); thickLine(sx + 0.6f * q, sy, sx + 0.5f * q, sy + 0.9f * q, 0.07f * q);
            rect(sx - 0.55f * q, sy + 0.86f * q, sx + 0.55f * q, sy + 0.94f * q);
            for (int j = 0; j < 2; j++) { const float px = sx + (j ? 0.25f : -0.25f) * q, a = 0.5f * std::sin(t * 2.2f + j * 1.7f);
                const float ex = px + std::sin(a) * 0.6f * q, ey = sy + 0.9f * q - std::cos(a) * 0.6f * q;
                setColor(rgb(0.4f, 0.4f, 0.4f)); thickLine(px, sy + 0.9f * q, ex, ey, 0.025f * q);
                drawPerson(ex, ey - 0.05f * q, 0.45f * q, j ? rgb(1.0f, 0.8f, 0.2f) : rgb(0.3f, 0.7f, 1.0f), rgb(0.3f, 0.2f, 0.1f), 0.0f, 1.0f, true); }
            const float lx = x + 0.07f * s, ly = y - 0.005f * s;                              // slide
            setColor(rgb(0.30f, 0.60f, 0.95f)); rect(lx - 0.45f * q, ly, lx - 0.38f * q, ly + 0.8f * q); rect(lx - 0.55f * q, ly + 0.75f * q, lx - 0.30f * q, ly + 0.82f * q);
            setColor(rgb(1.0f, 0.80f, 0.20f)); quad(lx - 0.32f * q, ly + 0.82f * q, lx - 0.24f * q, ly + 0.82f * q, lx + 0.55f * q, ly, lx + 0.42f * q, ly);
            setColor(rgb(0.55f, 0.36f, 0.20f)); thickLine(x - 0.03f * s, y - 0.030f * s + 0.012f * s * std::sin(t * 2.0f), x + 0.05f * s, y - 0.030f * s - 0.012f * s * std::sin(t * 2.0f), 0.010f * s);   // see-saw
            setColor(rgb(0.95f, 0.30f, 0.30f)); circle(x + 0.13f * s, y - 0.025f * s, 0.012f * s, 12);       // ball
        }
        else { setColor(rgb(0.85f, 0.30f, 0.25f)); rect(x - 0.02f * s, y, x - 0.015f * s, y + 0.05f * s); rect(x + 0.015f * s, y, x + 0.02f * s, y + 0.05f * s); rect(x - 0.02f * s, y + 0.045f * s, x + 0.02f * s, y + 0.05f * s); }
        break;
    case PlaceKind::Station:
        if (full)
        {
            const float ry = y - 0.025f * s;
            setColor(rgb(0.62f, 0.58f, 0.54f)); rect(x - 0.19f * s, ry - 0.008f * s, x + 0.19f * s, ry + 0.008f * s);
            setColor(rgb(0.45f, 0.32f, 0.22f)); for (float xx = x - 0.19f * s; xx < x + 0.19f * s; xx += 0.012f * s) rect(xx, ry - 0.007f * s, xx + 0.005f * s, ry + 0.006f * s);
            setColor(rgb(0.35f, 0.35f, 0.40f)); rect(x - 0.19f * s, ry + 0.002f * s, x + 0.19f * s, ry + 0.004f * s);
            drawStationBuilding(x, y + 0.005f * s, 0.11f * s);
            const float tx = x - 0.20f * s + std::fmod(t * 0.05f, 0.40f) * s;                  // a train passing
            for (int c = 0; c < 3; c++) { const float cx = tx + c * 0.055f * s; if (cx < x - 0.19f * s || cx > x + 0.16f * s) continue;
                setColor(c == 0 ? rgb(0.85f, 0.20f, 0.20f) : rgb(0.25f, 0.45f, 0.80f)); rect(cx, ry, cx + 0.05f * s, ry + 0.028f * s);
                setColor(rgb(0.55f, 0.78f, 0.95f)); rect(cx + 0.008f * s, ry + 0.012f * s, cx + 0.042f * s, ry + 0.022f * s); }
        }
        else drawStationBuilding(x, y, 0.05f * s);
        break;
    case PlaceKind::Hospital:
        if (full)
        {
            drawHospitalBuilding(x, y, 0.13f * s, t);
            drawRoundTree(x - 0.16f * s, y + 0.004f * s, 0.07f * s, 0.0f, rgb(0.18f, 0.48f, 0.24f), rgb(0.40f, 0.72f, 0.32f));
            drawRoundTree(x + 0.17f * s, y + 0.004f * s, 0.06f * s, 0.0f, rgb(0.18f, 0.48f, 0.24f), rgb(0.40f, 0.72f, 0.32f));
        }
        else drawHospitalBuilding(x, y, 0.05f * s, t);
        break;
    case PlaceKind::ClockTower:  drawClockTower(x, y, 0.040f * s, t); break;
    case PlaceKind::Fair:        drawFerrisWheel(x + 0.03f * s, y, 0.040f * s, t); drawBigTop(x - 0.05f * s, y - 0.005f * s, 0.035f * s, t); break;
    case PlaceKind::Market:      drawShop(x, y, 0.05f * s, 0, rgb(0.90f, 0.25f, 0.25f), t); break;
    case PlaceKind::Police:      drawPolice(x, y, 0.045f * s, t); break;
    case PlaceKind::Detective:   drawDetective(x, y, 0.045f * s, t); break;
    case PlaceKind::FireStation: drawFireStation(x, y, 0.045f * s, t); break;
    case PlaceKind::School:      drawSchool(x, y, 0.040f * s, t); break;
    case PlaceKind::Library:     drawLibrary(x, y, 0.050f * s); break;
    case PlaceKind::PostOffice:  drawPostOffice(x, y, 0.050f * s); break;
    }
}
