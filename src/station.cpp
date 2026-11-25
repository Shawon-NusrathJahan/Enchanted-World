// station.cpp - draws the train station (building, platforms, footbridge, trains, people) and its HUD

#include "station.h"

#include "gfx.h"
#include "theme.h"
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace gfx;

namespace station
{
namespace
{

const Color SKIN[4] = {rgb(0.96f, 0.76f, 0.60f), rgb(0.87f, 0.66f, 0.48f), rgb(0.66f, 0.46f, 0.32f), rgb(0.45f, 0.30f, 0.22f)};
const Color COAT[6] = {rgb(0.22f, 0.30f, 0.48f), rgb(0.62f, 0.24f, 0.22f), rgb(0.30f, 0.45f, 0.32f), rgb(0.55f, 0.48f, 0.38f),
                       rgb(0.40f, 0.40f, 0.44f), rgb(0.70f, 0.55f, 0.20f)};
const Color HAIR[4] = {rgb(0.15f, 0.10f, 0.08f), rgb(0.42f, 0.26f, 0.12f), rgb(0.80f, 0.66f, 0.32f), rgb(0.60f, 0.60f, 0.62f)};
const Color LINE[5] = {rgb(0.82f, 0.18f, 0.20f), rgb(0.20f, 0.45f, 0.80f), rgb(0.18f, 0.60f, 0.38f), rgb(0.55f, 0.30f, 0.70f), rgb(0.90f, 0.55f, 0.12f)};
const float PAX_S   = 0.22f;

Color uiColor(int c)
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

bool night() { return theme::cur().windowsLit > 0.5f; }

void additiveGlow(float x, float y, float r, const Color& c)
{
    const bool old = gfx::useTint; gfx::useTint = false;
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glow(x, y, r, c, 24);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gfx::useTint = old;
}

void selfLitRect(float x0, float y0, float x1, float y1, const Color& c)
{
    const bool old = gfx::useTint; gfx::useTint = false;
    setColor(c); rect(x0, y0, x1, y1);
    gfx::useTint = old;
}

// ---------------------------------------------------------------------------
// Passengers (local units: feet at 0, head top at ~1)
// ---------------------------------------------------------------------------
void commuter(const Passenger& p)
{
    const bool moving = p.st == Passenger::Walk || p.st == Passenger::ToDoor || p.st == Passenger::Board || p.st == Passenger::Exit;
    const float step = moving ? std::sin(p.phase) : 0.0f;
    const float oldA = gfx::alphaMul;
    gfx::alphaMul = oldA * std::max(0.0f, p.alpha);
    const float lift = p.st == Passenger::Dragged ? 0.03f : 0.0f;

    glPushMatrix();
    glTranslatef(p.x, p.y + lift, 0.0f);
    glScalef(PAX_S * p.facing, PAX_S, 1.0f);
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0.0f, 0.0f, 0.20f, 0.03f, 12);
    const Color coat = COAT[p.coat % 6], skin = SKIN[p.skin % 4];
    const Color legs = rgb(0.20f, 0.20f, 0.24f);
    for (int k = 0; k < 2; k++)
    {
        glPushMatrix(); glTranslatef(0.0f, 0.44f, 0.0f);
        glRotatef((k ? -1.0f : 1.0f) * 22.0f * step + (p.st == Passenger::Dragged ? (k ? -10.0f : 10.0f) : 0.0f), 0, 0, 1);
        setColor(k ? shade(legs, 0.8f) : legs); rect(-0.04f, -0.42f, 0.04f, 0.0f);
        setColor(rgb(0.12f, 0.10f, 0.10f)); rect(-0.045f, -0.46f, 0.08f, -0.41f);
        glPopMatrix();
    }
    if (p.look == 2)                                                      // wheeled suitcase behind
    {
        setColor(rgb(0.30f, 0.30f, 0.34f)); thickLine(-0.12f, 0.50f, -0.26f, 0.22f, 0.02f);
        setColor(rgb(0.25f, 0.45f, 0.70f)); rect(-0.38f, 0.02f, -0.20f, 0.26f);
        setColor(rgb(0.10f, 0.10f, 0.10f)); circle(-0.34f, 0.01f, 0.025f, 8); circle(-0.24f, 0.01f, 0.025f, 8);
    }
    setColor(coat); quad(-0.12f, 0.40f, 0.12f, 0.40f, 0.11f, 0.80f, -0.11f, 0.80f);
    setColor(shade(coat, 0.8f)); rect(-0.005f, 0.42f, 0.005f, 0.78f);
    if (p.look == 1) { setColor(rgb(0.35f, 0.25f, 0.18f)); rect(-0.20f, 0.52f, -0.10f, 0.76f); }        // backpack
    glPushMatrix(); glTranslatef(0.0f, 0.76f, 0.0f);                    // arm
    glRotatef(p.st == Passenger::Dragged ? 160.0f : 20.0f * step, 0, 0, 1);
    setColor(shade(coat, 0.9f)); rect(-0.03f, -0.30f, 0.03f, 0.0f);
    setColor(skin); circle(0.0f, -0.32f, 0.035f, 8);
    if (p.look == 3 && p.st != Passenger::Dragged) { setColor(rgb(0.30f, 0.20f, 0.12f)); rect(-0.06f, -0.48f, 0.08f, -0.34f); }  // briefcase
    glPopMatrix();
    setColor(skin); circle(0.0f, 0.90f, 0.085f, 14);
    setColor(HAIR[(p.id + p.skin) % 4]); ellipse(-0.01f, 0.955f, 0.088f, 0.045f, 12); rect(-0.088f, 0.90f, -0.06f, 0.96f);
    if (p.look == 0) { setColor(shade(coat, 0.7f)); rect(-0.10f, 0.95f, 0.10f, 0.99f); rect(-0.07f, 0.99f, 0.07f, 1.05f); }   // hat
    setColor(rgb(0.12f, 0.08f, 0.08f)); circle(0.04f, 0.91f, 0.009f, 6);
    glPopMatrix();
    gfx::alphaMul = oldA;

    if (p.st == Passenger::NoRoute)                                       // "no way through" bubble
    {
        const bool o = gfx::useTint; gfx::useTint = false;
        setColor(rgb(1, 1, 1, 0.9f)); circle(p.x + 0.03f, p.y + 0.29f, 0.025f, 12);
        setColor(rgb(0.85f, 0.2f, 0.2f)); rect(p.x + 0.027f, p.y + 0.282f, p.x + 0.033f, p.y + 0.305f); rect(p.x + 0.027f, p.y + 0.272f, p.x + 0.033f, p.y + 0.278f);
        gfx::useTint = o;
    }
    if (p.flash > 0.0f) additiveGlow(p.x, p.y + 0.12f, 0.08f, rgb(1.0f, 0.95f, 0.4f, 0.5f * p.flash));
    if (p.st == Passenger::Dragged) { setColor(rgb(1, 1, 1, 0.7f)); ring(p.x, p.y - 0.005f, 0.07f, 0.022f, 18); }
}

// which drawing layer a passenger belongs to: 0 platform 1 / P1 stairs, 1 bridge deck, 2 platform 2 / P2 stairs
int layerOf(const Passenger& p)
{
    if (p.st == Passenger::Dragged) return 3;
    int a = -1, b = -1;
    if (p.st == Passenger::Walk && p.pathIdx > 0 && p.pathIdx < (int)p.path.size()) { a = p.path[p.pathIdx - 1]; b = p.path[p.pathIdx]; }
    if ((a == N_BR1 && b == N_BR2) || (a == N_BR2 && b == N_BR1)) return 1;
    if ((a == N_BR2 && b == N_P2W) || (a == N_P2W && b == N_BR2)) return 2;
    return p.y > TRACK_Y[0] + 0.02f ? 0 : 2;
}

// ---------------------------------------------------------------------------
// Backdrop and station structures
// ---------------------------------------------------------------------------
void sky(float t)
{
    const float sunX = -0.95f, sunY = 0.64f;
    Stop stops[] = {{-0.2f, rgb(0.90f, 0.94f, 0.98f)}, {0.3f, rgb(0.72f, 0.86f, 0.98f)}, {1.2f, rgb(0.32f, 0.58f, 0.92f)}};
    for (Stop& s : stops) s.c = theme::skyColor(s.c, s.y);
    const bool old = gfx::useTint;
    gfx::useTint = false;
    vGradient(-WORLD_W - 0.2f, WORLD_W + 0.2f, stops, 3);
    if (theme::cur().sunAlpha > 0.01f)
    {
        glow(sunX, sunY, 0.5f, theme::sun(rgb(1.0f, 0.97f, 0.80f, 0.7f)), 40);
        setColor(theme::sun(rgb(1.0f, 0.97f, 0.78f))); circle(sunX, sunY, 0.08f, 30);
    }
    theme::drawStarsAndMoon(sunX, sunY, 0.08f, fract(t / 60.0f));
    gfx::useTint = old;
    const float u = fract(t / 110.0f);
    drawCloud(-2.0f + 4.2f * u, 0.70f, 0.18f, 0.15f, 0.8f);
    drawCloud(-2.0f + 4.2f * fract(u + 0.55f), 0.58f, 0.13f, 0.25f, 0.7f);
}

void skyline()
{
    static const Ridge hills = makeRolling(-2.4f, 2.4f, 0.04f, 0.18f, 0.14f, 3.3f, 1.6f);
    drawRidge(hills, 0.0f, rgb(0.55f, 0.72f, 0.66f), rgb(0.66f, 0.80f, 0.72f));
    for (int i = 0; i < 16; i++)                                        // city blocks behind the station
    {
        const float x = -0.95f + i * 0.17f, w = 0.07f + 0.05f * hash1(i * 2.7f), h = 0.16f + 0.22f * hash1(i * 5.3f);
        const Color c = mix(rgb(0.62f, 0.66f, 0.74f), rgb(0.74f, 0.72f, 0.70f), hash1(i * 1.9f));
        setColor(c); rect(x - w, 0.20f, x + w, 0.20f + h);
        for (int r = 0; r < (int)(h / 0.05f); r++)
            for (int k = 0; k < 2; k++)
            {
                const float wx = x - w * 0.55f + k * w * 0.9f, wy = 0.23f + r * 0.05f;
                const bool lit = night() && hash1(i * 13.0f + r * 3.0f + k) > 0.35f;
                if (lit) selfLitRect(wx, wy, wx + 0.018f, wy + 0.022f, rgb(1.0f, 0.85f, 0.45f));
                else { setColor(shade(c, 0.82f)); rect(wx, wy, wx + 0.018f, wy + 0.022f); }
            }
    }
}

void lamp(float x, float y, float h)
{
    setColor(rgb(0.25f, 0.27f, 0.30f)); rect(x - 0.006f, y, x + 0.006f, y + h);
    rect(x - 0.006f, y + h - 0.01f, x + 0.05f, y + h);
    setColor(night() ? rgb(1.0f, 0.95f, 0.72f) : rgb(0.85f, 0.88f, 0.92f)); ellipse(x + 0.045f, y + h - 0.015f, 0.022f, 0.01f, 10);
    if (night()) additiveGlow(x + 0.045f, y + h - 0.02f, 0.20f, rgb(1.0f, 0.88f, 0.55f, 0.35f));
}

void building()
{
    const float x0 = -WORLD_W - 0.02f, x1 = -1.02f, y0 = P1_Y + 0.04f, y1 = 0.60f;
    setColor(rgb(0.72f, 0.42f, 0.32f)); rect(x0, y0, x1, y1);                             // brick facade
    setColor(rgb(0.64f, 0.36f, 0.28f));
    for (int r = 0; r < 14; r++) rect(x0, y0 + r * 0.04f, x1, y0 + r * 0.04f + 0.004f);
    setColor(rgb(0.36f, 0.28f, 0.26f)); quad(x0 - 0.02f, y1, x1 + 0.04f, y1, x1 - 0.06f, y1 + 0.08f, x0 + 0.06f, y1 + 0.08f);
    // name sign
    setColor(rgb(0.12f, 0.22f, 0.36f)); rect(x0 + 0.06f, 0.46f, x1 - 0.06f, 0.54f);
    setColor(rgb(0.85f, 0.70f, 0.30f)); rect(x0 + 0.06f, 0.455f, x1 - 0.06f, 0.462f);
    {
        const bool o = gfx::useTint; gfx::useTint = false;
        const float px = fitPx(stationcfg::NAME, x1 - x0 - 0.16f, 0.0072f);
        ui::drawText((x0 + x1) / 2, 0.50f - 3.5f * px, px, night() ? rgb(1.0f, 0.92f, 0.60f) : rgb(1.0f, 0.95f, 0.80f), stationcfg::NAME, true);
        gfx::useTint = o;
    }
    // clock (real local time)
    const float cx = -1.20f, cy = 0.38f;
    setColor(rgb(0.30f, 0.25f, 0.22f)); circle(cx, cy, 0.052f, 24);
    { const bool o = gfx::useTint; gfx::useTint = !night(); setColor(rgb(0.98f, 0.96f, 0.88f)); circle(cx, cy, 0.044f, 24); gfx::useTint = o; }
    const float hr = theme::localHour(), ha = PI / 2 - TAU * std::fmod(hr, 12.0f) / 12.0f, ma = PI / 2 - TAU * (hr - std::floor(hr));
    setColor(rgb(0.15f, 0.12f, 0.12f));
    thickLine(cx, cy, cx + 0.024f * std::cos(ha), cy + 0.024f * std::sin(ha), 0.006f);
    thickLine(cx, cy, cx + 0.036f * std::cos(ma), cy + 0.036f * std::sin(ma), 0.004f);
    // windows
    for (int i = 0; i < 2; i++)
        for (int r = 0; r < 2; r++)
        {
            const float wx = -1.70f + i * 0.40f, wy = 0.24f + r * 0.12f;
            if (i == 1 && r == 1) continue;
            setColor(rgb(0.95f, 0.92f, 0.86f)); rect(wx - 0.008f, wy - 0.008f, wx + 0.088f, wy + 0.088f);
            if (night()) selfLitRect(wx, wy, wx + 0.08f, wy + 0.08f, rgb(1.0f, 0.85f, 0.48f));
            else { setColor(rgb(0.55f, 0.72f, 0.86f)); rect(wx, wy, wx + 0.08f, wy + 0.08f); }
        }
    // entrance (clickable: rush hour)
    setColor(rgb(0.95f, 0.92f, 0.86f)); rect(ENTRY_X - 0.075f, y0, ENTRY_X + 0.075f, y0 + 0.23f); circle(ENTRY_X, y0 + 0.23f, 0.075f, 20);
    setColor(night() ? rgb(0.55f, 0.42f, 0.25f) : rgb(0.30f, 0.22f, 0.18f)); rect(ENTRY_X - 0.06f, y0, ENTRY_X + 0.06f, y0 + 0.22f); circle(ENTRY_X, y0 + 0.22f, 0.06f, 20);
    if (night()) additiveGlow(ENTRY_X, y0 + 0.12f, 0.16f, rgb(1.0f, 0.85f, 0.5f, 0.35f));
    { const bool o = gfx::useTint; gfx::useTint = false; ui::drawText(ENTRY_X, y0 + 0.31f, 0.0042f, rgb(1, 1, 1, 0.9f), "ENTRANCE", true); gfx::useTint = o; }
}

void platformSlab(float x0, float x1, float feetY, float faceBottom)
{
    setColor(rgb(0.50f, 0.50f, 0.52f)); rect(x0, faceBottom, x1, feetY - 0.035f);            // front face
    setColor(rgb(0.42f, 0.42f, 0.45f)); rect(x0, faceBottom, x1, faceBottom + 0.01f);
    setColor(rgb(0.78f, 0.77f, 0.74f)); rect(x0, feetY - 0.035f, x1, feetY + 0.05f);          // surface
    setColor(rgb(0.70f, 0.69f, 0.66f)); for (int i = 0; i < 40; i++) { const float x = x0 + i * 0.09f; if (x < x1) rect(x, feetY - 0.035f, x + 0.003f, feetY + 0.05f); }
    setColor(rgb(0.98f, 0.82f, 0.12f)); rect(x0, feetY - 0.033f, x1, feetY - 0.024f);           // safety line
}

void rails(int track, float x0, float x1)
{
    const float y = TRACK_Y[track];
    setColor(rgb(0.46f, 0.42f, 0.38f)); rect(x0, y - 0.045f, x1, y + 0.02f);                    // ballast
    setColor(rgb(0.36f, 0.28f, 0.22f));
    for (float x = x0; x < x1; x += 0.05f) rect(x, y - 0.012f, x + 0.026f, y + 0.004f);        // sleepers
    setColor(rgb(0.70f, 0.72f, 0.76f)); rect(x0, y + 0.002f, x1, y + 0.010f);                   // rail head
    setColor(rgb(0.40f, 0.40f, 0.44f)); rect(x0, y - 0.002f, x1, y + 0.002f);
}

void crossoverRails()
{
    setColor(rgb(0.46f, 0.42f, 0.38f));
    glBegin(GL_QUAD_STRIP);
    for (float x = PTS_X0 - 0.06f; x <= PTS_X1 + 0.001f; x += 0.01f) { const float y = trackY(0, x); glVertex2f(x, y - 0.045f); glVertex2f(x, y + 0.02f); }
    glEnd();
    setColor(rgb(0.36f, 0.28f, 0.22f));
    for (float x = PTS_X0 - 0.05f; x < PTS_X1; x += 0.05f) { const float y = trackY(0, x); rect(x, y - 0.012f, x + 0.026f, y + 0.004f); }
    setColor(rgb(0.70f, 0.72f, 0.76f));
    glBegin(GL_QUAD_STRIP);
    for (float x = PTS_X0 - 0.06f; x <= PTS_X1 + 0.001f; x += 0.01f) { const float y = trackY(0, x); glVertex2f(x, y + 0.002f); glVertex2f(x, y + 0.010f); }
    glEnd();
}

void signalHead(float x, float baseY, Aspect a, bool home, bool held, float t)
{
    setColor(rgb(0.30f, 0.30f, 0.32f)); rect(x - 0.006f, baseY, x + 0.006f, baseY + 0.28f);
    setColor(rgb(0.10f, 0.10f, 0.12f)); rect(x - 0.026f, baseY + 0.27f, x + 0.026f, baseY + 0.39f);
    const float ry = baseY + 0.365f, gy = baseY + 0.295f;
    const Color off = rgb(0.22f, 0.22f, 0.24f);
    const Color red = rgb(1.0f, 0.18f, 0.15f), other = home ? rgb(1.0f, 0.80f, 0.15f) : rgb(0.25f, 1.0f, 0.40f);
    const bool redOn = a == Aspect::Red, otherOn = a != Aspect::Red;
    { const bool o = gfx::useTint; gfx::useTint = false;
      setColor(redOn ? red : off); circle(x, ry, 0.016f, 14);
      setColor(otherOn ? other : off); circle(x, gy, 0.016f, 14);
      gfx::useTint = o; }
    additiveGlow(x, redOn ? ry : gy, 0.07f, withAlpha(redOn ? red : other, night() ? 0.65f : 0.35f));
    if (held)                                                          // "held" collar
    {
        const bool o = gfx::useTint; gfx::useTint = false;
        setColor(rgb(1.0f, 1.0f, 1.0f, 0.5f + 0.5f * std::sin(t * 6.0f))); ring(x, baseY + 0.33f, 0.05f, 0.075f, 20);
        gfx::useTint = o;
    }
}

void canopyAndBoard(float t)
{
    // columns + roof over platform 1
    for (float x : {-0.30f, 0.25f, 0.80f, 1.26f}) { setColor(rgb(0.30f, 0.36f, 0.42f)); rect(x - 0.012f, P1_Y + 0.04f, x + 0.012f, 0.43f); }
    setColor(rgb(0.24f, 0.30f, 0.36f)); rect(-0.38f, 0.42f, 1.34f, 0.465f);
    setColor(rgb(0.70f, 0.24f, 0.22f)); rect(-0.38f, 0.455f, 1.34f, 0.47f);
    for (float x : {0.0f, 0.55f, 1.05f})
    {
        setColor(night() ? rgb(1.0f, 0.96f, 0.80f) : rgb(0.85f, 0.88f, 0.90f)); rect(x - 0.05f, 0.405f, x + 0.05f, 0.42f);
        if (night()) additiveGlow(x, 0.40f, 0.22f, rgb(1.0f, 0.92f, 0.70f, 0.25f));
    }
    // hanging departure board (self-lit LED panel)
    const float bx0 = 0.28f, bx1 = 1.14f, by0 = 0.515f, by1 = 0.688f;                  // mounted on the canopy roof
    setColor(rgb(0.25f, 0.25f, 0.28f)); rect(0.40f, 0.47f, 0.415f, by0); rect(1.005f, 0.47f, 1.02f, by0);
    selfLitRect(bx0 - 0.008f, by0 - 0.008f, bx1 + 0.008f, by1 + 0.008f, rgb(0.20f, 0.22f, 0.26f));
    selfLitRect(bx0, by0, bx1, by1, rgb(0.03f, 0.04f, 0.08f));
    const bool o = gfx::useTint; gfx::useTint = false;
    const float px = 0.0034f, cName = bx0 + 0.015f, cDest = bx0 + 0.15f, cPlat = bx0 + 0.46f, cDue = bx0 + 0.58f, cLate = bx0 + 0.74f;
    const Color head = rgb(0.55f, 0.75f, 1.0f);
    ui::drawText(cName, by1 - 0.032f, px, head, "TRAIN");
    ui::drawText(cDest, by1 - 0.032f, px, head, "TO");
    ui::drawText(cPlat, by1 - 0.032f, px, head, "PLAT");
    ui::drawText(cDue, by1 - 0.032f, px, head, "DUE");
    ui::drawText(cLate, by1 - 0.032f, px, head, "LATE");
    int row = 0;
    for (const Service& s : services())
    {
        if (s.st == SvcState::Departed) continue;
        if (row >= 4) break;
        const Train* tr = nullptr; for (const Train& x : trains()) if (x.svc == s.id) tr = &x;
        char due[16], plat[8], late[16] = "";
        if (s.st == SvcState::AtPlatform) std::snprintf(due, sizeof(due), tr && tr->closing ? "DEPART" : "BOARD");
        else
        {
            const float eta = s.estArr - now();
            if (eta <= 1.0f) std::snprintf(due, sizeof(due), "ARRIVE");
            else std::snprintf(due, sizeof(due), "%.0fS", eta);
        }
        const bool known = s.confirmed || s.locked || s.st != SvcState::Scheduled;
        std::snprintf(plat, sizeof(plat), known ? "%d" : "%d?", s.platform + 1);
        const float lateBy = (s.actualArr >= 0 ? s.actualArr : s.estArr) - s.planArr;
        if (lateBy > 3.0f) std::snprintf(late, sizeof(late), "+%.0fS", lateBy);
        const float y = by1 - 0.068f - row * 0.033f;
        Color c = rgb(1.0f, 0.80f, 0.30f);
        if (s.flash > 0.0f && std::fmod(t * 4.0f, 1.0f) < 0.6f) c = rgb(1.0f, 1.0f, 1.0f);
        ui::drawText(cName, y, px, c, s.name);
        ui::drawText(cDest, y, px, c, destName(s.dest));
        ui::drawText(cPlat + 0.01f, y, px, known ? c : rgb(0.7f, 0.6f, 0.4f), plat);
        ui::drawText(cDue, y, px, s.st == SvcState::AtPlatform ? rgb(0.55f, 1.0f, 0.60f) : c, due);
        if (late[0]) ui::drawText(cLate, y, px, rgb(1.0f, 0.45f, 0.40f), late);
        row++;
    }
    gfx::useTint = o;
}

void platformSign(float x, float y, const char* n)
{
    setColor(rgb(0.30f, 0.30f, 0.34f)); rect(x - 0.004f, y, x + 0.004f, y + 0.20f);
    setColor(rgb(0.10f, 0.25f, 0.55f)); rect(x - 0.035f, y + 0.18f, x + 0.035f, y + 0.25f);
    const bool o = gfx::useTint; gfx::useTint = false;
    ui::drawText(x, y + 0.196f, 0.0062f, rgb(1, 1, 1), n, true);
    gfx::useTint = o;
}

void bridgeBack()       // platform-1 stairs and the pillar on platform 1
{
    setColor(rgb(0.42f, 0.46f, 0.52f)); rect(BR1_X - 0.035f, P1_Y + 0.03f, BR1_X - 0.01f, BR1_Y);
    // stairs: a slanted band with steps
    const float sx0 = NODE_X[N_P1W], sy0 = P1_Y, sx1 = BR1_X, sy1 = BR1_Y;
    setColor(rgb(0.56f, 0.58f, 0.62f)); quad(sx0 - 0.04f, sy0, sx0 + 0.02f, sy0, sx1 + 0.02f, sy1, sx1 - 0.04f, sy1);
    setColor(rgb(0.44f, 0.46f, 0.50f));
    for (int i = 1; i < 12; i++) { const float u = i / 12.0f, x = sx0 + (sx1 - sx0) * u, y = sy0 + (sy1 - sy0) * u; rect(x - 0.04f, y - 0.004f, x + 0.02f, y); }
    setColor(rgb(0.30f, 0.32f, 0.36f)); thickLine(sx0 - 0.045f, sy0 + 0.10f, sx1 - 0.045f, sy1 + 0.10f, 0.008f);
}

void bridgeDeck()
{
    const bool closed = !bridgeOpen();
    // covered walkway crossing over track 0 (seen end-on it runs "towards" the viewer)
    setColor(rgb(0.30f, 0.38f, 0.46f));
    quad(BR1_X - 0.07f, BR1_Y + 0.10f, BR1_X + 0.07f, BR1_Y + 0.10f, BR2_X + 0.07f, BR2_Y - 0.02f, BR2_X - 0.07f, BR2_Y - 0.02f);
    setColor(rgb(0.62f, 0.66f, 0.70f));
    quad(BR1_X - 0.055f, BR1_Y - 0.01f, BR1_X + 0.055f, BR1_Y - 0.01f, BR2_X + 0.055f, BR2_Y, BR2_X - 0.055f, BR2_Y);
    { const Color glass = night() ? rgb(1.0f, 0.88f, 0.55f, 0.55f) : rgb(0.70f, 0.86f, 0.96f, 0.55f);
      const bool o = gfx::useTint; gfx::useTint = !night();
      setColor(glass);
      quad(BR1_X - 0.055f, BR1_Y + 0.085f, BR1_X + 0.055f, BR1_Y + 0.085f, BR2_X + 0.055f, BR2_Y + 0.065f, BR2_X - 0.055f, BR2_Y + 0.065f);
      gfx::useTint = o; }
    setColor(rgb(0.24f, 0.30f, 0.36f)); thickLine(BR1_X - 0.07f, BR1_Y + 0.10f, BR1_X + 0.07f, BR1_Y + 0.10f, 0.02f);
    setColor(rgb(0.70f, 0.24f, 0.22f)); thickLine(BR1_X - 0.075f, BR1_Y + 0.115f, BR1_X + 0.075f, BR1_Y + 0.115f, 0.012f);
    if (closed)
    {
        const bool o = gfx::useTint; gfx::useTint = false;
        for (int i = 0; i < 2; i++)
        {
            const float y = i ? BR2_Y + 0.04f : BR1_Y - 0.02f, x = i ? BR2_X : BR1_X;
            for (int k = 0; k < 4; k++) { setColor(k % 2 ? rgb(1, 1, 1) : rgb(0.9f, 0.15f, 0.15f)); rect(x - 0.06f + k * 0.03f, y, x - 0.03f + k * 0.03f, y + 0.02f); }
        }
        ui::drawText((BR1_X + BR2_X) / 2, (BR1_Y + BR2_Y) / 2, 0.0040f, rgb(1.0f, 0.4f, 0.35f), "CLOSED", true);
        gfx::useTint = o;
    }
}

void bridgeFront()      // platform-2 stairs and pillar (in front of track 0)
{
    setColor(rgb(0.42f, 0.46f, 0.52f)); rect(BR2_X - 0.06f, P2_Y + 0.03f, BR2_X - 0.035f, BR2_Y);
    const float sx0 = NODE_X[N_P2W], sy0 = P2_Y, sx1 = BR2_X, sy1 = BR2_Y;
    setColor(rgb(0.58f, 0.60f, 0.64f)); quad(sx0 - 0.02f, sy0, sx0 + 0.05f, sy0, sx1 + 0.05f, sy1, sx1 - 0.02f, sy1);
    setColor(rgb(0.46f, 0.48f, 0.52f));
    for (int i = 1; i < 12; i++) { const float u = i / 12.0f, x = sx0 + (sx1 - sx0) * u, y = sy0 + (sy1 - sy0) * u; rect(x - 0.02f, y - 0.004f, x + 0.05f, y); }
}

void bridgeFrontRail()
{
    const float sx0 = NODE_X[N_P2W], sy0 = P2_Y, sx1 = BR2_X, sy1 = BR2_Y;
    setColor(rgb(0.30f, 0.32f, 0.36f)); thickLine(sx0 + 0.055f, sy0 + 0.10f, sx1 + 0.055f, sy1 + 0.10f, 0.008f);
    for (int i = 0; i <= 4; i++) { const float u = i / 4.0f; thickLine(sx0 + 0.055f + (sx1 - sx0) * u, sy0 + (sy1 - sy0) * u, sx0 + 0.055f + (sx1 - sx0) * u, sy0 + (sy1 - sy0) * u + 0.10f, 0.005f); }
}

void levelCrossing(float t, bool behindTrack)
{
    const bool open = crossingOpen();
    if (behindTrack)
    {
        setColor(rgb(0.55f, 0.42f, 0.28f)); rect(XING_X - 0.06f, TRACK_Y[0] - 0.05f, XING_X + 0.06f, P1_Y - 0.035f);      // planks
        setColor(rgb(0.45f, 0.34f, 0.22f)); for (int i = 0; i < 6; i++) { const float y = TRACK_Y[0] - 0.05f + i * 0.03f; rect(XING_X - 0.06f, y, XING_X + 0.06f, y + 0.004f); }
        return;
    }
    // gates on both sides
    for (int side = 0; side < 2; side++)
    {
        const float px = XING_X + (side ? 0.09f : -0.09f), py = side ? P2_Y : P1_Y;
        setColor(rgb(0.30f, 0.30f, 0.32f)); rect(px - 0.008f, py, px + 0.008f, py + 0.12f);
        glPushMatrix(); glTranslatef(px, py + 0.10f, 0.0f); glRotatef(open ? (side ? -80.0f : 80.0f) : 0.0f, 0, 0, 1);
        for (int k = 0; k < 5; k++) { setColor(k % 2 ? rgb(1, 1, 1) : rgb(0.9f, 0.15f, 0.15f)); rect((side ? -1.0f : 1.0f) * k * 0.035f - (side ? 0.035f : 0.0f), -0.008f, (side ? -1.0f : 1.0f) * k * 0.035f + (side ? 0.0f : 0.035f), 0.008f); }
        glPopMatrix();
        if (!open && std::fmod(t * 2.0f + side * 0.5f, 1.0f) < 0.5f)
        {
            const bool o = gfx::useTint; gfx::useTint = false;
            setColor(rgb(1.0f, 0.15f, 0.12f)); circle(px, py + 0.14f, 0.012f, 10);
            gfx::useTint = o;
            additiveGlow(px, py + 0.14f, 0.06f, rgb(1.0f, 0.2f, 0.1f, 0.6f));
        }
    }
}

void pointsLever()
{
    const float x = PTS_X0 + 0.10f, y = TRACK_Y[1] - 0.10f;
    setColor(rgb(0.30f, 0.30f, 0.34f)); rect(x - 0.05f, y - 0.03f, x + 0.05f, y + 0.01f);
    setColor(rgb(0.95f, 0.82f, 0.15f)); rect(x - 0.05f, y - 0.03f, x + 0.05f, y - 0.022f);
    const int tr = pointsTrack();
    setColor(pointsLocked() ? rgb(0.85f, 0.25f, 0.20f) : rgb(0.85f, 0.85f, 0.88f));
    thickLine(x, y, x + (tr == 0 ? 0.04f : -0.04f), y + 0.07f, 0.010f);
    circle(x + (tr == 0 ? 0.04f : -0.04f), y + 0.07f, 0.012f, 10);
    const bool o = gfx::useTint; gfx::useTint = false;
    ui::drawText(x, y - 0.065f, 0.0040f, rgb(1, 1, 1, 0.9f), tr == 0 ? "POINTS: P1" : "POINTS: P2", true);
    gfx::useTint = o;
    // the moving switch blade at the start of the crossover
    setColor(rgb(0.85f, 0.87f, 0.90f));
    if (tr == 0) thickLine(PTS_X0 - 0.10f, TRACK_Y[1] + 0.006f, PTS_X0 + 0.02f, trackY(0, PTS_X0 + 0.02f) + 0.006f, 0.008f);
    else         thickLine(PTS_X0 - 0.10f, TRACK_Y[1] + 0.006f, PTS_X0 + 0.02f, TRACK_Y[1] + 0.006f, 0.008f);
}

// ---------------------------------------------------------------------------
// Trains
// ---------------------------------------------------------------------------
void drawCar(const Train& tr, const Service& s, int c, float t)
{
    const float xf = tr.x - c * (CAR_L + CAR_GAP), xr = xf - CAR_L;
    if (xf < -WORLD_W - 0.3f || xr > WORLD_W + 0.3f) return;
    const float yr = trackY(tr.track, xr), yf = trackY(tr.track, xf);
    const float k = (yf - yr) / CAR_L;                                       // shear on the crossover
    GLfloat m[16] = {1, k, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  xr, yr, 0, 1};
    glPushMatrix(); glMultMatrixf(m);
    const Color body = rgb(0.94f, 0.94f, 0.92f), line = LINE[s.dest % 5];
    const float L = CAR_L, b = 0.028f, top = CAR_H;
    // bogies + wheels
    for (int w = 0; w < 4; w++)
    {
        const float wx = (w < 2 ? 0.06f : L - 0.10f) + (w % 2) * 0.045f;
        setColor(rgb(0.14f, 0.14f, 0.16f)); circle(wx, 0.016f, 0.017f, 12);
        setColor(rgb(0.55f, 0.56f, 0.60f)); circle(wx, 0.016f, 0.007f, 8);
    }
    setColor(rgb(0.22f, 0.22f, 0.26f)); rect(0.03f, 0.018f, L - 0.03f, b);
    // body
    setColor(body);
    if (c == 0) { quad(0.0f, b, L - 0.06f, b, L - 0.06f, top, 0.0f, top); quad(L - 0.06f, b, L, b, L, top - 0.10f, L - 0.06f, top); }   // lead car nose
    else rect(0.0f, b, L, top);
    setColor(line); rect(0.0f, b + 0.02f, c == 0 ? L : L, b + 0.045f);
    setColor(shade(line, 0.8f)); rect(0.0f, top - 0.022f, c == 0 ? L - 0.06f : L, top);
    // windows (lit at night) with passengers' heads
    const float wy0 = b + 0.10f, wy1 = top - 0.04f;
    const float fill = std::min(1.0f, (float)s.load / stationcfg::CAPACITY);
    for (int w = 0; w < 4; w++)
    {
        const float wx0 = 0.035f + w * 0.095f, wx1 = wx0 + 0.06f;
        if (wx1 > L - (c == 0 ? 0.07f : 0.02f)) break;
        if (w == 1 || w == 3) continue;                                     // door positions
        if (night()) selfLitRect(wx0, wy0, wx1, wy1, rgb(1.0f, 0.90f, 0.62f));
        else { setColor(rgb(0.45f, 0.60f, 0.72f)); rect(wx0, wy0, wx1, wy1); }
        if (hash1((float)(s.id * 7 + c * 3 + w)) < fill) { setColor(rgb(0.25f, 0.20f, 0.18f, 0.8f)); circle(wx0 + 0.03f, wy0 + 0.035f, 0.014f, 10); rect(wx0 + 0.012f, wy0, wx0 + 0.048f, wy0 + 0.022f); }
    }
    if (c == 0)                                                              // cab window + headlight
    {
        setColor(rgb(0.25f, 0.35f, 0.45f)); quad(L - 0.055f, top - 0.04f, L - 0.008f, top - 0.10f, L - 0.008f, top - 0.12f, L - 0.055f, top - 0.12f);
        { const bool o = gfx::useTint; gfx::useTint = false; setColor(rgb(1.0f, 0.98f, 0.85f)); circle(L - 0.012f, b + 0.06f, 0.008f, 8); gfx::useTint = o; }
    }
    // doors slide open
    for (int d = 0; d < 2; d++)
    {
        const float dc = L * 0.5f + (d ? 0.11f : -0.11f), hw = 0.026f, open = tr.doors * hw;
        setColor(rgb(0.15f, 0.15f, 0.18f)); rect(dc - hw, b + 0.005f, dc + hw, top - 0.03f);
        if (tr.doors > 0.01f && night()) selfLitRect(dc - open, b + 0.005f, dc + open, top - 0.03f, rgb(1.0f, 0.88f, 0.60f, 0.9f));
        setColor(shade(line, 1.05f));
        rect(dc - hw - open, b + 0.005f, dc - open, top - 0.03f);
        rect(dc + open, b + 0.005f, dc + hw + open, top - 0.03f);
        setColor(rgb(0.55f, 0.65f, 0.75f));
        rect(dc - hw - open + 0.006f, wy0, dc - open - 0.004f, wy1);
        rect(dc + open + 0.004f, wy0, dc + hw + open - 0.006f, wy1);
    }
    // pantograph on the lead car roof
    if (c == 0) { setColor(rgb(0.30f, 0.30f, 0.34f)); thickLine(0.10f, top, 0.16f, top + 0.05f, 0.006f); thickLine(0.16f, top + 0.05f, 0.22f, top, 0.006f); rect(0.12f, top + 0.05f, 0.20f, top + 0.056f); }
    if (c == CARS - 1) { setColor(rgb(0.8f, 0.1f, 0.1f)); rect(0.0f, b + 0.03f, 0.008f, b + 0.05f); }   // tail lamp
    glPopMatrix();
    if (c == 0 && (night() || theme::cur().windowsLit > 0.2f)) additiveGlow(xf + 0.04f, yf + b + 0.06f, 0.16f, rgb(1.0f, 0.95f, 0.75f, 0.45f));
    (void)t;
}

void drawTrain(const Train& tr, float t)
{
    const Service* s = findService(tr.svc);
    if (!s) return;
    for (int c = CARS - 1; c >= 0; c--) drawCar(tr, *s, c, t);
    // destination label above the lead car while visible
    const float yf = trackY(tr.track, tr.x);
    if (tr.x > -WORLD_W && tr.x < WORLD_W + 0.2f)
    {
        char b[32]; std::snprintf(b, sizeof(b), "%s %s", s->name, destName(s->dest));
        const bool o = gfx::useTint; gfx::useTint = false;
        const float px = 0.0034f, w = ui::textWidth(b, px) + 0.02f, x = std::min(tr.x - 0.07f, WORLD_W - 0.02f) - w;
        setColor(rgb(0.02f, 0.03f, 0.06f, 0.85f)); rect(x, yf + CAR_H - 0.065f, x + w, yf + CAR_H - 0.035f);
        ui::drawText(x + 0.01f, yf + CAR_H - 0.06f, px, rgb(1.0f, 0.8f, 0.3f), b);
        gfx::useTint = o;
    }
}

void drawPaxLayer(int layer)
{
    std::vector<const Passenger*> v;
    for (const Passenger& p : passengers()) if (layerOf(p) == layer) v.push_back(&p);
    std::sort(v.begin(), v.end(), [](const Passenger* a, const Passenger* b) { return a->y > b->y; });
    for (const Passenger* p : v) commuter(*p);
}

void dropHighlight()
{
    if (!dragging() || !ui::mouse.valid) return;
    const bool o = gfx::useTint; gfx::useTint = false;
    const float y = ui::mouse.y - 0.0f;
    if (std::fabs(y - P1_Y) < 0.13f) { setColor(rgb(0.3f, 1.0f, 0.5f, 0.20f)); rect(P1_X0, P1_Y - 0.04f, P1_X1, P1_Y + 0.06f); }
    else if (std::fabs(y - P2_Y) < 0.10f) { setColor(rgb(0.3f, 1.0f, 0.5f, 0.20f)); rect(P2_X0, P2_Y - 0.04f, P2_X1, P2_Y + 0.06f); }
    else { setColor(rgb(1.0f, 0.3f, 0.3f, 0.18f)); rect(-WORLD_W, y - 0.05f, WORLD_W, y + 0.05f); }
    gfx::useTint = o;
}

}   // namespace

// ---------------------------------------------------------------------------
// public
// ---------------------------------------------------------------------------
void draw(float t)
{
    sky(t);
    skyline();
    // back wall / ground behind platform 1
    rectV(-WORLD_W - 0.1f, P1_Y + 0.04f, WORLD_W + 0.1f, 0.22f, rgb(0.56f, 0.58f, 0.58f), rgb(0.62f, 0.64f, 0.62f));
    setColor(rgb(0.50f, 0.52f, 0.52f)); for (int i = 0; i < 30; i++) rect(-1.8f + i * 0.12f, P1_Y + 0.04f, -1.795f + i * 0.12f, 0.22f);
    building();
    // trackbed / gravel between and under the platforms
    rectV(-WORLD_W - 0.1f, TRACK_Y[1] - 0.06f, WORLD_W + 0.1f, P1_Y - 0.03f, rgb(0.40f, 0.37f, 0.34f), rgb(0.48f, 0.45f, 0.41f));
    setColor(rgb(0.36f, 0.33f, 0.30f));
    for (int i = 0; i < 120; i++) { const float x = -1.78f + 3.56f * hash1(i * 1.3f), y = TRACK_Y[1] - 0.05f + (P1_Y - TRACK_Y[1]) * hash1(i * 4.1f); rect(x, y, x + 0.008f, y + 0.005f); }

    // ---- platform 1 (back) ----
    platformSlab(P1_X0, P1_X1, P1_Y, TRACK_Y[0] + 0.03f);
    setColor(rgb(0.50f, 0.50f, 0.52f)); rect(P1_X1, TRACK_Y[0] + 0.03f, WORLD_W + 0.1f, P1_Y - 0.035f);     // ramp end
    setColor(rgb(0.72f, 0.71f, 0.68f)); rect(P1_X1, P1_Y - 0.035f, WORLD_W + 0.1f, P1_Y + 0.05f);
    canopyAndBoard(t);
    platformSign(-0.15f, P1_Y + 0.02f, "1");
    // turnstile gate
    setColor(rgb(0.35f, 0.38f, 0.42f)); rect(NODE_X[N_GATE] - 0.03f, P1_Y + 0.02f, NODE_X[N_GATE] - 0.018f, P1_Y + 0.13f); rect(NODE_X[N_GATE] + 0.018f, P1_Y + 0.02f, NODE_X[N_GATE] + 0.03f, P1_Y + 0.13f);
    { const bool o = gfx::useTint; gfx::useTint = false; setColor(rgb(0.3f, 1.0f, 0.4f)); circle(NODE_X[N_GATE] - 0.024f, P1_Y + 0.14f, 0.006f, 8); gfx::useTint = o; }
    signalHead(STARTER_X + 0.04f, TRACK_Y[0] - 0.04f, starterAspect(0), false, starterHeld(0), t);
    bridgeBack();
    drawPaxLayer(0);
    bridgeDeck();
    drawPaxLayer(1);

    // ---- track 0 ----
    rails(0, PTS_X1, WORLD_W + 0.1f);
    crossoverRails();
    levelCrossing(t, true);
    for (const Train& tr : trains()) if (tr.routed && tr.track == 0) drawTrain(tr, t);

    // ---- platform 2 (island) ----
    platformSlab(P2_X0, P2_X1, P2_Y, TRACK_Y[1] + 0.03f);
    setColor(rgb(0.50f, 0.50f, 0.52f)); tri(P2_X0, TRACK_Y[1] + 0.03f, P2_X0, P2_Y + 0.05f, P2_X0 - 0.10f, TRACK_Y[1] + 0.03f);
    setColor(rgb(0.50f, 0.50f, 0.52f)); rect(P2_X1, TRACK_Y[1] + 0.03f, XING_X + 0.07f, P2_Y - 0.035f);
    setColor(rgb(0.72f, 0.71f, 0.68f)); rect(P2_X1, P2_Y - 0.035f, XING_X + 0.07f, P2_Y + 0.05f);
    levelCrossing(t, false);
    bridgeFront();
    lamp(0.10f, P2_Y + 0.03f, 0.36f);
    lamp(0.95f, P2_Y + 0.03f, 0.36f);
    platformSign(0.55f, P2_Y + 0.02f, "2");
    // bench
    setColor(rgb(0.30f, 0.30f, 0.32f)); rect(0.70f, P2_Y + 0.02f, 0.71f, P2_Y + 0.07f); rect(0.85f, P2_Y + 0.02f, 0.86f, P2_Y + 0.07f);
    setColor(rgb(0.55f, 0.36f, 0.20f)); rect(0.68f, P2_Y + 0.065f, 0.88f, P2_Y + 0.08f); rect(0.68f, P2_Y + 0.10f, 0.88f, P2_Y + 0.115f);
    signalHead(STARTER_X + 0.04f, TRACK_Y[1] - 0.04f, starterAspect(1), false, starterHeld(1), t);
    drawPaxLayer(2);
    bridgeFrontRail();

    // ---- track 1 (front) + approach, points, home signal ----
    rails(1, -WORLD_W - 0.1f, WORLD_W + 0.1f);
    for (const Train& tr : trains()) if (!(tr.routed && tr.track == 0)) drawTrain(tr, t);
    // foreground: grass strip and fence
    rectV(-WORLD_W - 0.1f, -1.0f, WORLD_W + 0.1f, TRACK_Y[1] - 0.045f, rgb(0.34f, 0.52f, 0.30f), rgb(0.44f, 0.62f, 0.36f));
    setColor(rgb(0.30f, 0.32f, 0.34f));
    for (int i = 0; i < 40; i++) rect(-1.80f + i * 0.09f, -0.84f, -1.792f + i * 0.09f, -0.74f);
    rect(-WORLD_W - 0.1f, -0.76f, WORLD_W + 0.1f, -0.755f); rect(-WORLD_W - 0.1f, -0.80f, WORLD_W + 0.1f, -0.795f);
    for (int i = 0; i < 9; i++) drawBush(-1.70f + i * 0.42f, -0.88f, 0.05f, rgb(0.16f, 0.40f, 0.22f), rgb(0.28f, 0.56f, 0.30f));
    pointsLever();
    signalHead(HOME_X, TRACK_Y[1] - 0.04f, homeAspect(), true, homeHeld(), t);

    dropHighlight();
    drawPaxLayer(3);
}

void drawOverlay()
{
    const bool old = gfx::useTint;
    gfx::useTint = false;
    const Stats& s = stats();

    // ---- statistics bar ----
    const float x0 = -1.18f, x1 = 1.55f;
    setColor(rgb(0.04f, 0.06f, 0.14f, 0.74f)); rect(x0, 0.795f, x1, 1.0f);
    setColor(rgb(1.0f, 0.75f, 0.30f, 0.9f)); rect(x0, 0.793f, x1, 0.797f);
    char l1[96], l2[160], l3[160];
    std::snprintf(l1, sizeof(l1), "%s  -  DISPATCHER VIEW", stationcfg::NAME);
    std::snprintf(l2, sizeof(l2), "TRAINS: ARRIVED %d  DEPARTED %d  ON TIME %d  LATE %d    PASSENGERS: BOARDED %d  ALIGHTED %d  MISSED %d",
                  s.arrived, s.departed, s.onTime, s.delayed, s.boarded, s.alighted, s.missed);
    std::snprintf(l3, sizeof(l3), "WAITING P1 %d  P2 %d   BRIDGE %s  CROSSING %s   DIJKSTRA RE-ROUTES %d   PLATFORM CHANGES %d",
                  waitingOn(0), waitingOn(1), bridgeOpen() ? "OPEN" : "CLOSED", crossingOpen() ? "OPEN" : "CLOSED", s.reroutes, s.platformChanges);
    ui::drawText(x0 + 0.03f, 0.925f, fitPx(l1, 2.0f, 0.0090f), rgb(1.0f, 0.90f, 0.55f), l1);
    ui::drawText(x0 + 0.03f, 0.865f, fitPx(l2, 2.65f, 0.0068f), rgb(1, 1, 1), l2);
    ui::drawText(x0 + 0.03f, 0.812f, fitPx(l3, 2.65f, 0.0068f), rgb(0.85f, 0.92f, 1.0f), l3);

    // ---- route of the passenger under the cursor (the Dijkstra path) ----
    const int hp = dragging() ? -1 : hoverPassengerId();
    if (hp >= 0)
        if (const Passenger* p = findPassenger(hp))
        {
            glLineWidth(3.0f);
            glColor4f(1.0f, 0.90f, 0.25f, 0.9f);
            glBegin(GL_LINE_STRIP);
            glVertex2f(p->x, p->y + 0.01f);
            for (size_t i = p->pathIdx; i < p->path.size(); i++) glVertex2f(NODE_X[p->path[i]], NODE_Y[p->path[i]] + 0.01f);
            if (p->ticket >= 0 && p->door >= 0)
                if (const Service* sv = findService(p->ticket)) { float dx, dy; doorPos(sv->platform, p->door, dx, dy); glVertex2f(dx, dy + 0.01f); }
            glEnd();
            glLineWidth(1.0f);
            for (size_t i = p->pathIdx; i < p->path.size(); i++) { setColor(rgb(1.0f, 0.9f, 0.25f)); circle(NODE_X[p->path[i]], NODE_Y[p->path[i]] + 0.01f, 0.012f, 10); }
        }

    // ---- event feed (newest at the bottom) ----
    {
        const std::vector<LogLine>& lg = log();
        const int n = (int)std::min<size_t>(3, lg.size());
        setColor(rgb(0.03f, 0.05f, 0.12f, 0.74f)); rect(-0.66f, -1.0f, WORLD_W + 0.1f, -0.86f);
        for (int i = 0; i < n; i++)
        {
            const LogLine& l = lg[lg.size() - n + i];
            Color c = uiColor(l.color); c.a = 0.55f + 0.45f * (float)(i + 1) / n;
            ui::drawText(-0.63f, -0.895f - i * 0.043f, fitPx(l.text, 2.35f, 0.0052f), c, l.text);
        }
    }

    // ---- floating pop-ups ----
    for (const Popup& p : popups())
    {
        const float k = p.age / 2.6f;
        Color c = uiColor(p.color); c.a = k < 0.7f ? 1.0f : 1.0f - (k - 0.7f) / 0.3f;
        const float px = 0.0058f, w = ui::textWidth(p.text, px) * 0.5f + 0.02f, yy = std::min(0.70f, p.y + 0.12f * k);
        const float cx = clampf(p.x, -WORLD_W + w + 0.02f, WORLD_W - w - 0.02f);
        setColor(rgb(0.02f, 0.04f, 0.10f, 0.78f * c.a)); rect(cx - w, yy - 0.012f, cx + w, yy + 0.055f);
        ui::drawText(cx, yy, px, c, p.text, true);
    }

    // ---- drag hint ----
    if (dragging() && ui::mouse.valid)
    {
        const char* b = std::fabs(ui::mouse.y - P1_Y) < 0.13f ? "DROP ON PLATFORM 1 - DIJKSTRA FINDS A NEW ROUTE"
                      : std::fabs(ui::mouse.y - P2_Y) < 0.10f ? "DROP ON PLATFORM 2 - DIJKSTRA FINDS A NEW ROUTE" : "NOT HERE - THAT IS THE TRACK!";
        const float px = 0.0058f, w = ui::textWidth(b, px) + 0.04f;
        const float x = clampf(ui::mouse.x + 0.06f, -WORLD_W + 0.02f, WORLD_W - w - 0.02f), y = clampf(ui::mouse.y + 0.06f, -0.80f, 0.70f);
        setColor(rgb(0.02f, 0.06f, 0.10f, 0.88f)); rect(x, y, x + w, y + 0.07f);
        ui::drawText(x + 0.02f, y + 0.02f, px, std::fabs(ui::mouse.y - P1_Y) < 0.13f || std::fabs(ui::mouse.y - P2_Y) < 0.10f ? rgb(0.6f, 1.0f, 0.6f) : rgb(1.0f, 0.5f, 0.45f), b);
    }

    // ---- hover tooltip ----
    if (ui::mouse.valid && !dragging())
    {
        char t1[96] = "", t2[96] = "", t3[96] = "";
        bool show = true;
        const int thing = hoverThing();
        if (hp >= 0)
        {
            const Passenger* p = findPassenger(hp);
            const Service* sv = p->ticket >= 0 ? findService(p->ticket) : nullptr;
            if (sv) std::snprintf(t1, sizeof(t1), "PASSENGER #%d  TICKET %s TO %s  PLATFORM %d", p->id, sv->name, destName(sv->dest), sv->platform + 1);
            else    std::snprintf(t1, sizeof(t1), "PASSENGER #%d  ARRIVED - WALKING TO THE EXIT", p->id);
            if (p->st == Passenger::NoRoute) std::snprintf(t2, sizeof(t2), "NO ROUTE: BRIDGE AND CROSSING CLOSED - WAITING");
            else if (p->st == Passenger::Wait) std::snprintf(t2, sizeof(t2), "WAITING AT DOOR %d (FIFO, %d AT THIS DOOR)", p->door + 1, sv ? doorLoad(sv->platform, p->door) : 0);
            else std::snprintf(t2, sizeof(t2), "DIJKSTRA ROUTE: %s, %.0fS WALK", p->viaBridge ? "VIA FOOTBRIDGE" : p->viaCrossing ? "VIA LEVEL CROSSING" : "ALONG THE PLATFORM", p->routeCost);
            std::snprintf(t3, sizeof(t3), "DRAG ME TO ANOTHER SPOT - I WILL RE-ROUTE");
        }
        else if (thing == 1)
        {
            std::snprintf(t1, sizeof(t1), "HOME SIGNAL: %s%s", homeAspect() == Aspect::Red ? "RED (STOP)" : "YELLOW (ROUTE SET)", homeHeld() ? " - HELD BY YOU" : "");
            std::snprintf(t2, sizeof(t2), "INTERLOCKING: RED WHILE POINTS OR PLATFORM ARE OCCUPIED");
            std::snprintf(t3, sizeof(t3), "CLICK TO %s ARRIVING TRAINS", homeHeld() ? "RELEASE" : "HOLD");
        }
        else if (thing == 2 || thing == 3)
        {
            const int pl = thing - 2;
            std::snprintf(t1, sizeof(t1), "STARTER SIGNAL PLATFORM %d: %s%s", pl + 1, starterAspect(pl) == Aspect::Green ? "GREEN" : "RED", starterHeld(pl) ? " - HELD" : "");
            std::snprintf(t2, sizeof(t2), "GREEN ONLY WHEN BOARDING IS DONE AND THE EXIT IS CLEAR");
            std::snprintf(t3, sizeof(t3), "CLICK TO %s THE TRAIN AT PLATFORM %d", starterHeld(pl) ? "RELEASE" : "HOLD", pl + 1);
        }
        else if (thing == 4)
        {
            std::snprintf(t1, sizeof(t1), "POINTS: SET FOR PLATFORM %d%s", pointsTrack() + 1, pointsLocked() ? " - LOCKED (TRAIN ON THEM)" : "");
            std::snprintf(t2, sizeof(t2), "PLANNER: GREEDY INTERVAL PARTITIONING OF PLATFORMS");
            std::snprintf(t3, sizeof(t3), "CLICK TO SEND THE NEXT TRAIN TO THE OTHER PLATFORM");
        }
        else if (thing == 5)
        {
            std::snprintf(t1, sizeof(t1), "FOOTBRIDGE: %s", bridgeOpen() ? "OPEN" : "CLOSED FOR MAINTENANCE");
            std::snprintf(t2, sizeof(t2), "STAIRS COST %.1fX WALKING TIME IN DIJKSTRA", stationcfg::STAIR_FACTOR);
            std::snprintf(t3, sizeof(t3), "CLICK TO %s IT - EVERYONE RE-ROUTES", bridgeOpen() ? "CLOSE" : "OPEN");
        }
        else if (thing == 6)
        {
            std::snprintf(t1, sizeof(t1), "STATION ENTRANCE");
            std::snprintf(t2, sizeof(t2), "PASSENGERS ARRIVE AS A POISSON PROCESS (MEAN %.1fS)", stationcfg::ARRIVAL_MEAN);
            std::snprintf(t3, sizeof(t3), "CLICK FOR A RUSH: 5 MORE PASSENGERS");
        }
        else if (thing == 7)
        {
            std::snprintf(t1, sizeof(t1), "LEVEL CROSSING: %s", crossingOpen() ? "OPEN" : "GATES DOWN - TRAIN PASSING");
            std::snprintf(t2, sizeof(t2), "WHEN CLOSED ITS EDGE COSTS INFINITY IN DIJKSTRA");
            std::snprintf(t3, sizeof(t3), "PASSENGERS USE THE BRIDGE OR WAIT");
        }
        else if (hoverTrainSvc() >= 0)
        {
            const Service* sv = findService(hoverTrainSvc());
            const Train* tr = nullptr; for (const Train& x : trains()) if (x.svc == sv->id) tr = &x;
            std::snprintf(t1, sizeof(t1), "%s TO %s  LOAD %d/%d", sv->name, destName(sv->dest), sv->load, stationcfg::CAPACITY);
            if (tr->st == Train::Dwell) std::snprintf(t2, sizeof(t2), "AT PLATFORM %d  DWELL %.0f/%.0fS  DOORS %s", tr->track + 1, tr->t, sv->dwell, tr->closing ? "CLOSING" : "OPEN");
            else std::snprintf(t2, sizeof(t2), "SPEED %.2f U/S  BRAKES FOR X=%.2f (A = V2/2D)", tr->v, tr->target);
            std::snprintf(t3, sizeof(t3), tr->st == Train::Dwell ? "CLICK TO DISPATCH NOW" : "PLATFORM %d", tr->track + 1);
        }
        else show = false;
        if (show)
        {
            const float px = 0.0054f;
            const float w = std::max(ui::textWidth(t1, px), std::max(ui::textWidth(t2, px), ui::textWidth(t3, px))) + 0.04f;
            const float x = clampf(ui::mouse.x + 0.05f, -WORLD_W + 0.02f, WORLD_W - w - 0.02f), y = clampf(ui::mouse.y + 0.05f, -0.80f, 0.60f);
            setColor(rgb(0.02f, 0.04f, 0.10f, 0.90f)); rect(x, y, x + w, y + 0.18f);
            ui::drawText(x + 0.02f, y + 0.13f, px, rgb(1.0f, 0.90f, 0.55f), t1);
            ui::drawText(x + 0.02f, y + 0.08f, px, rgb(1, 1, 1), t2);
            ui::drawText(x + 0.02f, y + 0.03f, px, rgb(0.6f, 0.95f, 1.0f), t3);
        }
    }
    gfx::useTint = old;
}

}   // namespace station
