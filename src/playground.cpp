// playground.cpp - draws the playground (sky, equipment, children, ball) and its HUD

#include "playground.h"

#include "gfx.h"
#include "theme.h"
#include "ui.h"
#include "game.h"
#include "font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace gfx;

namespace playground
{
namespace
{

const Color SKIN[4]  = {rgb(0.96f, 0.76f, 0.60f), rgb(0.87f, 0.66f, 0.48f), rgb(0.66f, 0.46f, 0.32f), rgb(0.45f, 0.30f, 0.22f)};
const Color HAIR[4]  = {rgb(0.18f, 0.12f, 0.10f), rgb(0.45f, 0.28f, 0.14f), rgb(0.88f, 0.72f, 0.30f), rgb(0.70f, 0.30f, 0.12f)};
const Color SHIRT[6] = {rgb(0.92f, 0.32f, 0.30f), rgb(0.28f, 0.58f, 0.92f), rgb(0.35f, 0.75f, 0.42f), rgb(0.98f, 0.78f, 0.22f),
                        rgb(0.68f, 0.42f, 0.85f), rgb(0.98f, 0.55f, 0.70f)};
const Color PANTS[3] = {rgb(0.25f, 0.32f, 0.55f), rgb(0.50f, 0.32f, 0.20f), rgb(0.30f, 0.30f, 0.34f)};
const float KID_S    = 0.25f;                     // height of a standing child
float       waxClock = 0.0f;

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

// ---------------------------------------------------------------------------
// Children (local units: feet at 0, head top at ~1; drawn scaled by KID_S)
// ---------------------------------------------------------------------------
enum class Pose { Stand, Sit, Climb, Held, Kick };

void kidHead(const Kid& k)
{
    setColor(SKIN[k.skin % 4]); circle(0.0f, 0.82f, 0.13f, 18);
    setColor(HAIR[k.hair % 4]);
    ellipse(-0.01f, 0.90f, 0.135f, 0.07f, 14);
    rect(-0.135f, 0.80f, -0.09f, 0.90f);
    if (k.id % 3 == 0) { circle(-0.15f, 0.86f, 0.05f, 10); circle(0.11f, 0.95f, 0.04f, 8); }   // pigtails / curls
    setColor(rgb(0.12f, 0.08f, 0.08f)); circle(0.06f, 0.83f, 0.014f, 8);
    setColor(rgb(0.90f, 0.45f, 0.45f, 0.6f)); circle(0.075f, 0.78f, 0.022f, 8);                // cheek
    // mouth shows the mood: smile, flat line or frown (a little parabola)
    const float m = k.mood;
    const float curve = m > 0.55f ? -0.018f : m > 0.3f ? 0.0f : 0.016f;
    setColor(rgb(0.45f, 0.15f, 0.12f));
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= 6; i++)
    {
        const float u = i / 6.0f, x = 0.035f + 0.07f * u, y = 0.765f + curve * (1.0f - 4.0f * (u - 0.5f) * (u - 0.5f));
        glVertex2f(x, y - 0.006f); glVertex2f(x, y + 0.006f);
    }
    glEnd();
    if (k.st == KState::Hurt || (m < 0.2f && k.st != KState::Away))     // tears
    {
        setColor(rgb(0.45f, 0.75f, 1.0f, 0.9f));
        const float f = fract(k.t * 1.5f);
        ellipse(0.07f, 0.80f - 0.12f * f, 0.014f, 0.022f, 8);
    }
    if (k.bandage > 0.0f || k.st == KState::Hurt || k.st == KState::ToGate)   // plaster on the forehead
    {
        setColor(rgb(0.98f, 0.92f, 0.86f));
        glPushMatrix(); glTranslatef(-0.01f, 0.90f, 0); glRotatef(-20, 0, 0, 1);
        rect(-0.06f, -0.018f, 0.06f, 0.018f);
        setColor(rgb(0.85f, 0.30f, 0.30f)); rect(-0.012f, -0.012f, 0.012f, 0.012f);
        glPopMatrix();
    }
}

void kid(float x, float y, float s, const Kid& k, Pose pose, float facing, float rotDeg = 0.0f)
{
    const Color shirt = SHIRT[k.shirt % 6], pants = PANTS[k.pants % 3], skin = SKIN[k.skin % 4];
    const bool  moving = pose == Pose::Stand && (k.st == KState::Wander || k.st == KState::ToQueue || k.st == KState::ToRide ||
                                                 k.st == KState::Playing || (k.st == KState::Queued && false));
    const float step = (moving || pose == Pose::Climb) ? std::sin(k.phase) : 0.0f;

    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glRotatef(rotDeg, 0.0f, 0.0f, 1.0f);
    glScalef(s * facing, s, 1.0f);

    if (pose == Pose::Sit)
    {
        // hips at the origin, thighs forward, shins down
        setColor(shade(pants, 0.85f)); rect(-0.06f, -0.05f, 0.30f, 0.06f);
        glPushMatrix(); glTranslatef(0.28f, 0.0f, 0.0f); glRotatef(-70.0f + 18.0f * std::sin(k.phase * 0.6f), 0, 0, 1);
        setColor(pants); rect(-0.05f, -0.30f, 0.05f, 0.03f);
        setColor(rgb(0.95f, 0.95f, 0.95f)); rect(-0.06f, -0.36f, 0.10f, -0.29f);
        glPopMatrix();
        setColor(shirt); quad(-0.12f, 0.0f, 0.12f, 0.0f, 0.10f, 0.34f, -0.10f, 0.34f);
        glPushMatrix(); glTranslatef(0.0f, -0.40f, 0.0f); kidHead(k); glPopMatrix();
        glPushMatrix(); glTranslatef(0.02f, 0.30f, 0.0f); glRotatef(150.0f, 0, 0, 1);        // arms up holding the chains / handle
        setColor(shirt); rect(-0.035f, -0.26f, 0.035f, 0.0f); setColor(skin); circle(0.0f, -0.27f, 0.04f, 8);
        glPopMatrix();
        glPopMatrix();
        return;
    }

    setColor(rgb(0, 0, 0, 0.18f)); ellipse(0.0f, 0.0f, 0.22f, 0.035f, 14);

    for (int leg = 0; leg < 2; leg++)
    {
        glPushMatrix();
        glTranslatef(0.0f, 0.38f, 0.0f);
        float ang = (leg ? -1.0f : 1.0f) * 28.0f * step;
        if (pose == Pose::Kick && leg == 0) ang = 55.0f;
        if (pose == Pose::Held) ang = (leg ? -1.0f : 1.0f) * 15.0f * std::sin(k.phase * 2.0f);
        glRotatef(ang, 0.0f, 0.0f, 1.0f);
        setColor(leg ? shade(pants, 0.85f) : pants); rect(-0.05f, -0.34f, 0.05f, 0.0f);
        setColor(leg ? shade(skin, 0.9f) : skin); rect(-0.04f, -0.36f, 0.04f, -0.32f);
        setColor(rgb(0.95f, 0.95f, 0.95f)); rect(-0.055f, -0.40f, 0.10f, -0.35f);           // sneakers
        setColor(SHIRT[(k.shirt + 2) % 6]); rect(-0.055f, -0.40f, 0.10f, -0.385f);
        glPopMatrix();
    }
    setColor(shirt); quad(-0.13f, 0.36f, 0.13f, 0.36f, 0.11f, 0.68f, -0.11f, 0.68f);
    setColor(shade(shirt, 0.8f)); rect(-0.13f, 0.36f, 0.13f, 0.40f);
    for (int arm = 0; arm < 2; arm++)
    {
        glPushMatrix();
        glTranslatef(0.0f, 0.64f, 0.0f);
        float ang = (arm ? 1.0f : -1.0f) * 30.0f * step;
        if (pose == Pose::Climb || pose == Pose::Held) ang = 160.0f + (arm ? 15.0f : -15.0f) * step;
        if (pose == Pose::Kick) ang = arm ? 60.0f : -50.0f;
        glRotatef(ang, 0.0f, 0.0f, 1.0f);
        setColor(arm ? shirt : shade(shirt, 0.85f)); rect(-0.035f, -0.24f, 0.035f, 0.0f);
        setColor(skin); circle(0.0f, -0.26f, 0.04f, 8);
        glPopMatrix();
    }
    kidHead(k);
    glPopMatrix();
}

void drawStandingKid(const Kid& k)
{
    if (k.st == KState::Away) return;
    if (k.st == KState::Falling)                                   // tumbling through the air
    {
        kid(k.x, k.y, KID_S, k, Pose::Held, k.vx >= 0 ? 1.0f : -1.0f, k.spin);
        setColor(rgb(0, 0, 0, 0.18f)); ellipse(k.x, k.landY, 0.05f, 0.012f, 14);   // shadow where it will land
        return;
    }
    if (k.st == KState::Hurt)                                      // sitting on the ground, crying
    {
        kid(k.x, k.y + 0.02f, KID_S, k, Pose::Sit, k.facing);
        if (k.serious)
        {
            const bool o = gfx::useTint; gfx::useTint = false;
            const float b = 0.01f * std::sin(k.t * 6.0f);
            setColor(rgb(1, 1, 1)); circle(k.x, k.y + 0.31f + b, 0.032f, 18);
            setColor(rgb(0.90f, 0.15f, 0.15f));
            rect(k.x - 0.007f, k.y + 0.293f + b, k.x + 0.007f, k.y + 0.327f + b);
            rect(k.x - 0.017f, k.y + 0.303f + b, k.x + 0.017f, k.y + 0.317f + b);
            gfx::useTint = o;
        }
        return;
    }
    Pose p = Pose::Stand;
    if (k.st == KState::Dragged) p = Pose::Held;
    if (k.st == KState::Playing && k.flash > 0.0f) p = Pose::Kick;
    float y = k.y;
    if (k.st == KState::Dragged) y += 0.02f;
    kid(k.x, y, KID_S, k, p, k.facing);
    if (k.st == KState::Dragged) { setColor(rgb(1, 1, 1, 0.7f)); ring(k.x, k.y - 0.01f, 0.08f, 0.025f, 20); }
}

// ---------------------------------------------------------------------------
// Backdrop
// ---------------------------------------------------------------------------
void sky(float t)
{
    const float sunX = 1.30f, sunY = 0.58f;
    Stop stops[] = {{-0.2f, rgb(0.88f, 0.95f, 0.99f)}, {0.3f, rgb(0.70f, 0.87f, 0.99f)}, {1.2f, rgb(0.30f, 0.62f, 0.95f)}};
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
    const float u = fract(t / 90.0f);
    drawCloud(-1.9f + 4.0f * u, 0.62f, 0.22f, 0.1f, 0.85f);
    drawCloud(-0.6f + 4.0f * fract(u + 0.45f) - 2.0f, 0.70f, 0.16f, 0.2f, 0.75f);
}

void drawLamp(float x, float y)
{
    setColor(rgb(0.25f, 0.30f, 0.28f)); rect(x - 0.007f, y, x + 0.007f, y + 0.42f);
    setColor(rgb(0.20f, 0.24f, 0.22f)); rect(x - 0.03f, y + 0.42f, x + 0.03f, y + 0.435f);
    tri(x - 0.035f, y + 0.475f, x + 0.035f, y + 0.475f, x, y + 0.50f);
    setColor(night() ? rgb(1.0f, 0.93f, 0.65f) : rgb(0.86f, 0.90f, 0.92f)); rect(x - 0.022f, y + 0.435f, x + 0.022f, y + 0.475f);
    if (night()) additiveGlow(x, y + 0.455f, 0.30f, rgb(1.0f, 0.88f, 0.50f, 0.40f));
}

void drawBench(float x, float y)
{
    setColor(rgb(0.30f, 0.30f, 0.32f)); rect(x - 0.15f, y, x - 0.135f, y + 0.07f); rect(x + 0.135f, y, x + 0.15f, y + 0.07f);
    setColor(rgb(0.62f, 0.40f, 0.22f)); rect(x - 0.17f, y + 0.06f, x + 0.17f, y + 0.08f);
    setColor(rgb(0.55f, 0.35f, 0.18f)); rect(x - 0.17f, y + 0.10f, x + 0.17f, y + 0.12f); rect(x - 0.17f, y + 0.14f, x + 0.17f, y + 0.16f);
    setColor(rgb(0.30f, 0.30f, 0.32f)); rect(x - 0.15f, y + 0.08f, x - 0.14f, y + 0.17f); rect(x + 0.14f, y + 0.08f, x + 0.15f, y + 0.17f);
}

void drawBackdrop(float t)
{
    sky(t);
    static const Ridge farHills  = makeRolling(-2.4f, 2.4f, 0.04f, 0.06f, 0.16f, 1.3f, 1.7f);
    static const Ridge nearHills = makeRolling(-2.4f, 2.4f, 0.04f, -0.02f, 0.10f, 4.1f, 2.4f);
    drawRidge(farHills, -0.3f, rgb(0.52f, 0.74f, 0.60f), rgb(0.64f, 0.82f, 0.64f));
    // tiny houses on the far hill
    for (int i = 0; i < 6; i++)
    {
        const float x = -1.5f + i * 0.62f + 0.12f * hash1(i * 5.1f), y = ridgeAt(farHills, x) - 0.02f;
        setColor(rgb(0.95f, 0.90f, 0.82f)); rect(x - 0.04f, y, x + 0.04f, y + 0.045f);
        setColor(i % 2 ? rgb(0.78f, 0.30f, 0.25f) : rgb(0.35f, 0.45f, 0.70f)); tri(x - 0.05f, y + 0.045f, x + 0.05f, y + 0.045f, x, y + 0.08f);
        if (night()) { const bool o = gfx::useTint; gfx::useTint = false; setColor(rgb(1.0f, 0.85f, 0.45f)); rect(x - 0.012f, y + 0.012f, x + 0.012f, y + 0.032f); gfx::useTint = o; }
    }
    drawRidge(nearHills, -0.3f, rgb(0.44f, 0.70f, 0.46f), rgb(0.56f, 0.80f, 0.52f));
    for (int i = 0; i < 14; i++)
    {
        const float x = -1.95f + i * 0.30f + 0.08f * hash1(i * 3.7f);
        drawRoundTree(x, 0.04f, 0.22f + 0.08f * hash1(i * 9.1f), wave(fract(t / 60.0f), 6.0f, hash1(i * 1.3f)),
                      rgb(0.18f, 0.48f, 0.26f), rgb(0.34f, 0.66f, 0.34f), i % 4 == 1);
    }
    // grass
    rectV(-WORLD_W - 0.1f, -1.0f, WORLD_W + 0.1f, 0.06f, rgb(0.36f, 0.66f, 0.32f), rgb(0.52f, 0.80f, 0.42f));
    // hedge + picket fence at the back
    for (int i = 0; i < 24; i++) drawBush(-1.85f + i * 0.16f, 0.0f, 0.045f, rgb(0.16f, 0.44f, 0.22f), rgb(0.30f, 0.60f, 0.30f));
    drawFence(-WORLD_W - 0.05f, WORLD_W + 0.05f, -0.03f, 0.11f, rgb(0.97f, 0.96f, 0.92f));

    // soft-fall play surfaces under the equipment
    setColor(rgb(0.86f, 0.74f, 0.52f)); ellipse(-1.16f, GROUND_Y - 0.01f, 0.52f, 0.07f, 40);
    setColor(rgb(0.78f, 0.46f, 0.36f)); ellipse(SEESAW_X, GROUND_Y - 0.01f, 0.44f, 0.06f, 40);
    setColor(rgb(0.50f, 0.62f, 0.86f)); ellipse(0.66f, GROUND_Y - 0.01f, 0.48f, 0.065f, 40);
    // queue paint marks
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < 6; i++)
        {
            const float head = r == 0 ? -1.44f : r == 1 ? -0.60f : 0.20f;
            setColor(i == 0 ? rgb(1.0f, 0.85f, 0.30f, 0.65f) : rgb(1.0f, 1.0f, 1.0f, 0.30f));
            ellipse(head + i * 0.085f, QUEUE_Y - (i % 2) * 0.012f - 0.004f, 0.028f, 0.009f, 12);
        }
    // ball pitch (front strip)
    setColor(rgb(0.42f, 0.74f, 0.38f)); rect(-WORLD_W - 0.1f, BALL_Y - 0.07f, WORLD_W + 0.1f, BALL_Y + 0.05f);
    setColor(rgb(1.0f, 1.0f, 1.0f, 0.55f));
    rect(-WORLD_W - 0.1f, BALL_Y + 0.045f, WORLD_W + 0.1f, BALL_Y + 0.052f);
    rect(-0.004f, BALL_Y - 0.07f, 0.004f, BALL_Y + 0.05f);
    for (int s = -1; s <= 1; s += 2)                                   // little goals at both ends
    {
        const float gx = s * (WALL_X + 0.02f);
        setColor(rgb(0.95f, 0.95f, 0.95f)); rect(gx - 0.01f, BALL_Y, gx + 0.01f, BALL_Y + 0.14f);
        rect(gx - s * 0.10f, BALL_Y + 0.13f, gx, BALL_Y + 0.145f);
        setColor(rgb(1, 1, 1, 0.35f)); for (int k = 0; k < 4; k++) rect(gx - s * 0.10f * (k / 3.0f) - 0.002f, BALL_Y, gx - s * 0.10f * (k / 3.0f) + 0.002f, BALL_Y + 0.13f);
    }

    // flowers along the fence
    for (int i = 0; i < 20; i++)
    {
        const float x = -1.70f + i * 0.18f + 0.05f * hash1(i * 2.2f), y = -0.08f - 0.03f * hash1(i * 4.4f);
        setColor(rgb(0.25f, 0.55f, 0.28f)); rect(x - 0.002f, y, x + 0.002f, y + 0.04f);
        setColor(i % 3 == 0 ? rgb(1.0f, 0.82f, 0.25f) : i % 3 == 1 ? rgb(0.95f, 0.45f, 0.62f) : rgb(0.70f, 0.55f, 0.95f));
        circle(x, y + 0.04f, 0.012f, 8);
    }
    drawBench(1.38f, GROUND_Y - 0.02f);
    drawLamp(-1.68f, GROUND_Y - 0.04f);
    drawLamp(0.12f, GROUND_Y + 0.02f);
    drawLamp(1.14f, GROUND_Y - 0.04f);

    // welcome sign
    setColor(rgb(0.45f, 0.30f, 0.16f)); rect(1.27f, GROUND_Y + 0.02f, 1.285f, 0.22f); rect(1.555f, GROUND_Y + 0.02f, 1.57f, 0.22f);
    setColor(rgb(0.95f, 0.78f, 0.30f)); rect(1.24f, 0.12f, 1.60f, 0.25f);
    setColor(rgb(0.80f, 0.55f, 0.18f)); rect(1.24f, 0.12f, 1.60f, 0.13f);
    const float px = fitPx(playgroundcfg::NAME, 0.32f, 0.0062f);
    ui::drawText(1.42f, 0.17f, px, rgb(0.40f, 0.18f, 0.08f), playgroundcfg::NAME, true);
}

// ---------------------------------------------------------------------------
// Equipment
// ---------------------------------------------------------------------------
void drawSwingSet(float t)
{
    const Color frame = rgb(0.85f, 0.25f, 0.25f), frameD = rgb(0.65f, 0.16f, 0.18f);
    const float top = SWING_PIVOT_Y + 0.03f;
    for (int e = 0; e < 2; e++)
    {
        const float x = e ? SWING_FRAME_X1 : SWING_FRAME_X0;
        setColor(frameD); thickLine(x + 0.07f, GROUND_Y + 0.02f, x, top, 0.024f);
        setColor(frame);  thickLine(x - 0.07f, GROUND_Y, x, top, 0.026f);
        setColor(rgb(0.40f, 0.40f, 0.42f)); rect(x - 0.09f, GROUND_Y - 0.012f, x - 0.05f, GROUND_Y + 0.005f);
    }
    setColor(rgb(0.25f, 0.45f, 0.80f)); rect(SWING_FRAME_X0 - 0.02f, top - 0.012f, SWING_FRAME_X1 + 0.02f, top + 0.016f);
    for (int i = 0; i < SWINGS; i++)
    {
        const Swing& s = swing(i);
        float sx, sy; swingSeatPos(i, sx, sy);
        const float c = std::cos(s.theta), sn = std::sin(s.theta);
        setColor(rgb(0.55f, 0.55f, 0.58f));
        for (int side = -1; side <= 1; side += 2)                  // two chains
        {
            const float ox = side * 0.035f;
            const float x0 = SWING_X[i] + ox, y0 = SWING_PIVOT_Y;
            const float x1 = sx + ox * c, y1 = sy + ox * sn;
            glLineWidth(2.0f); glBegin(GL_LINES); glVertex2f(x0, y0); glVertex2f(x1, y1); glEnd(); glLineWidth(1.0f);
        }
        setColor(rgb(0.45f, 0.45f, 0.48f)); circle(SWING_X[i], SWING_PIVOT_Y, 0.010f, 8);
        // seat (rotates with the chains)
        glPushMatrix(); glTranslatef(sx, sy, 0.0f); glRotatef(s.theta * 57.2958f, 0, 0, 1);
        setColor(rgb(0.20f, 0.20f, 0.22f)); rect(-0.05f, -0.012f, 0.05f, 0.006f);
        setColor(i ? rgb(0.98f, 0.75f, 0.20f) : rgb(0.30f, 0.70f, 0.40f)); rect(-0.048f, -0.008f, 0.048f, 0.010f);
        glPopMatrix();
        if (s.rider >= 0 && swingAmplitude(i) > playgroundcfg::SWING_DANGER_AMP)   // pulsing red warning
        {
            const bool o = gfx::useTint; gfx::useTint = false;
            const float p = 0.5f + 0.5f * std::sin(t * 12.0f);
            glLineWidth(3.0f);
            setColor(rgb(1.0f, 0.2f, 0.15f, 0.5f + 0.4f * p));
            ring(sx, sy + 0.10f, 0.15f + 0.02f * p, 0.15f + 0.02f * p, 32);
            glLineWidth(1.0f);
            gfx::useTint = o;
        }
        if (s.rider >= 0)
            if (const Kid* k = findKid(s.rider))
            {
                const float face = s.omega >= 0 ? 1.0f : -1.0f;
                kid(sx, sy + 0.012f, KID_S, *k, Pose::Sit, face, s.theta * 57.2958f);
            }
        if (s.pushT > 0.0f)                                         // a push "whoosh"
        {
            const bool o = gfx::useTint; gfx::useTint = false;
            setColor(rgb(1.0f, 1.0f, 1.0f, s.pushT * 1.8f));
            for (int k = 0; k < 3; k++) thickLine(sx - 0.10f - k * 0.03f, sy + 0.03f + k * 0.03f, sx - 0.06f - k * 0.03f, sy + 0.03f + k * 0.03f, 0.006f);
            gfx::useTint = o;
        }
    }
}

void drawSeesaw()
{
    const Seesaw& s = seesaw();
    // base (A-shaped pivot stand)
    setColor(rgb(0.30f, 0.32f, 0.36f)); tri(SEESAW_X - 0.07f, GROUND_Y - 0.005f, SEESAW_X + 0.07f, GROUND_Y - 0.005f, SEESAW_X, SEESAW_Y);
    setColor(rgb(0.45f, 0.48f, 0.52f)); tri(SEESAW_X - 0.05f, GROUND_Y - 0.005f, SEESAW_X + 0.02f, GROUND_Y - 0.005f, SEESAW_X, SEESAW_Y - 0.01f);
    // springs / tyres where the ends touch the ground
    for (int sd = -1; sd <= 1; sd += 2) { setColor(rgb(0.18f, 0.18f, 0.20f)); ellipse(SEESAW_X + sd * (playgroundcfg::SEESAW_HALF - 0.02f), GROUND_Y + 0.005f, 0.04f, 0.015f, 12); }

    glPushMatrix(); glTranslatef(SEESAW_X, SEESAW_Y, 0.0f); glRotatef(s.phi * 57.2958f, 0, 0, 1);
    // NOTE: +phi = left end down, i.e. a positive (counter-clockwise) rotation of a beam whose
    //       left end is at -x
    setColor(rgb(0.95f, 0.60f, 0.20f)); rect(-playgroundcfg::SEESAW_HALF, -0.012f, playgroundcfg::SEESAW_HALF, 0.016f);
    setColor(rgb(0.80f, 0.45f, 0.12f)); rect(-playgroundcfg::SEESAW_HALF, -0.012f, playgroundcfg::SEESAW_HALF, -0.004f);
    for (int sd = -1; sd <= 1; sd += 2)
    {
        const float arm = s.arm[sd < 0 ? 0 : 1];
        setColor(sd < 0 ? rgb(0.25f, 0.55f, 0.90f) : rgb(0.90f, 0.30f, 0.45f)); rect(sd * arm - 0.04f, 0.016f, sd * arm + 0.04f, 0.030f);
        setColor(rgb(0.35f, 0.35f, 0.38f));                         // handle in front of each seat
        thickLine(sd * (arm - 0.07f), 0.016f, sd * (arm - 0.07f), 0.08f, 0.012f);
        thickLine(sd * (arm - 0.07f) - 0.02f, 0.08f, sd * (arm - 0.07f) + 0.02f, 0.08f, 0.012f);
    }
    glPopMatrix();
    setColor(rgb(0.25f, 0.25f, 0.28f)); circle(SEESAW_X, SEESAW_Y, 0.016f, 12);
    if (!s.balance) { setColor(rgb(0.9f, 0.2f, 0.2f)); circle(SEESAW_X, SEESAW_Y, 0.008f, 8); }

    for (int side = 0; side < 2; side++)
    {
        if (s.seat[side] < 0 || !s.mounted[side]) continue;
        const Kid* k = findKid(s.seat[side]);
        if (!k) continue;
        float x, y; seesawSeatPos(side, x, y);
        kid(x, y + 0.012f, KID_S, *k, Pose::Sit, side == 0 ? 1.0f : -1.0f, s.phi * 57.2958f);
    }
}

void drawSlide()
{
    const Color frame = rgb(0.25f, 0.45f, 0.82f), chute = rgb(0.98f, 0.78f, 0.20f), chuteD = rgb(0.85f, 0.60f, 0.10f);
    // tower posts and platform
    setColor(frame);
    rect(LADDER_X - 0.04f, GROUND_Y, LADDER_X - 0.025f, SLIDE_TOP_Y + 0.12f);
    rect(SLIDE_TOP_X + 0.005f, GROUND_Y, SLIDE_TOP_X + 0.02f, SLIDE_TOP_Y + 0.12f);
    setColor(rgb(0.90f, 0.30f, 0.30f)); tri(LADDER_X - 0.07f, SLIDE_TOP_Y + 0.12f, SLIDE_TOP_X + 0.05f, SLIDE_TOP_Y + 0.12f, (LADDER_X + SLIDE_TOP_X) / 2 - 0.01f, SLIDE_TOP_Y + 0.22f);
    setColor(rgb(0.55f, 0.38f, 0.22f)); rect(LADDER_X - 0.045f, SLIDE_TOP_Y - 0.015f, SLIDE_TOP_X + 0.025f, SLIDE_TOP_Y);
    // ladder
    setColor(rgb(0.30f, 0.32f, 0.36f));
    thickLine(LADDER_X - 0.035f, GROUND_Y, LADDER_X - 0.035f, SLIDE_TOP_Y, 0.012f);
    thickLine(LADDER_X + 0.020f, GROUND_Y, LADDER_X + 0.020f, SLIDE_TOP_Y, 0.012f);
    for (int r = 1; r < 9; r++) { const float y = GROUND_Y + r * (SLIDE_TOP_Y - GROUND_Y) / 9.0f; rect(LADDER_X - 0.035f, y - 0.004f, LADDER_X + 0.02f, y + 0.004f); }
    // chute (with side rail) + lip
    const float a = slideAngle(), nx = -std::sin(a) * 0.0f, w = 0.030f;
    setColor(chuteD);
    quad(SLIDE_TOP_X, SLIDE_TOP_Y - w, SLIDE_BOT_X, SLIDE_BOT_Y - w, SLIDE_BOT_X, SLIDE_BOT_Y, SLIDE_TOP_X, SLIDE_TOP_Y);
    rect(SLIDE_BOT_X, SLIDE_BOT_Y - w, SLIDE_LIP_X, SLIDE_BOT_Y);
    setColor(chute);
    quad(SLIDE_TOP_X, SLIDE_TOP_Y - 0.010f, SLIDE_BOT_X, SLIDE_BOT_Y - 0.010f, SLIDE_BOT_X, SLIDE_BOT_Y + 0.012f, SLIDE_TOP_X, SLIDE_TOP_Y + 0.012f);
    rect(SLIDE_BOT_X, SLIDE_BOT_Y - 0.010f, SLIDE_LIP_X, SLIDE_BOT_Y + 0.012f);
    setColor(frame);                                                 // support legs
    rect(0.66f, GROUND_Y, 0.675f, 0.66f * 0.0f + (SLIDE_TOP_Y + (SLIDE_BOT_Y - SLIDE_TOP_Y) * (0.66f - SLIDE_TOP_X) / (SLIDE_BOT_X - SLIDE_TOP_X)) - w);
    rect(SLIDE_LIP_X - 0.02f, GROUND_Y, SLIDE_LIP_X - 0.005f, SLIDE_BOT_Y - w);
    (void)nx;

    for (const Kid& k : kids())
    {
        if (k.ride != Ride::Slide || k.st != KState::Riding) continue;
        if (k.slideStage == 0) kid(k.x, k.y, KID_S, k, Pose::Climb, 1.0f);
        else if (k.slideStage == 1) kid(SLIDE_TOP_X - 0.02f, SLIDE_TOP_Y + 0.02f, KID_S, k, Pose::Sit, 1.0f);
        else if (k.slideStage == 2) kid(k.x, k.y + 0.02f, KID_S, k, Pose::Sit, 1.0f, -a * 57.2958f);
        else kid(k.x, k.y + 0.02f, KID_S, k, Pose::Sit, 1.0f);
    }
    if (slideWax() > 0.0f)                                           // waxed: glints racing down the chute
    {
        const bool o = gfx::useTint; gfx::useTint = false;
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        const float fade = std::min(1.0f, slideWax() / 2.0f);
        for (int k = 0; k < 5; k++)
        {
            const float u = fract(waxClock * 0.9f + k / 5.0f);
            const float x = lerpf(SLIDE_TOP_X, SLIDE_BOT_X, u), y = lerpf(SLIDE_TOP_Y, SLIDE_BOT_Y, u) + 0.004f;
            glow(x, y, 0.035f, rgb(1.0f, 1.0f, 0.85f, 0.8f * fade), 14);
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gfx::useTint = o;
    }
    // chute side rail drawn over the sliding child
    setColor(shade(chute, 0.9f)); thickLine(SLIDE_TOP_X, SLIDE_TOP_Y + 0.045f, SLIDE_BOT_X - 0.02f, SLIDE_BOT_Y + 0.04f, 0.008f);
}

void drawBall()
{
    const Ball& b = ball();
    const float k = 1.0f / (1.0f + b.h * 4.0f);
    setColor(rgb(0, 0, 0, 0.25f * k)); ellipse(b.x, BALL_Y - 0.005f, BALL_R * 1.2f * (0.6f + 0.4f * k), 0.01f, 14);
    const float y = BALL_Y + BALL_R + b.h;
    setColor(rgb(0.97f, 0.97f, 0.97f)); circle(b.x, y, BALL_R, 20);
    setColor(rgb(0.15f, 0.15f, 0.18f));
    for (int i = 0; i < 3; i++)
    {
        const float a = b.spin + i * TAU / 3.0f;
        circle(b.x + std::cos(a) * BALL_R * 0.55f, y + std::sin(a) * BALL_R * 0.55f, BALL_R * 0.25f, 8);
    }
    circle(b.x, y, BALL_R * 0.22f, 8);
    setColor(rgb(1, 1, 1, 0.5f)); circle(b.x - BALL_R * 0.35f, y + BALL_R * 0.4f, BALL_R * 0.2f, 8);
}

void drawMat()
{
    const Mat& m = mat();
    const float x0 = m.x - MAT_HW, x1 = m.x + MAT_HW, y0 = m.y - MAT_HH, y1 = m.y + MAT_HH;
    setColor(rgb(0, 0, 0, 0.2f)); ellipse(m.x, y0 - 0.005f, MAT_HW * 1.05f, 0.012f, 20);
    setColor(rgb(0.20f, 0.42f, 0.85f)); quad(x0, y0, x1, y0, x1 - 0.02f, y1, x0 + 0.02f, y1);
    setColor(rgb(0.32f, 0.58f, 0.98f)); quad(x0 + 0.01f, y0 + 0.012f, x1 - 0.01f, y0 + 0.012f, x1 - 0.025f, y1 - 0.004f, x0 + 0.025f, y1 - 0.004f);
    setColor(rgb(1.0f, 0.85f, 0.20f));
    for (int k = 1; k < 4; k++) { const float x = lerpf(x0, x1, k / 4.0f); rect(x - 0.004f, y0 + 0.006f, x + 0.004f, y1 - 0.004f); }
    if (hoverMat() || matCovers(m.x, m.y))
    {
        const bool o = gfx::useTint; gfx::useTint = false;
        ui::drawText(m.x, y1 + 0.015f, 0.0034f, rgb(1, 1, 1, 0.9f), "SAFETY MAT", true);
        gfx::useTint = o;
    }
}

void drawGate()
{
    const float x = GATE_X - 0.02f, y = GATE_Y;
    setColor(rgb(0.55f, 0.36f, 0.20f)); rect(x - 0.01f, y, x + 0.01f, y + 0.30f); rect(x + 0.12f, y, x + 0.14f, y + 0.30f);
    setColor(rgb(0.70f, 0.48f, 0.26f)); rect(x - 0.01f, y + 0.27f, x + 0.14f, y + 0.30f);
    const bool o = gfx::useTint; gfx::useTint = false;
    ui::drawText(x + 0.065f, y + 0.315f, 0.0030f, rgb(1, 1, 1, 0.85f), "TO HOSPITAL", true);
    gfx::useTint = o;
}

// while you hold the ball: dotted parabola of the throw you are about to make
void drawThrowPreview()
{
    float vx, vh;
    if (!ball().held || !predictThrow(vx, vh)) return;
    const bool o = gfx::useTint; gfx::useTint = false;
    const Ball& b = ball();
    const float G = playgroundcfg::G;
    for (int i = 1; i < 26; i++)
    {
        const float t = i * 0.045f;
        const float x = b.x + vx * t, h = b.h + vh * t - 0.5f * G * t * t;
        if (h < -0.01f) break;
        setColor(rgb(1.0f, 1.0f, 1.0f, 0.75f * (1.0f - i / 26.0f)));
        circle(x, BALL_Y + BALL_R + h, 0.007f, 8);
    }
    gfx::useTint = o;
}

void drawFireflies(float t)
{
    if (!night()) return;
    const bool o = gfx::useTint; gfx::useTint = false;
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for (int i = 0; i < 18; i++)
    {
        const float x = -1.7f + 3.4f * hash1(i * 3.1f) + 0.08f * std::sin(t * 0.7f + i);
        const float y = -0.05f + 0.25f * hash1(i * 7.7f) + 0.05f * std::sin(t * 1.1f + i * 2.0f);
        const float a = 0.5f + 0.5f * std::sin(t * 3.0f + i * 1.7f);
        glow(x, y, 0.025f, rgb(0.85f, 1.0f, 0.45f, 0.8f * a), 10);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gfx::useTint = o;
}

void dropHighlight()
{
    if (!dragging() || !ui::mouse.valid) return;
    const Ride r = rideZoneAt(ui::mouse.x, ui::mouse.y);
    const bool o = gfx::useTint; gfx::useTint = false;
    float x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    switch (r)
    {
    case Ride::Swing:  x0 = SWING_FRAME_X0 - 0.06f; x1 = SWING_FRAME_X1 + 0.04f; y0 = -0.68f; y1 = 0.46f; break;
    case Ride::Seesaw: x0 = -0.70f; x1 = 0.10f; y0 = -0.68f; y1 = 0.05f; break;
    case Ride::Slide:  x0 = 0.16f; x1 = 1.05f; y0 = -0.68f; y1 = 0.32f; break;
    case Ride::Ball:   x0 = -WORLD_W; x1 = WORLD_W; y0 = -0.86f; y1 = -0.68f; break;
    default: break;
    }
    if (r != Ride::None) { setColor(rgb(0.3f, 1.0f, 0.5f, 0.18f)); rect(x0, y0, x1, y1); }
    gfx::useTint = o;
}

}   // namespace

// ---------------------------------------------------------------------------
// public
// ---------------------------------------------------------------------------
void draw(float t)
{
    drawBackdrop(t);
    waxClock = t;
    drawGate();
    drawMat();
    drawSwingSet(t);
    drawSeesaw();
    drawSlide();
    if (ball().goalFlash > 0.0f)                                     // goal celebration
    {
        const bool o = gfx::useTint; gfx::useTint = false;
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glow(ball().goalSide * (WALL_X - 0.02f), BALL_Y + 0.08f, 0.35f, rgb(1.0f, 0.95f, 0.4f, 0.5f * ball().goalFlash), 30);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gfx::useTint = o;
    }

    // children on the ground and the ball, back (high y) to front (low y)
    struct Item { float y; const Kid* k; };
    std::vector<Item> items;
    for (const Kid& k : kids())
        if (k.st != KState::Riding && k.st != KState::Dragged && k.st != KState::Falling && k.st != KState::Away) items.push_back({k.y, &k});
    items.push_back({BALL_Y - 0.001f, nullptr});
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.y > b.y; });
    for (const Item& it : items) { if (it.k) drawStandingKid(*it.k); else drawBall(); }

    drawThrowPreview();
    drawFireflies(t);
    dropHighlight();
    for (const Kid& k : kids()) if (k.st == KState::Dragged || k.st == KState::Falling) drawStandingKid(k);   // flying / held on top
}

void drawOverlay()
{
    const bool old = gfx::useTint;
    gfx::useTint = false;
    const Stats& s = stats();

    // ---- game HUD (score + the few numbers that matter) and the gameplay feed ----
    {
        char stat[96];
        int kidsHere = 0;
        for (const Kid& k : kids()) if (k.st != KState::Away) kidsHere++;
        std::snprintf(stat, sizeof(stat), "Happy kids %d/%d   Hurt %d   Goals %d%s", happyCount(), kidsHere, injuredCount(), s.goals,
                      slideWax() > 0.0f ? "   Slide waxed!" : "");
        game::drawHud(game::Where::Playground, playgroundcfg::NAME, stat);
        game::drawFeed();
    }

    // ---- floating pop-ups ----
    for (const Popup& p : popups())
    {
        const float k = p.age / 2.6f;
        Color c = uiColor(p.color); c.a = k < 0.7f ? 1.0f : 1.0f - (k - 0.7f) / 0.3f;
        const float px = 0.0060f, w = ui::textWidth(p.text, px) * 0.5f + 0.02f, yy = std::min(0.70f, p.y + 0.12f * k);
        const float cx = clampf(p.x, -WORLD_W + w + 0.02f, WORLD_W - w - 0.02f);
        setColor(rgb(0.02f, 0.06f, 0.04f, 0.75f * c.a)); rect(cx - w, yy - 0.012f, cx + w, yy + 0.055f);
        ui::drawText(cx, yy, px, c, p.text, true);
    }

    // ---- drag hint ----
    if (dragging() && ui::mouse.valid)
    {
        const Ride r = rideZoneAt(ui::mouse.x, ui::mouse.y);
        char b[64];
        if (r == Ride::None) std::snprintf(b, sizeof(b), "DROP: FREE PLAY HERE");
        else if (r == Ride::Ball) std::snprintf(b, sizeof(b), "DROP: JOIN THE BALL GAME (%d/%d)", ballPlayers(), playgroundcfg::BALL_MAX_PLAYERS);
        else std::snprintf(b, sizeof(b), "DROP: JOIN %s QUEUE (WAIT %.0fS)", rideName(r), expectedWait(r));
        const float px = 0.0062f, w = ui::textWidth(b, px) + 0.04f;
        const float x = clampf(ui::mouse.x + 0.06f, -WORLD_W + 0.02f, WORLD_W - w - 0.02f), y = clampf(ui::mouse.y + 0.06f, -0.80f, 0.70f);
        setColor(rgb(0.02f, 0.10f, 0.04f, 0.85f)); rect(x, y, x + w, y + 0.07f);
        ui::drawText(x + 0.02f, y + 0.02f, px, rgb(0.6f, 1.0f, 0.6f), b);
    }

    // ---- hover tooltip (what can I do here?) ----
    if (ui::mouse.valid && !dragging() && !ball().held)
    {
        char t1[80] = "", t2[80] = "", t3[80] = "";
        bool show = true;
        int side = -1;
        const int kidId = hoverKidId();
        if (kidId >= 0)
        {
            const Kid* k = findKid(kidId);
            std::snprintf(t1, sizeof(t1), "Kid #%d  -  %s", k->id, k->mood > 0.7f ? "very happy" : k->mood > 0.45f ? "happy" : k->mood > 0.2f ? "a bit sad" : "upset");
            std::snprintf(t2, sizeof(t2), "%s", stateName(*k));
            std::snprintf(t3, sizeof(t3), "Drag me to a ride or to the ball pitch");
        }
        else if (hoverBall())
        {
            std::snprintf(t1, sizeof(t1), "The ball");
            std::snprintf(t2, sizeof(t2), "Click to kick - or drag and let go to THROW it");
            std::snprintf(t3, sizeof(t3), "Score in a goal (+%d). Don't hit the children!", playgroundcfg::SCORE_GOAL);
        }
        else if (hoverMat())
        {
            std::snprintf(t1, sizeof(t1), "Safety mat");
            std::snprintf(t2, sizeof(t2), "Drag it under a swing or the end of the slide");
            std::snprintf(t3, sizeof(t3), "A child who lands on it is not hurt");
        }
        else if (hoverSwing() >= 0)
        {
            const int i = hoverSwing();
            const float deg = swingAmplitude(i) * 57.2958f;
            std::snprintf(t1, sizeof(t1), "Swing %d  -  %.0f degrees%s", i + 1, deg, swing(i).rider >= 0 ? "" : "  (empty)");
            std::snprintf(t2, sizeof(t2), "Click to push. Children love 25-60 degrees");
            std::snprintf(t3, sizeof(t3), "Above 75 degrees they let go and FALL!");
        }
        else if (hoverSlide())
        {
            std::snprintf(t1, sizeof(t1), "The slide%s", slideWax() > 0.0f ? "  -  WAXED" : "");
            std::snprintf(t2, sizeof(t2), "Click to wax it: children slide much faster");
            std::snprintf(t3, sizeof(t3), "Too fast and they fly off the end - put the mat there!");
        }
        else if (hoverSeesaw(side))
        {
            const Seesaw& q = seesaw();
            if (side == 2)
            {
                std::snprintf(t1, sizeof(t1), "See-saw pivot  -  balancing %s", q.balance ? "ON" : "OFF");
                std::snprintf(t2, sizeof(t2), "ON: the heavier child sits closer to the middle");
                std::snprintf(t3, sizeof(t3), "Click to switch");
            }
            else
            {
                std::snprintf(t1, sizeof(t1), "See-saw");
                std::snprintf(t2, sizeof(t2), "Click a side to push it down");
                std::snprintf(t3, sizeof(t3), "Push too hard and it slams: BUMP!");
            }
        }
        else show = false;
        if (show)
        {
            const float px = 0.0050f;
            const float w = std::max(ui::textWidth(t1, px * 1.1f), std::max(ui::textWidth(t2, px), ui::textWidth(t3, px))) + 0.05f;
            const float x = clampf(ui::mouse.x + 0.05f, -WORLD_W + 0.02f, WORLD_W - w - 0.02f), y = clampf(ui::mouse.y + 0.05f, -0.80f, 0.60f);
            setColor(rgb(0.04f, 0.08f, 0.14f, 0.88f)); rect(x, y, x + w, y + 0.17f);
            ui::drawText(x + 0.025f, y + 0.12f, px * 1.1f, rgb(1.0f, 0.92f, 0.55f), t1);
            ui::drawText(x + 0.025f, y + 0.07f, px, rgb(1, 1, 1), t2);
            ui::drawText(x + 0.025f, y + 0.025f, px, rgb(0.65f, 1.0f, 0.75f), t3);
        }
    }
    gfx::useTint = old;
}

}   // namespace playground
