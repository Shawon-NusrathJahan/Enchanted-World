// game_hud.cpp - draws the game HUD: score panel, flying "+N" tokens, event feed

#include "game.h"

#include "font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace gfx;

namespace game
{
namespace
{

void roundRect(float x0, float y0, float x1, float y1, float r, const Color& c)
{
    glColor4f(c.r, c.g, c.b, c.a);
    rect(x0 + r, y0, x1 - r, y1);
    rect(x0, y0 + r, x0 + r, y1 - r);
    rect(x1 - r, y0 + r, x1, y1 - r);
    const float cs[4][2] = {{x0 + r, y0 + r}, {x1 - r, y0 + r}, {x1 - r, y1 - r}, {x0 + r, y1 - r}};
    for (int k = 0; k < 4; k++)
    {
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cs[k][0], cs[k][1]);
        for (int i = 0; i <= 8; i++)
        {
            const float a = PI * 0.5f * (k + i / 8.0f) + PI;
            glVertex2f(cs[k][0] + std::cos(a) * r, cs[k][1] + std::sin(a) * r);
        }
        glEnd();
    }
}

void star(float x, float y, float r, const Color& c)
{
    glColor4f(c.r, c.g, c.b, c.a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x, y);
    for (int i = 0; i <= 10; i++)
    {
        const float a = PI * 0.5f + TAU * i / 10.0f, rr = (i & 1) ? r * 0.45f : r;
        glVertex2f(x + std::cos(a) * rr, y + std::sin(a) * rr);
    }
    glEnd();
}

}   // namespace

const char* whereName(Where w)
{
    switch (w)
    {
    case Where::Garden:     return "GARDEN";
    case Where::Playground: return "PLAYGROUND";
    case Where::Station:    return "STATION";
    default:                return "HOSPITAL";
    }
}

namespace
{
const float HX0 = -0.98f, HX1 = 0.98f, HY0 = 0.845f, HY1 = 0.968f;   // whole panel
const float TOT_X1 = -0.52f;                                        // total capsule: HX0 .. TOT_X1
const float SC_X0 = -0.50f, SC_X1 = -0.10f;                         // scene capsule

Color sceneColor(Where w)
{
    switch (w)
    {
    case Where::Garden:     return rgb(0.30f, 0.70f, 0.36f);
    case Where::Playground: return rgb(0.95f, 0.55f, 0.20f);
    case Where::Station:    return rgb(0.28f, 0.52f, 0.90f);
    default:                return rgb(0.88f, 0.28f, 0.30f);
    }
}

void sceneIcon(Where w, float x, float y, float r)
{
    glColor4f(1, 1, 1, 1);
    switch (w)
    {
    case Where::Garden:                                        // flower
        for (int k = 0; k < 5; k++) { const float a = TAU * k / 5.0f; circle(x + std::cos(a) * r * 0.45f, y + std::sin(a) * r * 0.45f, r * 0.32f, 10); }
        glColor4f(1.0f, 0.85f, 0.25f, 1.0f); circle(x, y, r * 0.28f, 10); break;
    case Where::Playground:                                    // ball
        circle(x, y, r * 0.75f, 16); glColor4f(0.95f, 0.55f, 0.20f, 1.0f); rect(x - r * 0.75f, y - r * 0.08f, x + r * 0.75f, y + r * 0.08f); break;
    case Where::Station:                                       // train front
        rect(x - r * 0.55f, y - r * 0.55f, x + r * 0.55f, y + r * 0.60f); glColor4f(0.28f, 0.52f, 0.90f, 1.0f); rect(x - r * 0.40f, y + r * 0.05f, x + r * 0.40f, y + r * 0.45f);
        circle(x - r * 0.30f, y - r * 0.30f, r * 0.12f, 8); circle(x + r * 0.30f, y - r * 0.30f, r * 0.12f, 8); break;
    default:                                                   // red cross
        rect(x - r * 0.22f, y - r * 0.70f, x + r * 0.22f, y + r * 0.70f); rect(x - r * 0.70f, y - r * 0.22f, x + r * 0.70f, y + r * 0.22f); break;
    }
}
}   // namespace


void drawHud(Where w, const char* title, const char* stat)
{
    const bool old = gfx::useTint;
    gfx::useTint = false;
    const float x0 = HX0, x1 = HX1, y0 = HY0, y1 = HY1, ym = (y0 + y1) * 0.5f;
    const float bump = 1.0f + 0.06f * pulse() * pulse();

    roundRect(x0 + 0.008f, y0 - 0.012f, x1 + 0.008f, y1 - 0.012f, 0.045f, rgb(0.0f, 0.0f, 0.0f, 0.25f));
    roundRect(x0, y0, x1, y1, 0.045f, rgb(0.07f, 0.10f, 0.20f, 0.80f));

    // ---- TOTAL score (every scene together, kept everywhere) ----
    roundRect(x0, y0, TOT_X1, y1, 0.045f, rgb(0.98f, 0.80f, 0.28f, 0.96f));
    glColor4f(1.0f, 1.0f, 1.0f, 0.18f); rect(x0 + 0.045f, y1 - 0.030f, TOT_X1 - 0.045f, y1 - 0.012f);
    star(x0 + 0.072f, ym, 0.040f * bump, rgb(1.0f, 1.0f, 1.0f));
    star(x0 + 0.072f, ym, 0.026f * bump, rgb(0.95f, 0.55f, 0.10f));
    char sc[24];
    std::snprintf(sc, sizeof(sc), "%d", (int)std::lround(shownScore()));
    font::draw(x0 + 0.128f, y0 + 0.074f, 0.019f, rgb(0.45f, 0.24f, 0.04f), "TOTAL", 0, false);
    font::draw(x0 + 0.128f, y0 + 0.018f, 0.050f * bump, rgb(0.25f, 0.12f, 0.02f), sc, 0, false);

    // ---- SCENE score (only what was earned here) ----
    const Color cc = sceneColor(w);
    roundRect(SC_X0, y0 + 0.008f, SC_X1, y1 - 0.008f, 0.040f, withAlpha(shade(cc, 0.55f), 0.95f));
    roundRect(SC_X0 + 0.006f, y0 + 0.014f, SC_X1 - 0.006f, y1 - 0.014f, 0.036f, withAlpha(cc, 0.95f));
    glColor4f(0, 0, 0, 0.18f); circle(SC_X0 + 0.058f, ym, 0.040f, 20);
    sceneIcon(w, SC_X0 + 0.058f, ym, 0.030f);
    char ss[40];
    std::snprintf(ss, sizeof(ss), "%s SCORE", whereName(w));
    font::draw(SC_X0 + 0.112f, y0 + 0.074f, 0.017f, rgb(1.0f, 1.0f, 1.0f, 0.85f), ss, 0, false);
    std::snprintf(ss, sizeof(ss), "%d", (int)std::lround(shownSceneScore(w)));
    font::draw(SC_X0 + 0.112f, y0 + 0.020f, 0.046f * (lastDeltaWhere() == w ? bump : 1.0f), rgb(1, 1, 1), ss, 0, true);

    // the last change bounces next to the scene number (when it happened in this scene)
    if (lastDeltaAge() < 2.2f && lastDelta() != 0 && lastDeltaWhere() == w)
    {
        char d[16];
        std::snprintf(d, sizeof(d), "%+d", lastDelta());
        const float a = lastDeltaAge() < 1.6f ? 1.0f : 1.0f - (lastDeltaAge() - 1.6f) / 0.6f;
        const float hop = 0.02f * std::exp(-lastDeltaAge() * 4.0f) * std::fabs(std::sin(lastDeltaAge() * 14.0f));
        const Color c = lastDelta() > 0 ? rgb(0.80f, 1.0f, 0.70f, a) : rgb(1.0f, 0.82f, 0.78f, a);
        font::draw(SC_X1 - 0.020f, y0 + 0.032f + hop, 0.030f, c, d, 2, true);
    }

    // scene title + its one-line status
    font::draw(-0.06f, y0 + 0.068f, 0.030f, rgb(1.0f, 0.95f, 0.75f), title, 0, false);
    if (stat && *stat) font::draw(-0.06f, y0 + 0.022f, 0.021f, rgb(0.82f, 0.90f, 1.0f), stat, 0, false);

    // flying "+N" tokens (quadratic Bezier, ease-in-out timing)
    float tx, ty;
    scorePos(tx, ty);
    for (const Token& t : tokens())
    {
        const float u = smoothstep(std::min(1.0f, t.t)), v = 1.0f - u;
        const float px = v * v * t.x0 + 2 * v * u * t.cx + u * u * tx;
        const float py = v * v * t.y0 + 2 * v * u * t.cy + u * u * ty;
        const float a = t.t < 1.0f ? 1.0f : 1.0f - (t.t - 1.0f) / 0.25f;
        char d[16];
        std::snprintf(d, sizeof(d), "%+d", t.delta);
        const Color c = t.delta > 0 ? rgb(0.55f, 1.0f, 0.45f, a) : rgb(1.0f, 0.45f, 0.40f, a);
        font::draw(px, py, 0.040f * (1.0f - 0.35f * u), c, d, 1, true);
    }
    gfx::useTint = old;
}

void drawFeed(float x0, float y0, bool downward)
{
    const bool old = gfx::useTint;
    gfx::useTint = false;
    // the three newest events, newest at the bottom; each fades out after a while
    int shown = 0;
    const std::vector<Event>& ev = events();
    for (int i = (int)ev.size() - 1; i >= 0 && shown < 3; i--)
    {
        const Event& e = ev[i];
        if (e.age > 14.0f) break;
        const float a = e.age < 11.0f ? 1.0f : 1.0f - (e.age - 11.0f) / 3.0f;
        const float slide = 1.0f - smoothstep(e.age / 0.35f);
        const float y = y0 + (downward ? -1.0f : 1.0f) * shown * 0.062f, x = x0 - 0.25f * slide * (x0 < -1.0f ? -1.0f : 1.0f);
        const float w = font::width(e.text, 0.024f) + (e.delta ? 0.12f : 0.05f);
        roundRect(x, y - 0.018f, x + w, y + 0.040f, 0.022f, rgb(0.05f, 0.08f, 0.16f, 0.72f * a));
        float tx = x + 0.025f;
        if (e.delta != 0)
        {
            char d[12];
            std::snprintf(d, sizeof(d), "%+d", e.delta);
            const Color chip = e.delta > 0 ? rgb(0.25f, 0.72f, 0.32f, a) : rgb(0.88f, 0.25f, 0.22f, a);
            roundRect(x + 0.008f, y - 0.010f, x + 0.090f, y + 0.032f, 0.018f, chip);
            font::draw(x + 0.049f, y + 0.001f, 0.022f, rgb(1, 1, 1, a), d, 1, false);
            tx = x + 0.105f;
        }
        font::draw(tx, y + 0.001f, 0.024f, rgb(1.0f, 1.0f, 1.0f, a), e.text, 0, false);
        shown++;
    }
    gfx::useTint = old;
}

}   // namespace game
