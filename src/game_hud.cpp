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

void scorePos(float& x, float& y) { x = HUD_X - 0.42f; y = HUD_Y - 0.025f; }

void drawHud(Where w, const char* title, const char* stat)
{
    (void)w;
    const bool old = gfx::useTint;
    gfx::useTint = false;
    const float x0 = -0.80f, x1 = 0.80f, y0 = 0.845f, y1 = 0.968f;
    const float bump = 1.0f + 0.06f * pulse() * pulse();

    roundRect(x0 + 0.008f, y0 - 0.012f, x1 + 0.008f, y1 - 0.012f, 0.045f, rgb(0.0f, 0.0f, 0.0f, 0.25f));
    roundRect(x0, y0, x1, y1, 0.045f, rgb(0.07f, 0.10f, 0.20f, 0.80f));
    roundRect(x0, y0, -0.20f, y1, 0.045f, rgb(0.98f, 0.80f, 0.28f, 0.96f));          // score capsule
    glColor4f(1.0f, 1.0f, 1.0f, 0.18f);
    rect(x0 + 0.045f, y1 - 0.030f, -0.245f, y1 - 0.012f);

    // score: star + rolling number
    star(x0 + 0.075f, (y0 + y1) * 0.5f, 0.040f * bump, rgb(1.0f, 1.0f, 1.0f));
    star(x0 + 0.075f, (y0 + y1) * 0.5f, 0.026f * bump, rgb(0.95f, 0.55f, 0.10f));
    char sc[24];
    std::snprintf(sc, sizeof(sc), "%d", (int)std::lround(shownScore()));
    font::draw(x0 + 0.135f, y0 + 0.030f, 0.020f, rgb(0.45f, 0.24f, 0.04f), "SCORE", 0, false);
    font::draw(x0 + 0.245f, y0 + 0.030f, 0.055f * bump, rgb(0.25f, 0.12f, 0.02f), sc, 0, false);

    // the last change bounces next to the number for a moment
    if (lastDeltaAge() < 2.2f && lastDelta() != 0)
    {
        char d[16];
        std::snprintf(d, sizeof(d), "%+d", lastDelta());
        const float a = lastDeltaAge() < 1.6f ? 1.0f : 1.0f - (lastDeltaAge() - 1.6f) / 0.6f;
        const float hop = 0.02f * std::exp(-lastDeltaAge() * 4.0f) * std::fabs(std::sin(lastDeltaAge() * 14.0f));
        const Color c = lastDelta() > 0 ? rgb(0.10f, 0.62f, 0.20f, a) : rgb(0.85f, 0.15f, 0.12f, a);
        font::draw(-0.215f, y0 + 0.032f + hop, 0.032f, c, d, 2, false);
    }

    // scene title + its one-line status
    font::draw(-0.16f, y0 + 0.068f, 0.030f, rgb(1.0f, 0.95f, 0.75f), title, 0, false);
    if (stat && *stat) font::draw(-0.16f, y0 + 0.022f, 0.021f, rgb(0.82f, 0.90f, 1.0f), stat, 0, false);

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

void drawFeed()
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
        const float y = -0.955f + shown * 0.062f, x = -0.62f - 0.25f * slide;
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
