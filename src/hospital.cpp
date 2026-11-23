#include "hospital.h"

#include "gfx.h"
#include "theme.h"
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace gfx;

namespace hospital
{
namespace
{

const Color SKIN[4]  = {rgb(0.96f, 0.76f, 0.60f), rgb(0.87f, 0.66f, 0.48f), rgb(0.66f, 0.46f, 0.32f), rgb(0.45f, 0.30f, 0.22f)};
const Color HAIR[4]  = {rgb(0.18f, 0.12f, 0.10f), rgb(0.45f, 0.28f, 0.14f), rgb(0.85f, 0.70f, 0.30f), rgb(0.55f, 0.55f, 0.58f)};
const Color SHIRT[6] = {rgb(0.85f, 0.35f, 0.30f), rgb(0.30f, 0.55f, 0.85f), rgb(0.35f, 0.70f, 0.45f), rgb(0.92f, 0.75f, 0.25f),
                        rgb(0.65f, 0.40f, 0.78f), rgb(0.95f, 0.55f, 0.65f)};
const Color PANTS[3] = {rgb(0.28f, 0.32f, 0.45f), rgb(0.42f, 0.30f, 0.20f), rgb(0.25f, 0.25f, 0.28f)};

Color uiColor(int c)    // 0 green, 1 red, 2 yellow, 3 white
{
    switch (c)
    {
    case 0:  return rgb(0.55f, 1.00f, 0.60f);
    case 1:  return rgb(1.00f, 0.45f, 0.42f);
    case 2:  return rgb(1.00f, 0.90f, 0.45f);
    default: return rgb(1.00f, 1.00f, 1.00f);
    }
}

float fitPx(const char* s, float maxW, float maxPx)
{
    const float units = 6.0f * (float)std::strlen(s) - 1.0f;
    return std::min(maxPx, maxW / std::max(1.0f, units));
}

void plate(float cx, float cy, float w, float h, const Color& bg, const char* text, const Color& fg, float maxPx)
{
    setColor(rgb(0, 0, 0, 0.30f)); rect(cx - w / 2 + 0.004f, cy - h / 2 - 0.006f, cx + w / 2 + 0.004f, cy + h / 2 - 0.006f);
    setColor(shade(bg, 0.55f));    rect(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
    setColor(bg);                  rect(cx - w / 2 + 0.008f, cy - h / 2 + 0.008f, cx + w / 2 - 0.008f, cy + h / 2 - 0.008f);
    const float px = fitPx(text, w - 0.05f, maxPx);
    ui::drawText(cx, cy - 3.5f * px, px, fg, text, true);
}

bool night() { return theme::cur().windowsLit > 0.5f; }

// ---------------------------------------------------------------------------
// People
// ---------------------------------------------------------------------------
struct Look
{
    int skin = 0, hair = 0, shirt = 0, pants = 0;
    float facing = 1.0f, phase = 0.0f, alpha = 1.0f;
    bool moving = false, coat = false, gown = false, scrubs = false;
    float sick = 0.0f;                  // pedestrians: 0..1 symptom (pale / hunched)
    Cond cond = Cond::Checkup;
    bool hasCond = false;
    bool sitting = false;
    float armUp = 0.0f;                 // doctor gesture
};

void person(float x, float y, float s, const Look& L)
{
    const float step = L.moving ? std::sin(L.phase) : 0.0f;
    const float bob  = L.moving ? 0.010f * std::fabs(step) : 0.0f;
    const float oldA = gfx::alphaMul;
    gfx::alphaMul = oldA * L.alpha;

    glPushMatrix();
    glTranslatef(x, y + bob * s, 0.0f);
    glScalef(s * L.facing, s, 1.0f);

    setColor(rgb(0, 0, 0, 0.20f)); ellipse(0.0f, 0.0f, 0.24f, 0.04f, 14);

    const bool legCast = L.hasCond && L.cond == Cond::BrokenLeg;
    const Color pants = L.coat || L.scrubs ? rgb(0.30f, 0.62f, 0.68f) : PANTS[L.pants % 3];
    for (int k = 0; k < 2; k++)                                      // legs
    {
        glPushMatrix();
        glTranslatef(0.0f, 0.46f, 0.0f);
        glRotatef((k ? -1.0f : 1.0f) * 24.0f * step * (legCast && k == 0 ? 0.3f : 1.0f), 0.0f, 0.0f, 1.0f);
        setColor(k ? shade(pants, 0.85f) : pants);
        rect(-0.045f, -0.42f, 0.045f, 0.0f);
        setColor(rgb(0.20f, 0.14f, 0.12f)); rect(-0.05f, -0.46f, 0.09f, -0.40f);
        if (legCast && k == 0)                                       // white plaster cast
        {
            setColor(rgb(0.97f, 0.97f, 0.94f)); rect(-0.065f, -0.40f, 0.065f, 0.0f);
            setColor(rgb(0.80f, 0.80f, 0.78f)); rect(-0.065f, -0.20f, 0.065f, -0.185f);
            setColor(rgb(0.85f, 0.20f, 0.20f)); rect(-0.065f, -0.30f, 0.065f, -0.285f);
        }
        glPopMatrix();
    }

    // upper body (hunches forward when ill)
    const float hunch = std::min(1.0f, L.sick * 1.4f) * 12.0f + (L.hasCond && L.cond != Cond::Checkup ? 5.0f : 0.0f);
    glPushMatrix();
    glTranslatef(0.0f, 0.46f, 0.0f);
    glRotatef(-hunch, 0.0f, 0.0f, 1.0f);
    glTranslatef(0.0f, -0.46f, 0.0f);

    const bool armCast = L.hasCond && L.cond == Cond::BrokenArm;
    const Color top = L.coat ? rgb(0.97f, 0.97f, 0.99f) : L.gown ? rgb(0.72f, 0.88f, 0.92f) : L.scrubs ? rgb(0.30f, 0.62f, 0.68f) : SHIRT[L.shirt % 6];
    const Color skin = SKIN[L.skin % 4];

    glPushMatrix();                                                  // back arm
    glTranslatef(0.0f, 0.76f, 0.0f);
    glRotatef(-22.0f * step, 0.0f, 0.0f, 1.0f);
    setColor(shade(top, 0.85f)); rect(-0.03f, -0.28f, 0.03f, 0.0f);
    setColor(skin); circle(0.0f, -0.30f, 0.035f, 10);
    glPopMatrix();

    setColor(top); quad(-0.12f, 0.44f, 0.12f, 0.44f, 0.11f, 0.80f, -0.11f, 0.80f);   // torso
    if (L.coat)                                                      // doctor: open coat, stethoscope
    {
        setColor(rgb(0.30f, 0.62f, 0.68f)); tri(-0.04f, 0.80f, 0.04f, 0.80f, 0.0f, 0.60f);
        setColor(rgb(0.25f, 0.25f, 0.30f)); glLineWidth(2.0f); glBegin(GL_LINE_STRIP);
        glVertex2f(-0.07f, 0.80f); glVertex2f(-0.08f, 0.66f); glVertex2f(0.0f, 0.62f); glVertex2f(0.08f, 0.66f); glVertex2f(0.07f, 0.80f); glEnd(); glLineWidth(1.0f);
        setColor(rgb(0.6f, 0.6f, 0.65f)); circle(0.0f, 0.61f, 0.014f, 8);
    }
    if (L.hasCond && (L.cond == Cond::Bleeding || L.cond == Cond::Surgery))          // blood on the torso
    {
        setColor(rgb(0.70f, 0.05f, 0.08f, 0.95f)); ellipse(0.02f, 0.60f, 0.075f, 0.10f, 10);
        setColor(rgb(0.55f, 0.03f, 0.06f)); ellipse(0.03f, 0.58f, 0.04f, 0.06f, 8);
        setColor(rgb(0.70f, 0.05f, 0.08f, 0.95f)); rect(0.00f, 0.44f, 0.022f, 0.52f);   // drip
    }

    // front arm
    glPushMatrix();
    glTranslatef(0.0f, 0.76f, 0.0f);
    if (armCast)
    {
        glRotatef(-95.0f, 0.0f, 0.0f, 1.0f);                         // forearm across the chest in a sling
        setColor(rgb(0.97f, 0.97f, 0.94f)); rect(-0.045f, -0.30f, 0.045f, 0.0f);
        setColor(rgb(0.85f, 0.20f, 0.20f)); rect(-0.045f, -0.16f, 0.045f, -0.145f);
    }
    else
    {
        glRotatef(22.0f * step - 70.0f * L.armUp, 0.0f, 0.0f, 1.0f);
        setColor(top); rect(-0.03f, -0.28f, 0.03f, 0.0f);
        setColor(skin); circle(0.0f, -0.30f, 0.035f, 10);
    }
    glPopMatrix();
    if (armCast) { setColor(rgb(0.93f, 0.90f, 0.82f)); tri(-0.10f, 0.78f, 0.10f, 0.78f, 0.10f, 0.56f); }   // sling

    // head
    Color face = skin;
    if (L.sick > 0.35f) face = mix(skin, rgb(0.70f, 0.82f, 0.62f), std::min(0.6f, (L.sick - 0.3f) * 1.2f));   // pale / greenish
    if (L.hasCond && L.cond == Cond::Fever) face = mix(skin, rgb(0.95f, 0.45f, 0.40f), 0.55f);
    if (L.hasCond && (L.cond == Cond::Bleeding || L.cond == Cond::Surgery)) face = mix(skin, rgb(0.90f, 0.90f, 0.84f), 0.4f);
    setColor(face); circle(0.0f, 0.90f, 0.085f, 16);
    setColor(HAIR[L.hair % 4]); ellipse(0.0f, 0.955f, 0.088f, 0.045f, 12); rect(-0.088f, 0.90f, -0.06f, 0.96f);
    setColor(rgb(0.15f, 0.10f, 0.10f)); circle(0.040f, 0.91f, 0.009f, 8);
    if (L.coat || L.scrubs) { setColor(rgb(0.55f, 0.82f, 0.86f)); rect(0.0f, 0.86f, 0.09f, 0.90f); }   // face mask
    if (L.hasCond && (L.cond == Cond::Bleeding || L.cond == Cond::Surgery))         // head bandage with blood
    {
        setColor(rgb(0.97f, 0.97f, 0.94f)); rect(-0.088f, 0.94f, 0.088f, 0.975f);
        setColor(rgb(0.75f, 0.05f, 0.08f)); circle(0.03f, 0.957f, 0.016f, 8);
    }
    if (L.hasCond && L.cond == Cond::Fever)                          // thermometer
    {
        setColor(rgb(0.95f, 0.95f, 0.95f)); rect(0.03f, 0.865f, 0.13f, 0.875f);
        setColor(rgb(0.9f, 0.1f, 0.1f)); rect(0.03f, 0.865f, 0.07f, 0.875f);
    }
    if (L.hasCond && L.cond == Cond::Cough)                          // cough puffs
    {
        setColor(rgb(0.85f, 0.85f, 0.85f, 0.8f)); circle(0.13f, 0.86f, 0.012f, 8); circle(0.16f, 0.89f, 0.016f, 8); circle(0.19f, 0.86f, 0.011f, 8);
    }
    glPopMatrix();                                                   // end upper body

    if (legCast)                                                     // crutch
    {
        setColor(rgb(0.62f, 0.45f, 0.25f));
        thickLine(0.17f, 0.62f, 0.10f, 0.0f, 0.022f);
        thickLine(0.12f, 0.64f, 0.24f, 0.60f, 0.022f);
    }
    glPopMatrix();
    gfx::alphaMul = oldA;
}

void marker(float x, float y, Kind k, int severity, float flash, float t)
{
    const bool e = k == Kind::Emergency;
    const float pulse = e ? 1.0f + 0.12f * std::sin(t * 7.0f) : 1.0f;
    const float r = 0.030f * pulse;
    gfx::useTint = false;
    setColor(rgb(1, 1, 1, 0.95f)); circle(x, y, r + 0.004f, 14);
    setColor(e ? rgb(0.85f, 0.10f, 0.12f) : rgb(0.20f, 0.65f, 0.35f));
    rect(x - r * 0.62f, y - r * 0.18f, x + r * 0.62f, y + r * 0.18f);
    rect(x - r * 0.18f, y - r * 0.62f, x + r * 0.18f, y + r * 0.62f);
    for (int i = 0; i < severity; i++)                               // severity pips
    {
        setColor(i < 3 ? rgb(1.0f, 0.75f, 0.2f) : rgb(1.0f, 0.2f, 0.2f));
        circle(x - (severity - 1) * 0.0085f + i * 0.017f, y + r + 0.016f, 0.0058f, 6);
    }
    if (flash > 0.0f) { glBlendFunc(GL_SRC_ALPHA, GL_ONE); glow(x, y, 0.10f, rgb(1.0f, 0.3f, 0.2f, 0.55f * flash), 20); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    gfx::useTint = true;
}

Look patientLook(const Patient& p)
{
    Look L;
    L.skin = p.skin; L.hair = p.hair; L.shirt = p.shirt;
    L.gown = true; L.cond = p.cond; L.hasCond = true;
    L.moving = p.st == PState::Walking || p.st == PState::CheckIn || p.st == PState::Leaving;
    L.phase = p.walkPhase;
    L.sick = p.kind == Kind::Emergency ? 0.6f : 0.0f;
    return L;
}

// ---------------------------------------------------------------------------
// Shared bits
// ---------------------------------------------------------------------------
void redCross(float x, float y, float r, const Color& bg = rgb(1, 1, 1), const Color& c = rgb(0.85f, 0.10f, 0.14f))
{
    setColor(bg); rect(x - r, y - r, x + r, y + r);
    setColor(c);
    rect(x - r * 0.62f, y - r * 0.20f, x + r * 0.62f, y + r * 0.20f);
    rect(x - r * 0.20f, y - r * 0.62f, x + r * 0.20f, y + r * 0.62f);
}

void sky(float t, float sunX, float sunY, float yBottom, float yTop)
{
    Stop stops[] = {{yBottom, rgb(0.86f, 0.94f, 0.99f)}, {0.0f, rgb(0.74f, 0.89f, 0.99f)}, {yTop, rgb(0.30f, 0.60f, 0.93f)}};
    for (Stop& s : stops) s.c = theme::skyColor(s.c, s.y);
    const bool old = gfx::useTint;
    gfx::useTint = false;
    vGradient(-WORLD_W - 0.2f, WORLD_W + 0.2f, stops, 3);
    if (theme::cur().sunAlpha > 0.01f)
    {
        glow(sunX, sunY, 0.55f, theme::sun(rgb(1.0f, 0.97f, 0.80f, 0.7f)), 40);
        setColor(theme::sun(rgb(1.0f, 0.97f, 0.78f))); circle(sunX, sunY, 0.085f, 30);
    }
    theme::drawStarsAndMoon(sunX, sunY, 0.085f, fract(t / 60.0f));
    gfx::useTint = old;
}

void windowPane(float x0, float y0, float x1, float y1, bool lit)
{
    setColor(rgb(0.55f, 0.58f, 0.64f)); rect(x0 - 0.008f, y0 - 0.008f, x1 + 0.008f, y1 + 0.008f);
    if (lit)
    {
        const bool old = gfx::useTint; gfx::useTint = false;
        rectV(x0, y0, x1, y1, rgb(1.0f, 0.82f, 0.45f), rgb(1.0f, 0.93f, 0.68f));
        gfx::useTint = old;
    }
    else rectV(x0, y0, x1, y1, rgb(0.55f, 0.74f, 0.88f), rgb(0.78f, 0.90f, 0.97f));
    setColor(rgb(0.55f, 0.58f, 0.64f)); rect((x0 + x1) / 2 - 0.003f, y0, (x0 + x1) / 2 + 0.003f, y1);
}

// ---------------------------------------------------------------------------
// EXTERIOR
// ---------------------------------------------------------------------------
void drawAmbulance(const Ambulance& a, float t)
{
    glPushMatrix();
    glTranslatef(a.x, ROAD_Y + 0.02f, 0.0f);
    glScalef(a.dir, 1.0f, 1.0f);
    setColor(rgb(0, 0, 0, 0.25f)); ellipse(0.0f, -0.012f, 0.33f, 0.03f, 16);
    const bool open = a.st == Ambulance::Unload || a.st == Ambulance::Hold;
    setColor(rgb(0.96f, 0.96f, 0.98f)); rect(-0.30f, 0.02f, 0.20f, 0.22f);                           // box
    setColor(rgb(0.90f, 0.90f, 0.93f)); quad(0.20f, 0.02f, 0.33f, 0.02f, 0.33f, 0.12f, 0.27f, 0.19f); rect(0.20f, 0.12f, 0.27f, 0.19f);
    setColor(rgb(0.45f, 0.72f, 0.90f)); quad(0.22f, 0.13f, 0.28f, 0.13f, 0.245f, 0.18f, 0.22f, 0.18f); // windshield
    setColor(rgb(0.85f, 0.12f, 0.15f)); rect(-0.30f, 0.08f, 0.33f, 0.115f);                          // stripe
    redCross(-0.06f, 0.16f, 0.036f);
    setColor(rgb(0.70f, 0.70f, 0.74f)); rect(0.20f, 0.02f, 0.205f, 0.20f);
    if (open) { setColor(rgb(0.20f, 0.20f, 0.26f)); rect(-0.34f, 0.03f, -0.30f, 0.21f); setColor(rgb(0.95f, 0.95f, 0.97f)); rect(-0.33f, 0.04f, -0.30f, 0.20f); }
    setColor(rgb(0.30f, 0.30f, 0.34f)); rect(-0.30f, 0.0f, 0.33f, 0.03f);                            // bumper
    const bool phase = std::fmod(a.siren, 2.0f) < 1.0f;                                              // light bar flashes
    setColor(phase ? rgb(1.0f, 0.15f, 0.15f) : rgb(0.45f, 0.08f, 0.08f)); rect(-0.14f, 0.22f, -0.03f, 0.255f);
    setColor(phase ? rgb(0.10f, 0.20f, 0.55f) : rgb(0.25f, 0.45f, 1.0f)); rect(-0.03f, 0.22f, 0.08f, 0.255f);
    for (int w = 0; w < 2; w++)                                                                       // wheels
    {
        const float wx = w ? 0.20f : -0.18f;
        setColor(rgb(0.12f, 0.12f, 0.14f)); circle(wx, 0.015f, 0.042f, 16);
        setColor(rgb(0.70f, 0.72f, 0.76f)); circle(wx, 0.015f, 0.022f, 10);
        setColor(rgb(0.3f, 0.3f, 0.34f)); thickLine(wx, 0.015f, wx + 0.02f * std::cos(a.wheel), 0.015f + 0.02f * std::sin(a.wheel), 0.007f);
    }
    glPopMatrix();
    const bool old = gfx::useTint; gfx::useTint = false;                                              // siren glow (own light)
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    const float gx = a.x + a.dir * -0.04f, gy = ROAD_Y + 0.02f + 0.24f;
    glow(gx - a.dir * 0.03f, gy, 0.20f, phase ? rgb(1.0f, 0.1f, 0.1f, 0.55f) : rgb(0.2f, 0.35f, 1.0f, 0.5f), 24);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gfx::useTint = old;
    (void)t;
}

void drawBuilding(float t)
{
    const float B = BUILD_BASE_Y;
    const bool lit = night();
    const Color wall = rgb(0.94f, 0.95f, 0.97f), wallDk = rgb(0.80f, 0.84f, 0.90f), teal = rgb(0.28f, 0.60f, 0.70f);

    // shadow
    setColor(rgb(0, 0, 0, 0.14f)); ellipse(0.0f, B - 0.02f, 1.12f, 0.035f, 30);
    // wings
    for (int side = -1; side <= 1; side += 2)
    {
        const float x0 = side < 0 ? -1.02f : 0.52f, x1 = side < 0 ? -0.52f : 1.02f;
        rectV(x0, B, x1, 0.28f, wallDk, wall);
        setColor(teal); rect(x0 - 0.01f, 0.28f, x1 + 0.01f, 0.32f);
        setColor(rgb(0.82f, 0.84f, 0.88f)); rect(x0, B, x1, B + 0.02f);
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 3; c++)
            {
                const float wx = x0 + 0.075f + c * 0.145f, wy = B + 0.08f + r * 0.30f;
                windowPane(wx, wy, wx + 0.095f, wy + 0.17f, lit);
            }
    }
    // helipad on the right wing roof
    setColor(rgb(0.30f, 0.32f, 0.38f)); rect(0.64f, 0.32f, 0.92f, 0.345f);
    setColor(rgb(0.95f, 0.85f, 0.2f)); rect(0.72f, 0.345f, 0.735f, 0.40f); rect(0.82f, 0.345f, 0.835f, 0.40f); rect(0.72f, 0.365f, 0.835f, 0.378f);
    // tower
    rectV(-0.52f, B, 0.52f, 0.58f, wallDk, wall);
    setColor(wallDk); rect(0.40f, B, 0.52f, 0.58f);
    setColor(teal); rect(-0.54f, 0.58f, 0.54f, 0.62f);
    setColor(rgb(0.82f, 0.84f, 0.88f)); rect(-0.52f, B, 0.52f, B + 0.02f);
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 5; c++)
        {
            const float wx = -0.44f + c * 0.18f, wy = 0.02f + r * 0.17f - 0.02f;
            if (r == 0 && c == 2 && false) continue;
            windowPane(wx, wy, wx + 0.12f, wy + 0.12f, lit);
        }
    for (int c = 0; c < 4; c++)                                                                       // ground-floor windows
    {
        if (c == 1 || c == 2) continue;
        const float wx = c < 2 ? -0.46f + c * 0.18f : 0.28f + (c - 3) * 0.18f;
        windowPane(wx, B + 0.08f, wx + 0.12f, B + 0.20f, lit);
    }
    // sign plate with the hospital name (name comes from hospital_config.h)
    {
        const float cx = 0.0f, cy = 0.455f, w = 0.92f, h = 0.15f;
        setColor(rgb(0, 0, 0, 0.30f)); rect(cx - w / 2 + 0.006f, cy - h / 2 - 0.008f, cx + w / 2 + 0.006f, cy + h / 2 - 0.008f);
        setColor(rgb(0.80f, 0.84f, 0.90f)); rect(cx - w / 2 - 0.012f, cy - h / 2 - 0.012f, cx + w / 2 + 0.012f, cy + h / 2 + 0.012f);
        const bool old = gfx::useTint; gfx::useTint = false;
        rectV(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, shade(rgb(0.10f, 0.28f, 0.55f), 0.75f + 0.25f * (1.0f - (night() ? 0.4f : 0.0f))), rgb(0.14f, 0.36f, 0.68f));
        const float px = fitPx(hospitalcfg::NAME, w - 0.07f, 0.0125f);
        const float glowK = theme::cur().windowsLit;
        if (glowK > 0.05f) { glBlendFunc(GL_SRC_ALPHA, GL_ONE); glow(cx, cy, 0.55f, rgb(0.4f, 0.65f, 1.0f, 0.22f * glowK), 30); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
        ui::drawText(cx, cy - 3.5f * px, px, rgb(1.0f, 1.0f, 1.0f), hospitalcfg::NAME, true);
        gfx::useTint = old;
    }
    // roof cross emblem
    redCross(0.0f, 0.70f, 0.075f);
    setColor(rgb(0.6f, 0.62f, 0.68f)); rect(-0.01f, 0.62f, 0.01f, 0.625f);
    // entrance: canopy, doors, steps
    const bool hover = ui::mouse.valid && std::fabs(ui::mouse.x - DOOR_X) < 0.14f && ui::mouse.y > DOOR_Y - 0.10f && ui::mouse.y < DOOR_Y + 0.40f && !draggingSomething();
    setColor(rgb(0.78f, 0.82f, 0.88f)); rect(-0.24f, B + 0.26f, 0.24f, B + 0.29f);
    setColor(rgb(0.85f, 0.12f, 0.15f)); rect(-0.24f, B + 0.23f, 0.24f, B + 0.26f);
    setColor(rgb(0.60f, 0.64f, 0.70f)); rect(-0.115f, B, 0.115f, B + 0.23f);
    for (int s = 0; s < 2; s++)
    {
        const float dx0 = s ? 0.0f : -0.105f;
        if (lit) { const bool old = gfx::useTint; gfx::useTint = false; rectV(dx0, B + 0.005f, dx0 + 0.105f, B + 0.225f, rgb(1.0f, 0.82f, 0.50f), rgb(1.0f, 0.94f, 0.72f)); gfx::useTint = old; }
        else rectV(dx0, B + 0.005f, dx0 + 0.105f, B + 0.225f, hover ? rgb(0.78f, 0.95f, 0.85f) : rgb(0.58f, 0.78f, 0.90f), hover ? rgb(0.92f, 1.0f, 0.94f) : rgb(0.80f, 0.92f, 0.98f));
    }
    setColor(rgb(0.45f, 0.48f, 0.55f)); rect(-0.003f, B, 0.003f, B + 0.23f);
    setColor(rgb(0.80f, 0.80f, 0.82f)); rect(-0.16f, B - 0.03f, 0.16f, B); rect(-0.20f, B - 0.06f, 0.20f, B - 0.03f);    // steps
    plate(-0.40f, B + 0.12f, 0.20f, 0.05f, rgb(0.85f, 0.12f, 0.15f), "EMERGENCY", rgb(1, 1, 1), 0.0055f);
    plate(0.40f, B + 0.12f, 0.20f, 0.05f, rgb(0.20f, 0.55f, 0.70f), "ENTRANCE", rgb(1, 1, 1), 0.0055f);
    (void)t;
}

void drawLamp(float x, float y)
{
    setColor(rgb(0.28f, 0.30f, 0.36f)); rect(x - 0.006f, y, x + 0.006f, y + 0.34f);
    rect(x - 0.006f, y + 0.33f, x + 0.07f, y + 0.345f);
    setColor(night() ? rgb(1.0f, 0.95f, 0.70f) : rgb(0.85f, 0.88f, 0.92f)); ellipse(x + 0.06f, y + 0.325f, 0.03f, 0.012f, 10);
    if (night())
    {
        const bool old = gfx::useTint; gfx::useTint = false;
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); glow(x + 0.06f, y + 0.32f, 0.22f, rgb(1.0f, 0.88f, 0.5f, 0.38f), 24); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gfx::useTint = old;
    }
}

void drawPedestrian(const Pedestrian& p, float t)
{
    Look L;
    L.skin = p.skin; L.hair = p.hair; L.shirt = p.shirt; L.pants = p.shirt;
    L.facing = p.dir >= 0 ? 1.0f : -1.0f;
    L.phase = p.phase;
    L.moving = p.st != Pedestrian::Dragged;
    L.sick = p.symptom >= 0.30f ? p.symptom : 0.0f;
    L.alpha = p.st == Pedestrian::Entering ? std::max(0.0f, p.alpha) : 1.0f;
    L.armUp = p.st == Pedestrian::Dragged ? 1.0f : 0.0f;
    // visible clues: the sicker, the more obvious
    if (p.symptom >= 0.84f) { L.hasCond = true; L.cond = (p.id % 2) ? Cond::BrokenLeg : Cond::Bleeding; }
    else if (p.symptom >= 0.52f) { L.hasCond = true; L.cond = (p.id % 2) ? Cond::Cough : Cond::Fever; }
    const float lift = p.st == Pedestrian::Dragged ? 0.03f : 0.0f;
    person(p.x, p.y + lift, 0.30f, L);
    if (p.st == Pedestrian::Dragged) { setColor(rgb(1, 1, 1, 0.7f)); ring(p.x, p.y, 0.09f, 0.03f, 20); }
    if (p.flash > 0.0f && p.st == Pedestrian::Walk) {}
    (void)t;
}

void drawExterior(float t)
{
    sky(t, -1.35f, 0.58f, -1.0f, 1.2f);

    static const Ridge hills = makeRolling(-2.4f, 2.4f, 0.04f, 0.02f, 0.14f, 2.1f, 2.0f);
    static const Ridge hills2 = makeRolling(-2.4f, 2.4f, 0.04f, -0.12f, 0.10f, 5.3f, 2.6f);
    drawRidge(hills, -0.5f, rgb(0.50f, 0.74f, 0.58f), rgb(0.62f, 0.82f, 0.62f));
    drawRidge(hills2, -0.5f, rgb(0.42f, 0.70f, 0.46f), rgb(0.54f, 0.80f, 0.50f));
    for (int i = 0; i < 12; i++)
    {
        const float x = -2.0f + i * 0.36f + 0.1f * hash1(i * 3.0f);
        if (std::fabs(x) < 1.1f) continue;
        drawRoundTree(x, ridgeAt(hills2, x) - 0.01f, 0.18f + 0.06f * hash1(i * 7.0f), wave(fract(t / 60.0f), 6.0f, hash1(i * 1.7f)), rgb(0.20f, 0.52f, 0.28f), rgb(0.36f, 0.68f, 0.34f));
    }

    // lawn, sidewalk, road
    rectV(-WORLD_W - 0.1f, SIDEWALK_Y1, WORLD_W + 0.1f, BUILD_BASE_Y + 0.02f, rgb(0.34f, 0.64f, 0.34f), rgb(0.50f, 0.78f, 0.42f));
    setColor(rgb(0.80f, 0.80f, 0.82f)); rect(-WORLD_W - 0.1f, SIDEWALK_Y0, WORLD_W + 0.1f, SIDEWALK_Y1);
    setColor(rgb(0.66f, 0.66f, 0.70f)); rect(-WORLD_W - 0.1f, SIDEWALK_Y1 - 0.008f, WORLD_W + 0.1f, SIDEWALK_Y1);
    rectV(-WORLD_W - 0.1f, -1.0f, WORLD_W + 0.1f, SIDEWALK_Y0, rgb(0.24f, 0.25f, 0.29f), rgb(0.32f, 0.33f, 0.37f));
    setColor(rgb(0.95f, 0.88f, 0.40f));
    for (int i = -9; i <= 9; i++) rect(i * 0.20f - 0.05f, ROAD_Y - 0.006f, i * 0.20f + 0.05f, ROAD_Y + 0.006f);
    setColor(rgb(0.84f, 0.84f, 0.86f));                                                               // zebra crossing
    for (int i = 0; i < 6; i++) rect(-1.45f + i * 0.045f, -0.96f, -1.42f + i * 0.045f, SIDEWALK_Y0);
    // path to the door
    setColor(rgb(0.86f, 0.80f, 0.68f)); quad(-0.12f, BUILD_BASE_Y - 0.06f, 0.12f, BUILD_BASE_Y - 0.06f, 0.20f, SIDEWALK_Y1, -0.20f, SIDEWALK_Y1);

    drawBuilding(t);

    // garden around the entrance
    for (int s = -1; s <= 1; s += 2)
    {
        drawBush(s * 0.30f, BUILD_BASE_Y - 0.05f, 0.060f, rgb(0.14f, 0.42f, 0.22f), rgb(0.30f, 0.62f, 0.30f));
        drawBush(s * 0.88f, BUILD_BASE_Y - 0.05f, 0.075f, rgb(0.14f, 0.42f, 0.22f), rgb(0.30f, 0.62f, 0.30f));
        drawRoundTree(s * 1.42f, BUILD_BASE_Y - 0.08f, 0.62f, wave(fract(t / 60.0f), 5.0f, s * 0.2f), rgb(0.16f, 0.46f, 0.24f), rgb(0.34f, 0.68f, 0.30f));
        drawLamp(s * 0.70f, SIDEWALK_Y1 - 0.01f);
        for (int k = 0; k < 5; k++)
        {
            const Color fc = k % 2 ? rgb(0.95f, 0.45f, 0.60f) : rgb(1.0f, 0.82f, 0.25f);
            setColor(rgb(0.25f, 0.55f, 0.28f)); rect(s * (0.40f + k * 0.045f) - 0.002f, BUILD_BASE_Y - 0.09f, s * (0.40f + k * 0.045f) + 0.002f, BUILD_BASE_Y - 0.03f);
            setColor(fc); circle(s * (0.40f + k * 0.045f), BUILD_BASE_Y - 0.03f, 0.014f, 8);
        }
    }
    drawLamp(-1.20f, SIDEWALK_Y1 - 0.01f);
    drawLamp(1.20f, SIDEWALK_Y1 - 0.01f);
    // flag pole
    setColor(rgb(0.7f, 0.7f, 0.74f)); rect(-1.14f, BUILD_BASE_Y - 0.02f, -1.126f, 0.52f);
    const float fw = 0.02f * std::sin(t * 3.0f);
    setColor(rgb(0.85f, 0.12f, 0.15f)); quad(-1.126f, 0.50f, -0.96f, 0.50f + fw, -0.96f, 0.43f + fw, -1.126f, 0.43f);
    redCross(-1.05f, 0.465f + fw * 0.5f, 0.022f, rgb(1, 1, 1), rgb(0.85f, 0.12f, 0.15f));

    // ambulances and pedestrians, nearest (lowest) drawn last
    std::vector<std::pair<float, std::function<void()>>> items;
    for (const Ambulance& a : ambulances()) { const Ambulance* ap = &a; items.push_back({ROAD_Y + 0.02f, [ap, t]() { drawAmbulance(*ap, t); }}); }
    for (const Pedestrian& p : pedestrians()) { const Pedestrian* pp = &p; items.push_back({p.y, [pp, t]() { drawPedestrian(*pp, t); }}); }
    std::sort(items.begin(), items.end(), [](const std::pair<float, std::function<void()>>& a, const std::pair<float, std::function<void()>>& b) { return a.first > b.first; });
    for (auto& it : items) it.second();

    // drag feedback: the hospital lights up when a person is dragged onto it
    if (draggingSomething() && overHospital(ui::mouse.x, ui::mouse.y))
    {
        const bool old = gfx::useTint; gfx::useTint = false;
        setColor(rgb(0.3f, 1.0f, 0.5f, 0.20f)); rect(HOSP_X0, HOSP_Y0, HOSP_X1, BUILD_BASE_Y + 1.0f);
        ui::drawText(0.0f, 0.12f, 0.0095f, rgb(1, 1, 1), "DROP HERE: CHECK IN THIS PERSON", true);
        gfx::useTint = old;
    }
}

// ---------------------------------------------------------------------------
// INTERIOR
// ---------------------------------------------------------------------------
void bed(float x, float y, bool lampOn, float t)
{
    setColor(rgb(0.55f, 0.58f, 0.64f)); rect(x - 0.20f, y - 0.07f, x - 0.185f, y + 0.03f); rect(x + 0.185f, y - 0.07f, x + 0.20f, y + 0.03f);
    setColor(rgb(0.75f, 0.78f, 0.84f)); rect(x - 0.22f, y + 0.02f, x + 0.22f, y + 0.045f);
    setColor(rgb(0.90f, 0.95f, 0.98f)); rect(x - 0.21f, y + 0.045f, x + 0.21f, y + 0.075f);
    setColor(rgb(0.96f, 0.96f, 0.98f)); ellipse(x - 0.16f, y + 0.085f, 0.055f, 0.022f, 10);
    // surgical lamp
    setColor(rgb(0.55f, 0.58f, 0.64f)); rect(x - 0.004f, y + 0.55f, x + 0.004f, ROOM_TOP);
    setColor(rgb(0.85f, 0.88f, 0.92f)); ellipse(x, y + 0.52f, 0.07f, 0.022f, 14);
    if (lampOn)
    {
        const bool old = gfx::useTint; gfx::useTint = false;
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); glow(x, y + 0.50f, 0.30f, rgb(1.0f, 0.98f, 0.82f, 0.35f + 0.05f * std::sin(t * 3.0f)), 24); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gfx::useTint = old;
    }
    // monitor + IV
    setColor(rgb(0.18f, 0.20f, 0.26f)); rect(x + 0.22f, y + 0.10f, x + 0.33f, y + 0.20f);
    setColor(rgb(0.30f, 0.95f, 0.55f)); glLineWidth(2.0f);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i <= 10; i++) glVertex2f(x + 0.225f + i * 0.0095f, y + 0.15f + (i % 5 == 2 ? 0.035f : 0.0f) * (lampOn ? 1.0f : 0.0f));
    glEnd(); glLineWidth(1.0f);
    setColor(rgb(0.6f, 0.62f, 0.68f)); rect(x - 0.27f, y - 0.07f, x - 0.264f, y + 0.30f);
    setColor(rgb(0.70f, 0.88f, 0.98f)); rect(x - 0.295f, y + 0.22f, x - 0.24f, y + 0.30f);
}

void chair(float x, float y, bool front)
{
    if (!front)
    {
        setColor(rgb(0.25f, 0.45f, 0.62f)); rect(x - 0.075f, y + 0.12f, x + 0.075f, y + 0.27f);
        setColor(rgb(0.20f, 0.36f, 0.50f)); rect(x - 0.075f, y + 0.12f, x - 0.065f, y + 0.27f);
    }
    else
    {
        setColor(rgb(0.30f, 0.52f, 0.72f)); rect(x - 0.085f, y - 0.01f, x + 0.085f, y + 0.115f);
        setColor(rgb(0.22f, 0.40f, 0.56f)); rect(x - 0.085f, y - 0.01f, x + 0.085f, y + 0.012f);
    }
}

void drawInteriorBackdrop(float t)
{
    const Color wallA = rgb(0.90f, 0.95f, 0.97f), wallB = rgb(0.80f, 0.90f, 0.94f);
    // ceiling band + walls
    rectV(-WORLD_W - 0.1f, ROOM_TOP, WORLD_W + 0.1f, 1.0f, rgb(0.55f, 0.62f, 0.70f), rgb(0.70f, 0.76f, 0.82f));
    rectV(-WORLD_W - 0.1f, 0.02f, WORLD_W + 0.1f, ROOM_TOP, wallB, wallA);
    // zone wall tints
    const Color zt[4] = {rgb(0.55f, 0.85f, 0.85f, 0.35f), rgb(0.55f, 0.82f, 0.60f, 0.30f), rgb(1.00f, 0.55f, 0.55f, 0.32f), rgb(0.55f, 0.70f, 1.00f, 0.32f)};
    for (int z = 0; z < 4; z++) { setColor(zt[z]); rect(ZONE_X[z], 0.02f, ZONE_X[z + 1], ROOM_TOP); }
    // wainscot + skirting
    setColor(rgb(0.62f, 0.78f, 0.84f)); rect(-WORLD_W - 0.1f, 0.02f, WORLD_W + 0.1f, 0.12f);
    // floor
    rectV(-WORLD_W - 0.1f, -1.0f, WORLD_W + 0.1f, 0.02f, rgb(0.74f, 0.80f, 0.86f), rgb(0.88f, 0.92f, 0.95f));
    setColor(rgb(1, 1, 1, 0.35f));
    for (int i = -9; i <= 9; i++) rect(i * 0.20f - 0.002f, -1.0f, i * 0.20f + 0.002f, 0.02f);
    for (int j = 0; j < 5; j++) rect(-WORLD_W - 0.1f, -0.9f + j * 0.2f - 0.002f, WORLD_W + 0.1f, -0.9f + j * 0.2f + 0.002f);
    // guide lines on the floor
    setColor(rgb(0.90f, 0.25f, 0.25f, 0.55f)); rect(-0.95f, -0.865f, 0.52f, -0.845f);
    setColor(rgb(0.30f, 0.50f, 0.95f, 0.55f)); rect(0.15f, -0.905f, 1.40f, -0.885f);
    // partitions between zones
    for (int z = 1; z < 4; z++)
    {
        const float x = ZONE_X[z];
        setColor(rgb(0.60f, 0.72f, 0.80f)); rect(x - 0.012f, -1.0f, x + 0.012f, ROOM_TOP);
        setColor(rgb(0.75f, 0.90f, 0.98f, 0.30f)); rect(x - 0.05f, -0.15f, x + 0.05f, 0.55f);
    }
    // zone name plates
    plate(-1.37f, 0.67f, 0.74f, 0.085f, rgb(0.15f, 0.55f, 0.58f), "RECEPTION", rgb(1, 1, 1), 0.0085f);
    plate(-0.40f, 0.67f, 0.94f, 0.085f, rgb(0.20f, 0.58f, 0.34f), "WAITING AREA", rgb(1, 1, 1), 0.0085f);
    plate(0.565f, 0.67f, 0.70f, 0.085f, rgb(0.80f, 0.14f, 0.16f), "EMERGENCY ROOM", rgb(1, 1, 1), 0.0085f);
    plate(1.38f, 0.67f, 0.70f, 0.085f, rgb(0.20f, 0.38f, 0.82f), "GENERAL CONSULTATION", rgb(1, 1, 1), 0.0085f);

    // ---- reception wall: hospital name, door, desk ----
    {
        const bool old = gfx::useTint; gfx::useTint = false;
        const float cx = -1.37f, cy = 0.46f, w = 0.78f, h = 0.13f;
        setColor(rgb(0.80f, 0.84f, 0.90f)); rect(cx - w / 2 - 0.01f, cy - h / 2 - 0.01f, cx + w / 2 + 0.01f, cy + h / 2 + 0.01f);
        rectV(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, rgb(0.10f, 0.28f, 0.58f), rgb(0.14f, 0.36f, 0.68f));
        const float px = fitPx(hospitalcfg::NAME, w - 0.06f, 0.0100f);
        ui::drawText(cx, cy - 3.5f * px, px, rgb(1, 1, 1), hospitalcfg::NAME, true);
        gfx::useTint = old;
    }
    setColor(rgb(0.55f, 0.60f, 0.66f)); rect(-1.775f, -0.80f, -1.62f, 0.34f);                     // entrance door frame
    setColor(rgb(0.45f, 0.75f, 0.90f, 0.85f)); rect(-1.76f, -0.78f, -1.64f, 0.32f);
    plate(-1.70f, 0.37f, 0.15f, 0.045f, rgb(0.2f, 0.65f, 0.35f), "EXIT", rgb(1, 1, 1), 0.0055f);
    // wall clock (real time)
    {
        const double secs = 0; (void)secs;
        const float hr = theme::localHour();
        const float hAng = -TAU * (std::fmod(hr, 12.0f) / 12.0f) + PI / 2, mAng = -TAU * (hr - std::floor(hr)) + PI / 2;
        const float cx = -0.95f + 0.0f, cy = 0.50f; (void)cx; (void)cy;
        const float x = -1.00f, y = 0.30f;
        setColor(rgb(0.30f, 0.32f, 0.38f)); circle(x, y, 0.062f, 24);
        setColor(rgb(0.97f, 0.97f, 0.97f)); circle(x, y, 0.054f, 24);
        setColor(rgb(0.15f, 0.15f, 0.2f)); thickLine(x, y, x + std::cos(hAng) * 0.032f, y + std::sin(hAng) * 0.032f, 0.008f);
        thickLine(x, y, x + std::cos(mAng) * 0.046f, y + std::sin(mAng) * 0.046f, 0.005f);
    }
    // plant
    setColor(rgb(0.55f, 0.36f, 0.22f)); quad(-1.00f, -0.10f, -0.92f, -0.10f, -0.935f, -0.02f, -0.985f, -0.02f);
    setColor(rgb(0.20f, 0.58f, 0.28f)); ellipse(-0.96f, 0.06f, 0.07f, 0.09f, 12); ellipse(-0.99f, 0.02f, 0.045f, 0.06f, 10); ellipse(-0.93f, 0.03f, 0.045f, 0.06f, 10);

    // ---- waiting area wall: windows + triage board ----
    const float ox = -0.92f;
    for (int w = 0; w < 2; w++)
    {
        const float x0 = ox + w * 0.20f, y0 = 0.25f;
        setColor(rgb(0.55f, 0.58f, 0.64f)); rect(x0 - 0.01f, y0 - 0.01f, x0 + 0.17f, y0 + 0.37f);
        Stop st[2] = {{y0, theme::skyColor(rgb(0.80f, 0.92f, 0.99f), 0.0f)}, {y0 + 0.36f, theme::skyColor(rgb(0.45f, 0.72f, 0.96f), 0.9f)}};
        const bool old = gfx::useTint; gfx::useTint = false;
        vGradient(x0, x0 + 0.16f, st, 2);
        if (theme::cur().moonAlpha > 0.05f && w == 0) { setColor(rgb(0.96f, 0.96f, 0.88f, theme::cur().moonAlpha)); circle(x0 + 0.08f, y0 + 0.26f, 0.025f, 14); }
        gfx::useTint = old;
        setColor(rgb(0.55f, 0.58f, 0.64f)); rect(x0 + 0.077f, y0, x0 + 0.083f, y0 + 0.36f); rect(x0, y0 + 0.177f, x0 + 0.16f, y0 + 0.183f);
    }
    // ER wall: heart-rate poster
    setColor(rgb(0.97f, 0.97f, 0.98f)); rect(0.20f, 0.25f, 0.42f, 0.55f);
    redCross(0.31f, 0.46f, 0.05f);
    setColor(rgb(0.35f, 0.38f, 0.45f)); rect(0.225f, 0.28f, 0.395f, 0.30f); rect(0.225f, 0.32f, 0.36f, 0.34f);
    // consult wall: eye chart / board
    setColor(rgb(0.97f, 0.97f, 0.98f)); rect(1.05f, 0.25f, 1.30f, 0.55f);
    for (int r = 0; r < 4; r++) { setColor(rgb(0.25f, 0.28f, 0.34f)); rect(1.075f, 0.50f - r * 0.06f, 1.075f + 0.15f * (1.0f - r * 0.2f), 0.52f - r * 0.06f); }
    // ceiling lights
    const bool lights = true;
    for (int i = 0; i < 8; i++)
    {
        const float x = -1.55f + i * 0.46f;
        setColor(rgb(0.95f, 0.97f, 1.0f)); rect(x - 0.12f, ROOM_TOP - 0.02f, x + 0.12f, ROOM_TOP);
        if (lights)
        {
            const bool old = gfx::useTint; gfx::useTint = false;
            glBlendFunc(GL_SRC_ALPHA, GL_ONE); glow(x, ROOM_TOP - 0.10f, 0.28f, rgb(1.0f, 0.97f, 0.85f, night() ? 0.22f : 0.08f), 18); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            gfx::useTint = old;
        }
    }
    (void)t;
}

void reception(float t)
{
    // desk (patients stand in front, nurse behind)
    setColor(rgb(0.45f, 0.30f, 0.20f)); rect(-1.50f, -0.34f, -1.06f, -0.10f);
    setColor(rgb(0.62f, 0.44f, 0.30f)); rect(-1.52f, -0.10f, -1.04f, -0.07f);
    setColor(rgb(0.15f, 0.55f, 0.58f)); rect(-1.50f, -0.34f, -1.06f, -0.30f);
    setColor(rgb(0.18f, 0.20f, 0.26f)); rect(-1.40f, -0.07f, -1.28f, 0.02f);                      // computer
    setColor(rgb(0.45f, 0.75f, 0.95f)); rect(-1.385f, -0.055f, -1.295f, 0.01f);
    setColor(rgb(0.35f, 0.37f, 0.42f)); rect(-1.35f, -0.07f, -1.33f, -0.06f);
    (void)t;
}

void drawInterior(float t)
{
    drawInteriorBackdrop(t);

    // ---- zone-specific furniture drawn behind people ----
    // waiting chairs (backs)
    for (int i = 0; i < 10; i++) { float x, y; seatPos(i, x, y); chair(x, y, false); }
    // ER beds
    bool bedBusy[2] = {false, false};
    for (const Patient& p : patients()) if (p.zone == Zone::Emergency && p.slot >= 0 && p.slot < ER_BEDS && p.st == PState::Treating) bedBusy[p.slot] = true;
    for (int b = 0; b < ER_BEDS; b++) { float x, y; slotPos(Zone::Emergency, b, x, y); bed(x, y, bedBusy[b], t); }
    // ER queue marks
    for (int q = ER_BEDS; q < ER_SLOTS; q++)
    {
        float x, y; slotPos(Zone::Emergency, q, x, y);
        setColor(rgb(0.90f, 0.25f, 0.25f, 0.45f)); ellipse(x, y - 0.005f, 0.085f, 0.022f, 16);
        char nb[4]; std::snprintf(nb, sizeof(nb), "%d", q - ER_BEDS + 1);
        ui::drawText(x, y - 0.075f, 0.0050f, rgb(0.55f, 0.1f, 0.1f), nb, true);
    }
    ui::drawText(0.52f, -0.945f, 0.0050f, rgb(0.55f, 0.1f, 0.1f), "ER QUEUE", true);
    // consultation desks
    for (int c = 0; c < CONSULT_SLOTS; c++)
    {
        float x, y; slotPos(Zone::Consult, c, x, y);
        setColor(rgb(0.50f, 0.34f, 0.22f)); rect(x + 0.07f, y - 0.07f, x + 0.27f, y + 0.12f);
        setColor(rgb(0.66f, 0.48f, 0.32f)); rect(x + 0.06f, y + 0.12f, x + 0.28f, y + 0.145f);
        setColor(rgb(0.18f, 0.20f, 0.26f)); rect(x + 0.14f, y + 0.145f, x + 0.22f, y + 0.20f);
        setColor(rgb(0.45f, 0.75f, 0.95f)); rect(x + 0.15f, y + 0.152f, x + 0.21f, y + 0.19f);
        chair(x, y, false);
    }
    // consult: exam bench label
    // reception desk
    reception(t);

    // ---- people ----
    struct Item { float y; std::function<void()> fn; };
    std::vector<Item> items;
    // staff
    struct Staff { float x, y; bool nurse; int slot; Zone z; };
    const Staff staff[] = {{-1.28f, -0.22f, true, 0, Zone::Reception}, {0.60f, -0.34f, false, 0, Zone::Emergency}, {0.40f, -0.50f, false, 1, Zone::Emergency},
                           {1.40f, -0.30f, false, 0, Zone::Consult}, {1.74f, -0.30f, false, 1, Zone::Consult}};
    for (const Staff& s : staff)
    {
        float tx = s.x, ty = s.y; bool busy = false;
        if (s.z == Zone::Emergency) { float bx, by; slotPos(Zone::Emergency, s.slot, bx, by); tx = bx + 0.19f; ty = by - 0.05f; for (const Patient& p : patients()) if (p.zone == Zone::Emergency && p.slot == s.slot && p.st == PState::Treating) busy = true; }
        if (s.z == Zone::Consult)   { for (const Patient& p : patients()) if (p.zone == Zone::Consult && p.slot == s.slot && p.st == PState::Treating) busy = true; tx = s.slot ? 1.52f + 0.0f : 1.36f; tx = (s.slot ? 1.56f : 1.20f) + 0.17f; ty = -0.36f; }
        const bool nurse = s.nurse;
        items.push_back({ty, [=]() {
            Look L; L.skin = s.slot * 2 + (nurse ? 1 : 0); L.hair = s.slot + 1; L.coat = !nurse; L.scrubs = nurse; L.facing = s.z == Zone::Reception ? 1.0f : -1.0f;
            L.armUp = busy ? 0.5f + 0.5f * std::sin(t * 4.0f + s.slot) : 0.0f;
            person(tx, ty, 0.33f, L);
            if (s.z == Zone::Consult) { setColor(rgb(0.50f, 0.34f, 0.22f)); rect(tx - 0.06f, ty - 0.02f, tx + 0.10f, ty + 0.10f); }   // desk hides the legs
        }});
    }
    for (const Patient& p : patients())
    {
        const Patient* pp = &p;
        float sortY = p.y;
        items.push_back({sortY, [pp, t]() {
            const Patient& q = *pp;
            Look L = patientLook(q);
            L.facing = (q.zone == Zone::Consult ? -1.0f : q.zone == Zone::Emergency && q.slot >= ER_BEDS ? 1.0f : 1.0f);
            if (q.st == PState::Leaving) L.facing = -1.0f;
            if (q.st == PState::Walking || q.st == PState::CheckIn) L.facing = (q.tx - q.x) >= 0 ? 1.0f : -1.0f;
            const float s = 0.30f;
            const bool lying = q.st == PState::Treating && q.zone == Zone::Emergency;
            const bool seated = (q.st == PState::Seated) || (q.st == PState::Treating && q.zone == Zone::Consult);
            if (lying)
            {
                glPushMatrix();
                glTranslatef(q.x + 0.05f, q.y + 0.125f, 0.0f);
                glRotatef(90.0f, 0.0f, 0.0f, 1.0f);
                person(0.0f, 0.0f, s, L);
                glPopMatrix();
            }
            else if (q.st == PState::Dragged) { person(q.x, q.y + 0.02f, s, L); setColor(rgb(1, 1, 1, 0.55f)); ring(q.x, q.y, 0.10f, 0.03f, 20); }
            else person(q.x, q.y, s, L);
            if (seated && q.zone == Zone::Waiting) chair(q.x, q.y, true);
            if (seated && q.zone == Zone::Consult) chair(q.x, q.y, true);
            // treatment progress
            if (q.st == PState::Treating)
            {
                const float f = 1.0f - q.treatLeft / q.treatTotal;
                const float by = q.y + (lying ? 0.26f : 0.42f);
                const bool old = gfx::useTint; gfx::useTint = false;
                setColor(rgb(0, 0, 0, 0.5f)); rect(q.x - 0.07f, by, q.x + 0.07f, by + 0.016f);
                setColor(rgb(0.35f, 0.95f, 0.5f)); rect(q.x - 0.07f + 0.002f, by + 0.002f, q.x - 0.07f + 0.002f + 0.136f * f, by + 0.014f);
                gfx::useTint = old;
            }
            if (q.cooldown > 0.0f && q.st != PState::Dragged)                               // ER lock-out badge
            {
                const bool old = gfx::useTint; gfx::useTint = false;
                char b[12]; std::snprintf(b, sizeof(b), "LOCK %.0f", std::ceil(q.cooldown));
                setColor(rgb(0.15f, 0.05f, 0.05f, 0.75f)); rect(q.x - 0.075f, q.y + 0.50f, q.x + 0.075f, q.y + 0.545f);
                ui::drawText(q.x, q.y + 0.51f, 0.0050f, rgb(1.0f, 0.6f, 0.4f), b, true);
                gfx::useTint = old;
            }
            if (q.st != PState::Leaving)
                marker(q.x, q.y + (lying ? 0.30f : 0.40f) + (q.cooldown > 0.0f ? 0.0f : 0.0f), q.kind, q.severity, q.flash, t);
        }});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.y > b.y; });
    for (Item& it : items) it.fn();

    // ---- drag feedback: highlight the area under the cursor ----
    if (draggingSomething() && ui::mouse.valid)
    {
        const Zone z = zoneAt(ui::mouse.x, ui::mouse.y);
        if (z != Zone::None)
        {
            const int zi = (int)z - 1;
            const bool old = gfx::useTint; gfx::useTint = false;
            const Color hc[4] = {rgb(0.4f, 0.9f, 0.9f, 0.18f), rgb(0.4f, 0.95f, 0.5f, 0.18f), rgb(1.0f, 0.35f, 0.35f, 0.20f), rgb(0.4f, 0.55f, 1.0f, 0.20f)};
            const char* names[4] = {"DROP: BACK TO RECEPTION SEAT", "DROP: WAITING AREA", "DROP: EMERGENCY ROOM", "DROP: GENERAL CONSULTATION"};
            setColor(hc[zi]); rect(ZONE_X[zi], -1.0f, ZONE_X[zi + 1], ROOM_TOP);
            ui::drawText((ZONE_X[zi] + ZONE_X[zi + 1]) / 2, -0.12f, 0.0070f, rgb(1, 1, 1), names[zi], true);
            gfx::useTint = old;
        }
    }
}

}   // namespace

// ---------------------------------------------------------------------------
// public
// ---------------------------------------------------------------------------
void draw(View view, float time)
{
    if (view == View::Outside) { drawExterior(time); return; }
    const Color keep = gfx::tintColor;                                // indoors the daylight tint is much weaker
    gfx::tintColor = mix(rgb(1, 1, 1), keep, 0.35f);
    drawInterior(time);
    gfx::tintColor = keep;
}

void drawOverlay(View view)
{
    const bool old = gfx::useTint;
    gfx::useTint = false;
    const Stats& s = stats();

    // ---- statistics bar ----
    const float x0 = -1.18f, x1 = 1.55f;
    setColor(rgb(0.03f, 0.08f, 0.15f, 0.72f)); rect(x0, 0.795f, x1, 1.0f);
    setColor(rgb(0.4f, 0.75f, 0.95f, 0.9f)); rect(x0, 0.793f, x1, 0.797f);
    char l1[96], l2[112], l3[128];
    std::snprintf(l1, sizeof(l1), "%s  -  %s", hospitalcfg::NAME, view == View::Inside ? "INSIDE" : "OUTSIDE");
    std::snprintf(l2, sizeof(l2), "ADMITTED %d (EMERG %d / GENERAL %d)  RELEASED %d  AMBULANCES %d  TURNED AWAY %d", s.admitted, s.admittedEmergency, s.admittedGeneral, s.released, s.ambulances, s.turnedAway);
    std::snprintf(l3, sizeof(l3), "EMERG WAIT %d TREAT %d  GENERAL WAIT %d TREAT %d  INSIDE %d/%d  OK %d WRONG %d NO-ROOM %d",
                  waitingCount(Kind::Emergency), treatingCount(Kind::Emergency), waitingCount(Kind::General), treatingCount(Kind::General),
                  insideCount(), hospitalcfg::MAX_INSIDE, s.correct, s.wrong, s.rejected);
    ui::drawText(x0 + 0.03f, 0.925f, fitPx(l1, 2.0f, 0.0090f), rgb(1.0f, 0.95f, 0.6f), l1);
    ui::drawText(x0 + 0.03f, 0.865f, fitPx(l2, 2.65f, 0.0068f), rgb(1, 1, 1), l2);
    ui::drawText(x0 + 0.03f, 0.812f, fitPx(l3, 2.65f, 0.0068f), rgb(0.85f, 0.95f, 1.0f), l3);

    // ---- interior: EXIT button + triage board ----
    if (view == View::Inside)
    {
        const bool hov = ui::mouse.valid && ui::mouse.x >= EXIT_X0 && ui::mouse.x <= EXIT_X1 && ui::mouse.y >= EXIT_Y0 && ui::mouse.y <= EXIT_Y1;
        setColor(hov ? rgb(0.85f, 0.25f, 0.25f, 0.96f) : rgb(0.55f, 0.15f, 0.15f, 0.88f)); rect(EXIT_X0, EXIT_Y0, EXIT_X1, EXIT_Y1);
        setColor(rgb(1, 1, 1, 0.8f)); glLineWidth(2.0f); glBegin(GL_LINE_LOOP); glVertex2f(EXIT_X0, EXIT_Y0); glVertex2f(EXIT_X1, EXIT_Y0); glVertex2f(EXIT_X1, EXIT_Y1); glVertex2f(EXIT_X0, EXIT_Y1); glEnd(); glLineWidth(1.0f);
        ui::drawText((EXIT_X0 + EXIT_X1) / 2, EXIT_Y0 + 0.032f, 0.0075f, rgb(1, 1, 1), "EXIT", true);

        const float bx0 = -0.50f, by0 = 0.22f, bx1 = 0.12f, by1 = 0.58f;       // triage monitor on the wall
        setColor(rgb(0.08f, 0.10f, 0.14f, 0.92f)); rect(bx0, by0, bx1, by1);
        setColor(rgb(0.4f, 0.75f, 0.95f)); rect(bx0, by1 - 0.002f, bx1, by1 + 0.006f);
        ui::drawText(bx0 + 0.02f, by1 - 0.045f, 0.0040f, rgb(1.0f, 0.95f, 0.6f), "TRIAGE QUEUE (PRIORITY HEAP)");
        const std::vector<int> order = triageOrder();
        for (int i = 0; i < 6; i++)
        {
            char b[48];
            if (i < (int)order.size())
            {
                const Patient* p = findPatient(order[i]);
                std::snprintf(b, sizeof(b), "%d #%d %s S%d P%.0f", i + 1, p->id, p->kind == Kind::Emergency ? "EMERG" : "GEN", p->severity, priorityOf(*p));
                ui::drawText(bx0 + 0.02f, by1 - 0.095f - i * 0.043f, 0.0044f, p->kind == Kind::Emergency ? rgb(1.0f, 0.55f, 0.5f) : rgb(0.6f, 1.0f, 0.7f), b);
            }
            else if (i == 0) ui::drawText(bx0 + 0.02f, by1 - 0.095f, 0.0044f, rgb(0.6f, 0.7f, 0.8f), "NOBODY WAITING");
        }
        char nx[64]; std::snprintf(nx, sizeof(nx), "BEDS FREE %d  ER QUEUE %d/%d  ROOMS FREE %d", erBedsFree(), erQueueCount(), ER_QUEUE, consultFree());
        ui::drawText(bx0 + 0.02f, by0 + 0.015f, 0.0040f, rgb(0.7f, 0.85f, 1.0f), nx);
    }
    else
    {
        const bool onDoor = ui::mouse.valid && std::fabs(ui::mouse.x - DOOR_X) < 0.14f && ui::mouse.y > DOOR_Y - 0.10f && ui::mouse.y < DOOR_Y + 0.40f && !draggingSomething() && hoverPedestrianId() < 0;
        if (onDoor) { setColor(rgb(0, 0, 0, 0.6f)); rect(-0.30f, -0.04f, 0.30f, 0.03f); ui::drawText(0.0f, -0.025f, 0.0075f, rgb(1, 1, 1), "CLICK TO GO INSIDE", true); }
        else if (!draggingSomething()) ui::drawText(0.0f, -0.97f + 0.0f, 0.0f, rgb(1, 1, 1), "", true);
    }

    // ---- event feed (newest at the bottom) ----
    {
        const std::vector<LogLine>& lg = log();
        const int n = (int)std::min<size_t>(3, lg.size());
        setColor(rgb(0.03f, 0.06f, 0.12f, 0.72f)); rect(-0.66f, -1.0f, WORLD_W + 0.1f, -0.86f);
        for (int i = 0; i < n; i++)
        {
            const LogLine& l = lg[lg.size() - n + i];
            const float a = 0.55f + 0.45f * (float)(i + 1) / n;
            Color c = uiColor(l.color); c.a = a;
            ui::drawText(-0.63f, -0.895f - i * 0.043f, fitPx(l.text, 2.35f, 0.0052f), c, l.text);
        }
        // clearer newest-first order: draw newest on top line instead
    }

    // ---- floating pop-ups ----
    for (const Popup& p : popups())
    {
        if (p.view != view) continue;
        const float k = p.age / 2.6f;
        Color c = uiColor(p.color); c.a = k < 0.7f ? 1.0f : 1.0f - (k - 0.7f) / 0.3f;
        const float px = 0.0062f, w = ui::textWidth(p.text, px) * 0.5f + 0.02f, yy = p.y + 0.12f * k;
        setColor(rgb(0.02f, 0.05f, 0.10f, 0.75f * c.a)); rect(p.x - w, yy - 0.012f, p.x + w, yy + 0.055f);
        ui::drawText(p.x, yy, px, c, p.text, true);
    }

    // ---- hover tooltip ----
    if (ui::mouse.valid && !draggingSomething())
    {
        char t1[64] = "", t2[64] = "", t3[64] = "", t4[64] = "";
        bool show = false;
        if (view == View::Inside && hoverPatientId() >= 0)
        {
            const Patient* p = findPatient(hoverPatientId());
            if (p)
            {
                show = true;
                std::snprintf(t1, sizeof(t1), "#%d %s - %s", p->id, kindName(p->kind), condName(p->cond));
                std::snprintf(t2, sizeof(t2), "SEVERITY %d/5  PRIORITY %.0f  WAITED %.0fS", p->severity, priorityOf(*p), p->waited);
                if (p->cooldown > 0.0f) std::snprintf(t3, sizeof(t3), "ER LOCKED FOR %.0fS", std::ceil(p->cooldown));
                else if (p->st == PState::Treating) std::snprintf(t3, sizeof(t3), "TREATMENT %.0f%%", 100.0f * (1.0f - p->treatLeft / p->treatTotal));
                else std::snprintf(t3, sizeof(t3), p->kind == Kind::Emergency ? "NEEDS THE EMERGENCY ROOM" : "NEEDS GENERAL CONSULTATION");
                std::snprintf(t4, sizeof(t4), "DRAG ME TO AN AREA");
            }
        }
        else if (view == View::Outside && hoverPedestrianId() >= 0)
        {
            const Pedestrian* p = findPedestrian(hoverPedestrianId());
            if (p) { show = true; std::snprintf(t1, sizeof(t1), "PERSON #%d", p->id); std::snprintf(t2, sizeof(t2), "LOOK CLOSELY: DOES THIS PERSON NEED CARE?"); std::snprintf(t3, sizeof(t3), "DRAG ONTO THE HOSPITAL TO CHECK IN"); }
        }
        if (show)
        {
            const float px = 0.0058f;
            float w = std::max(std::max(ui::textWidth(t1, px), ui::textWidth(t2, px)), std::max(ui::textWidth(t3, px), ui::textWidth(t4, px))) + 0.04f;
            float x = clampf(ui::mouse.x + 0.05f, -WORLD_W + 0.02f, WORLD_W - w - 0.02f), y = clampf(ui::mouse.y + 0.05f, -0.80f, 0.60f);
            const int lines = t4[0] ? 4 : 3;
            setColor(rgb(0.02f, 0.05f, 0.10f, 0.88f)); rect(x, y, x + w, y + 0.03f + lines * 0.05f);
            ui::drawText(x + 0.02f, y + 0.03f + (lines - 1) * 0.05f, px, rgb(1.0f, 0.95f, 0.6f), t1);
            ui::drawText(x + 0.02f, y + 0.03f + (lines - 2) * 0.05f, px, rgb(1, 1, 1), t2);
            ui::drawText(x + 0.02f, y + 0.03f + (lines - 3) * 0.05f, px, rgb(0.6f, 0.95f, 1.0f), t3);
            if (t4[0]) ui::drawText(x + 0.02f, y + 0.03f, px, rgb(0.7f, 0.9f, 0.7f), t4);
        }
    }
    gfx::useTint = old;
}

}   // namespace hospital
