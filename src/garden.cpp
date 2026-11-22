#include "garden.h"
#include "gfx.h"
#include "village.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace gfx;

namespace
{

const float SPAN = 2.0f * (WORLD_W + 0.6f);     // wrap width for clouds / birds (off-screen both ends)

Ridge farMts, hill1, hill2, lawn;

struct Flower { float x, y, h, size; int petals, cycles; float phase; Color col; };
struct Blade  { float x, y, h, w, phase; Color col; };
std::vector<Flower> flowers;
std::vector<Blade>  blades;

// x position that wraps around the screen: n whole crossings per loop
float loopX(float x0, float u, float n)
{
    return std::fmod(x0 + SPAN * 0.5f + SPAN * n * u, SPAN) - SPAN * 0.5f;
}

// Catmull-Rom point through 4 control points
float catmull(float p0, float p1, float p2, float p3, float t)
{
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t);
}

// ---------------------------------------------------------------------------
void drawSkyAndFar(float u)
{
    Stop stops[] = {{-1.2f, rgb(0.90f, 0.96f, 0.98f)}, {0.10f, rgb(0.84f, 0.94f, 0.99f)},
                    {0.50f, rgb(0.62f, 0.84f, 0.98f)}, {1.60f, rgb(0.28f, 0.58f, 0.92f)}};
    for (Stop& st : stops) st.c = theme::skyColor(st.c, st.y);      // time-of-day sky
    gfx::useTint = false;                                           // sky is already final
    vGradient(-3.2f, 3.2f, stops, 4);

    glow(-0.35f, 0.74f, 0.9f, theme::sun(rgb(1.0f, 0.97f, 0.80f, 0.75f)), 48);
    setColor(theme::sun(rgb(1.0f, 0.97f, 0.78f)));
    circle(-0.35f, 0.74f, 0.10f, 36);
    theme::drawStarsAndMoon(-0.35f, 0.74f, 0.10f, u);               // integer-cycle twinkle => loops
    gfx::useTint = true;

    drawRidge(farMts, -0.3f, rgb(0.58f, 0.74f, 0.92f), rgb(0.82f, 0.92f, 0.98f));
    drawRidgeShadows(farMts, rgb(0.46f, 0.64f, 0.86f, 0.45f));

    // clouds: translation, wrapping off-screen => seamless
    drawCloud(loopX(-1.3f, u, 1), 0.78f, 0.80f, 0.15f);
    drawCloud(loopX(0.6f, u, 1), 0.60f, 0.55f, 0.10f);
    drawCloud(loopX(1.6f, u, 2), 0.86f, 0.42f, 0.10f);

    // birds: cross the sky several times per loop, wings flap
    for (int i = 0; i < 4; i++)
    {
        const float bx = loopX(-1.8f + i * 0.9f, u, 6.0f + (i & 1));
        const float by = 0.50f + 0.06f * i + 0.04f * wave(u, 3.0f + i, i * 0.21f);
        drawBird(bx, by, 0.030f, wave(u, 150.0f + 10.0f * i, i * 0.3f), rgb(0.14f, 0.18f, 0.28f));
    }
}

void drawWindmill(float x, float y, float u)
{
    // tower
    setColor(rgb(0.0f, 0.0f, 0.0f, 0.12f));
    ellipse(x, y, 0.11f, 0.015f);
    setColor(rgb(0.92f, 0.86f, 0.76f));
    quad(x - 0.07f, y, x + 0.07f, y, x + 0.04f, y + 0.36f, x - 0.04f, y + 0.36f);
    setColor(rgb(0.0f, 0.0f, 0.0f, 0.12f));
    quad(x + 0.01f, y, x + 0.07f, y, x + 0.04f, y + 0.36f, x + 0.01f, y + 0.36f);
    setColor(rgb(0.36f, 0.22f, 0.14f));
    rect(x - 0.012f, y, x + 0.012f, y + 0.06f);
    setColor(rgb(0.72f, 0.24f, 0.20f));
    tri(x - 0.06f, y + 0.36f, x + 0.06f, y + 0.36f, x, y + 0.45f);

    // blades rotate: 6 full turns per loop
    glPushMatrix();
    glTranslatef(x, y + 0.36f, 0.0f);
    glRotatef(360.0f * 6.0f * u, 0.0f, 0.0f, 1.0f);
    for (int k = 0; k < 4; k++)
    {
        glPushMatrix();
        glRotatef(90.0f * k, 0.0f, 0.0f, 1.0f);
        setColor(rgb(0.40f, 0.26f, 0.16f));
        rect(-0.006f, 0.0f, 0.006f, 0.30f);
        setColor(rgb(0.98f, 0.95f, 0.88f));
        quad(0.006f, 0.07f, 0.065f, 0.07f, 0.065f, 0.29f, 0.006f, 0.29f);
        setColor(rgb(0.80f, 0.76f, 0.68f));
        rect(0.006f, 0.15f, 0.065f, 0.155f);
        rect(0.006f, 0.22f, 0.065f, 0.225f);
        glPopMatrix();
    }
    glPopMatrix();
    setColor(rgb(0.30f, 0.20f, 0.14f));
    circle(x, y + 0.36f, 0.018f);
}

void drawFlower(const Flower& f, float u)
{
    const float sway = wave(u, (float)f.cycles, f.phase);          // -1..1
    const float hx   = f.x + sway * 0.035f;
    const float hy   = f.y + f.h;

    // stem (bends more toward the top)
    setColor(rgb(0.20f, 0.52f, 0.24f));
    glBegin(GL_TRIANGLE_STRIP);
    for (int k = 0; k <= 6; k++)
    {
        const float v = k / 6.0f, px = f.x + sway * 0.035f * v * v, py = f.y + f.h * v;
        glVertex2f(px - 0.0045f, py);
        glVertex2f(px + 0.0045f, py);
    }
    glEnd();
    // leaves
    setColor(rgb(0.28f, 0.62f, 0.28f));
    const float lx = f.x + sway * 0.035f * 0.25f, ly = f.y + f.h * 0.45f;
    ellipse(lx - 0.025f, ly, 0.028f, 0.010f, 10);
    ellipse(lx + 0.025f, ly + 0.03f, 0.028f, 0.010f, 10);

    glPushMatrix();
    glTranslatef(hx, hy, 0.0f);
    glRotatef(sway * 10.0f, 0.0f, 0.0f, 1.0f);
    const float s = f.size;
    if (f.petals > 0)
    {
        setColor(f.col);
        for (int k = 0; k < f.petals; k++)
        {
            glPushMatrix();
            glRotatef(360.0f * k / f.petals, 0.0f, 0.0f, 1.0f);
            ellipse(0.0f, s * 0.62f, s * 0.36f, s * 0.62f, 14);
            glPopMatrix();
        }
        setColor(shade(f.col, 0.82f));
        for (int k = 0; k < f.petals; k++)                         // darker inner petals
        {
            glPushMatrix();
            glRotatef(360.0f * k / f.petals + 180.0f / f.petals, 0.0f, 0.0f, 1.0f);
            ellipse(0.0f, s * 0.38f, s * 0.20f, s * 0.38f, 12);
            glPopMatrix();
        }
        setColor(rgb(0.98f, 0.80f, 0.15f));
        circle(0.0f, 0.0f, s * 0.30f, 14);
    }
    else        // tulip
    {
        setColor(f.col);
        ellipse(0.0f, s * 0.15f, s * 0.55f, s * 0.70f, 16);
        tri(-s * 0.55f, s * 0.30f, -s * 0.12f, s * 0.30f, -s * 0.38f, s * 1.10f);
        tri(-s * 0.18f, s * 0.30f, s * 0.18f, s * 0.30f, 0.0f, s * 1.25f);
        tri(s * 0.12f, s * 0.30f, s * 0.55f, s * 0.30f, s * 0.38f, s * 1.10f);
        setColor(shade(f.col, 0.80f));
        tri(-s * 0.12f, s * 0.30f, s * 0.18f, s * 0.30f, 0.0f, s * 1.0f);
    }
    glPopMatrix();
}

void drawButterfly(float x, float y, float size, float flap, const Color& wing, float tilt)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glRotatef(tilt, 0.0f, 0.0f, 1.0f);
    glScalef(size, size, 1.0f);
    const float a = 8.0f + 42.0f * (0.5f + 0.5f * flap);            // wing angle, degrees (rotation)
    for (int side = -1; side <= 1; side += 2)
    {
        glPushMatrix();
        glRotatef(side * a, 0.0f, 0.0f, 1.0f);
        setColor(wing);
        glPushMatrix();
        glTranslatef(side * 0.58f, 0.16f, 0.0f);
        glRotatef(side * 18.0f, 0.0f, 0.0f, 1.0f);
        ellipse(0.0f, 0.0f, 0.58f, 0.36f, 18);
        glPopMatrix();
        setColor(shade(wing, 0.85f));
        ellipse(side * 0.42f, -0.30f, 0.36f, 0.26f, 16);
        setColor(rgb(1.0f, 1.0f, 1.0f, 0.8f));
        circle(side * 0.74f, 0.20f, 0.09f, 10);
        circle(side * 0.40f, -0.30f, 0.07f, 10);
        glPopMatrix();
    }
    setColor(rgb(0.15f, 0.10f, 0.12f));
    ellipse(0.0f, 0.0f, 0.07f, 0.42f, 10);
    glLineWidth(1.5f);
    glBegin(GL_LINES);
    glVertex2f(0.0f, 0.38f); glVertex2f(-0.20f, 0.70f);
    glVertex2f(0.0f, 0.38f); glVertex2f(0.20f, 0.70f);
    glEnd();
    glLineWidth(1.0f);
    glPopMatrix();
}

void drawGardener(float x, float y, float scale, float facing, float u)
{
    const float step = wave(u, 36.0f);                              // 12 steps per lap, 3 laps per loop
    const float bob  = 0.012f * std::fabs(step);

    glPushMatrix();
    glTranslatef(x, y + bob * scale, 0.0f);
    glScalef(scale * facing, scale, 1.0f);

    setColor(rgb(0.0f, 0.0f, 0.0f, 0.20f));
    ellipse(0.0f, 0.0f, 0.22f, 0.04f);

    // legs (rotate about the hip)
    for (int k = 0; k < 2; k++)
    {
        glPushMatrix();
        glTranslatef(0.0f, 0.46f, 0.0f);
        glRotatef((k ? -1.0f : 1.0f) * 26.0f * step, 0.0f, 0.0f, 1.0f);
        setColor(k ? rgb(0.42f, 0.30f, 0.20f) : rgb(0.50f, 0.36f, 0.24f));
        rect(-0.04f, -0.42f, 0.04f, 0.0f);
        setColor(rgb(0.22f, 0.14f, 0.10f));
        rect(-0.045f, -0.46f, 0.085f, -0.40f);
        glPopMatrix();
    }
    // back arm swings
    glPushMatrix();
    glTranslatef(0.0f, 0.76f, 0.0f);
    glRotatef(-24.0f * step, 0.0f, 0.0f, 1.0f);
    setColor(rgb(0.22f, 0.45f, 0.30f));
    rect(-0.03f, -0.28f, 0.03f, 0.0f);
    setColor(rgb(0.96f, 0.76f, 0.60f));
    circle(0.0f, -0.30f, 0.035f, 10);
    glPopMatrix();

    // torso + apron
    setColor(rgb(0.28f, 0.58f, 0.38f));
    quad(-0.12f, 0.44f, 0.12f, 0.44f, 0.11f, 0.80f, -0.11f, 0.80f);
    setColor(rgb(0.93f, 0.88f, 0.72f));
    quad(-0.08f, 0.44f, 0.09f, 0.44f, 0.08f, 0.68f, -0.07f, 0.68f);

    // front arm holds the watering can
    glPushMatrix();
    glTranslatef(0.0f, 0.76f, 0.0f);
    glRotatef(-62.0f, 0.0f, 0.0f, 1.0f);
    setColor(rgb(0.30f, 0.62f, 0.42f));
    rect(-0.03f, -0.26f, 0.03f, 0.0f);
    glPopMatrix();
    const float cx = 0.22f, cy = 0.56f;                             // can position
    setColor(rgb(0.52f, 0.64f, 0.76f));
    ellipse(cx, cy, 0.075f, 0.065f, 16);
    setColor(rgb(0.40f, 0.52f, 0.64f));
    thickLine(cx + 0.04f, cy + 0.0f, cx + 0.17f, cy + 0.07f, 0.022f);   // spout
    setColor(rgb(0.96f, 0.76f, 0.60f));
    circle(0.20f, 0.62f, 0.032f, 10);                               // hand
    // droplets
    for (int k = 0; k < 3; k++)
    {
        const float p = fract(30.0f * u + k / 3.0f);
        setColor(rgb(0.45f, 0.75f, 0.98f, 0.9f * (1.0f - p)));
        circle(cx + 0.18f + 0.05f * p, cy + 0.06f - 0.30f * p * p - 0.02f * p, 0.011f, 8);
    }

    // head, face, straw hat
    setColor(rgb(0.96f, 0.76f, 0.60f));
    circle(0.0f, 0.90f, 0.085f, 18);
    setColor(rgb(0.15f, 0.10f, 0.10f));
    circle(0.040f, 0.91f, 0.009f, 8);
    setColor(rgb(0.93f, 0.80f, 0.40f));
    ellipse(0.0f, 0.96f, 0.19f, 0.032f, 20);
    ellipse(0.0f, 0.99f, 0.095f, 0.065f, 20);
    setColor(rgb(0.80f, 0.20f, 0.20f));
    rect(-0.092f, 0.965f, 0.092f, 0.99f);
    glPopMatrix();
}

}   // namespace

// ---------------------------------------------------------------------------
void initGarden()
{
    farMts = makeRidge(-2.8f, 2.8f, 0.02f, 0.12f, 0.30f, 5.3f, 2.2f);
    hill1  = makeRolling(-2.8f, 2.8f, 0.02f, 0.04f, 0.14f, 0.7f, 2.0f);
    hill2  = makeRolling(-2.8f, 2.8f, 0.02f, -0.10f, 0.10f, 3.4f, 2.7f);
    lawn   = makeRolling(-2.8f, 2.8f, 0.02f, -0.19f, 0.03f, 1.9f, 3.0f);

    static const Color petalCols[] = {rgb(0.95f, 0.35f, 0.50f), rgb(1.00f, 0.82f, 0.20f), rgb(0.92f, 0.20f, 0.20f),
                                      rgb(0.70f, 0.40f, 0.90f), rgb(0.98f, 0.98f, 0.95f), rgb(1.00f, 0.55f, 0.20f),
                                      rgb(0.35f, 0.55f, 0.95f)};
    static const int cyc[] = {8, 10, 12, 15};

    flowers.clear();
    auto addBed = [&](float xa, float xb, float ya, float yb, int count, float seed)
    {
        for (int i = 0; i < count; i++)
        {
            Flower f;
            f.x      = lerpf(xa, xb, hash1(i * 12.9f + seed));
            f.y      = lerpf(ya, yb, hash1(i * 78.2f + seed * 2.0f));
            const float depth = (0.0f - f.y);                        // lower on screen = closer = bigger
            f.h      = (0.10f + 0.10f * hash1(i * 4.1f + seed)) * (0.7f + 0.5f * depth);
            f.size   = (0.030f + 0.020f * hash1(i * 9.3f + seed)) * (0.7f + 0.5f * depth);
            const int kind = (int)(hash1(i * 5.7f + seed) * 9.0f);
            f.petals = kind < 6 ? (5 + (kind & 1)) : 0;               // 0 = tulip
            f.cycles = cyc[(int)(hash1(i * 2.9f + seed) * 4.0f) & 3];
            f.phase  = hash1(i * 6.1f + seed);
            f.col    = petalCols[(int)(hash1(i * 3.3f + seed) * 7.0f) % 7];
            flowers.push_back(f);
        }
    };
    addBed(-1.75f, -0.62f, -0.92f, -0.50f, 34, 1.0f);       // big left bed
    addBed(0.62f, 1.75f, -1.00f, -0.86f, 18, 7.0f);         // right front
    addBed(-0.55f, 0.35f, -0.30f, -0.24f, 12, 13.0f);       // along the fence
    addBed(0.95f, 1.70f, -0.30f, -0.24f, 10, 19.0f);
    std::stable_sort(flowers.begin(), flowers.end(), [](const Flower& a, const Flower& b) { return a.y > b.y; });

    blades.clear();
    for (int i = 0; i < 200; i++)
    {
        Blade b;
        b.x     = lerpf(-WORLD_W - 0.1f, WORLD_W + 0.1f, hash1(i * 3.7f));
        b.y     = lerpf(-1.04f, -0.90f, hash1(i * 8.1f));
        b.h     = 0.07f + 0.10f * hash1(i * 5.5f);
        b.w     = 0.012f + 0.008f * hash1(i * 1.3f);
        b.phase = hash1(i * 2.2f);
        b.col   = mix(rgb(0.20f, 0.52f, 0.20f), rgb(0.45f, 0.78f, 0.30f), hash1(i * 6.6f));
        blades.push_back(b);
    }
}

void drawGarden(const AnimState& st)
{
    const float u = (float)(std::fmod(st.gardenTime, GARDEN_LOOP_SECONDS) / GARDEN_LOOP_SECONDS);

    // while fading in, the garden settles from a slight zoom to rest
    const float settle = 1.0f + 0.30f * (1.0f - st.gardenBlend);
    pushCamera(settle, 0.0f, -0.10f, 0.0f, 0.0f);

    drawSkyAndFar(u);

    // --- rolling hills + windmill ---
    drawRidge(hill1, -0.4f, rgb(0.52f, 0.78f, 0.54f), rgb(0.62f, 0.84f, 0.58f));
    drawWindmill(1.18f, ridgeAt(hill1, 1.18f) - 0.01f, u);
    drawRidge(hill2, -0.5f, rgb(0.40f, 0.72f, 0.42f), rgb(0.52f, 0.80f, 0.46f));

    // distant tree line
    for (int i = 0; i < 16; i++)
    {
        const float x = -2.0f + i * 0.26f + 0.08f * hash1(i * 3.0f);
        const float h = 0.16f + 0.08f * hash1(i * 7.0f);
        if (visible(x, ridgeAt(hill2, x) + h * 0.6f, h * 0.6f))
            drawRoundTree(x, ridgeAt(hill2, x) - 0.015f, h, wave(u, 10.0f, hash1(i * 1.7f)),
                          rgb(0.20f, 0.52f, 0.28f), rgb(0.36f, 0.68f, 0.34f));
    }

    // --- lawn ---
    drawRidge(lawn, -1.3f, rgb(0.46f, 0.77f, 0.40f), rgb(0.26f, 0.56f, 0.26f));
    setColor(rgb(0.60f, 0.86f, 0.46f, 0.35f));
    ellipse(0.2f, -0.55f, 1.3f, 0.35f, 40);                         // sunlit patch

    // --- winding path from the cottage door to the viewer ---
    {
        static const float PX[] = {-1.04f, -1.00f, -0.62f, -0.18f, 0.06f, 0.16f, 0.20f};
        static const float PY[] = {-0.17f, -0.21f, -0.42f, -0.64f, -0.86f, -1.12f, -1.30f};
        static const float PW[] = {0.10f, 0.11f, 0.17f, 0.27f, 0.40f, 0.60f, 0.70f};
        for (int pass = 0; pass < 2; pass++)
        {
            glBegin(GL_TRIANGLE_STRIP);
            for (int seg = 0; seg < 5; seg++)
                for (int k = 0; k < 10; k++)
                {
                    const float t = k / 10.0f;
                    const float cx = catmull(PX[seg], PX[seg + 1], PX[seg + 2], PX[std::min(seg + 3, 6)], t);
                    const float cy = catmull(PY[seg], PY[seg + 1], PY[seg + 2], PY[std::min(seg + 3, 6)], t);
                    const float w  = catmull(PW[seg], PW[seg + 1], PW[seg + 2], PW[std::min(seg + 3, 6)], t) * (pass ? 1.0f : 1.22f);
                    vtx(pass ? rgb(0.95f, 0.86f, 0.62f) : rgb(0.72f, 0.60f, 0.40f), cx - w * 0.5f, cy);
                    vtx(pass ? rgb(0.88f, 0.76f, 0.52f) : rgb(0.72f, 0.60f, 0.40f), cx + w * 0.5f, cy);
                }
            glEnd();
        }
        setColor(rgb(0.78f, 0.68f, 0.48f));                         // pebbles
        for (int i = 0; i < 26; i++)
        {
            const float v  = hash1(i * 4.3f);
            const float cx = lerpf(-0.95f, 0.12f, 1.0f - v), cy = lerpf(-0.24f, -1.15f, v);
            ellipse(cx + (hash1(i * 9.0f) - 0.5f) * 0.1f * (1 + v * 3), cy, 0.012f + 0.014f * v, 0.007f + 0.008f * v, 8);
        }
    }

    // --- cottage (the same house style as the village) + chimney smoke ---
    drawHouse(-1.12f, -0.17f, 0.62f, 1, true, GARDEN_LOOP_SECONDS * u);

    // --- fence, trees, bushes ---
    drawFence(-0.58f, 1.85f, -0.235f, 0.12f, rgb(0.98f, 0.95f, 0.88f));
    drawRoundTree(0.30f, -0.20f, 0.95f, wave(u, 6.0f, 0.1f), rgb(0.16f, 0.46f, 0.24f), rgb(0.34f, 0.68f, 0.30f), true);
    drawRoundTree(1.62f, -0.20f, 0.78f, wave(u, 5.0f, 0.6f), rgb(0.18f, 0.50f, 0.26f), rgb(0.38f, 0.70f, 0.32f));
    drawBush(-0.55f, -0.27f, 0.075f, rgb(0.14f, 0.42f, 0.22f), rgb(0.30f, 0.62f, 0.30f));
    drawBush(0.78f, -0.27f, 0.085f, rgb(0.14f, 0.42f, 0.22f), rgb(0.30f, 0.62f, 0.30f));
    drawBush(-1.70f, -0.22f, 0.090f, rgb(0.14f, 0.42f, 0.22f), rgb(0.30f, 0.62f, 0.30f));

    // --- pond with stones, lily pads and expanding ripples ---
    {
        const float px = 1.02f, py = -0.54f, rx = 0.62f, ry = 0.20f;
        setColor(rgb(0.0f, 0.0f, 0.0f, 0.16f));
        ellipse(px, py - 0.03f, rx + 0.07f, ry + 0.05f, 40);
        setColor(rgb(0.66f, 0.64f, 0.60f));
        ellipse(px, py, rx + 0.05f, ry + 0.035f, 40);
        setColor(rgb(0.24f, 0.54f, 0.74f));
        ellipse(px, py, rx, ry, 40);
        setColor(rgb(0.40f, 0.74f, 0.90f));
        ellipse(px, py + 0.01f, rx * 0.88f, ry * 0.78f, 40);
        setColor(rgb(0.72f, 0.92f, 0.98f, 0.55f));
        ellipse(px - 0.18f, py + 0.05f, rx * 0.40f, ry * 0.30f, 30);   // sky reflection

        // ripples: 20 expanding rings per loop (every 3 s)
        glLineWidth(2.0f);
        for (int c = 0; c < 2; c++)
        {
            const float cx = px + (c ? -0.22f : 0.20f), cy = py + (c ? -0.04f : 0.02f);
            for (int k = 0; k < 3; k++)
            {
                const float p = fract(20.0f * u + k / 3.0f + c * 0.5f);
                setColor(rgb(1.0f, 1.0f, 1.0f, 0.75f * (1.0f - p)));
                ring(cx, cy, 0.04f + 0.30f * p, 0.012f + 0.085f * p);
            }
        }
        glLineWidth(1.0f);
        // shimmering highlights slide across the water
        for (int k = 0; k < 5; k++)
        {
            const float hx = px + 0.38f * wave(u, 6.0f + k, k * 0.2f);
            const float hy = py - 0.08f + 0.04f * k;
            setColor(rgb(1.0f, 1.0f, 1.0f, 0.25f + 0.2f * wave(u, 12.0f, k * 0.31f)));
            ellipse(hx, hy, 0.05f, 0.006f, 10);
        }
        // lily pads bob gently
        for (int k = 0; k < 3; k++)
        {
            const float lx = px - 0.25f + k * 0.30f, ly = py + 0.01f - 0.05f * (k & 1) + 0.006f * wave(u, 15.0f, k * 0.3f);
            setColor(rgb(0.20f, 0.60f, 0.30f));
            ellipse(lx, ly, 0.075f, 0.026f, 18);
            setColor(rgb(0.40f, 0.74f, 0.90f));
            tri(lx, ly, lx + 0.09f, ly + 0.01f, lx + 0.09f, ly - 0.01f);
            if (k == 1)
            {
                setColor(rgb(1.0f, 0.62f, 0.78f));
                ellipse(lx, ly + 0.02f, 0.025f, 0.03f, 12);
                setColor(rgb(1.0f, 0.85f, 0.35f));
                circle(lx, ly + 0.015f, 0.010f, 8);
            }
        }
        // rim stones
        for (int k = 0; k < 14; k++)
        {
            const float a = TAU * k / 14.0f + 0.2f;
            setColor(shade(rgb(0.72f, 0.70f, 0.66f), 0.85f + 0.2f * hash1(k * 5.0f)));
            ellipse(px + std::cos(a) * (rx + 0.05f), py + std::sin(a) * (ry + 0.04f), 0.04f, 0.022f, 10);
        }
        // reeds on the left bank
        for (int k = 0; k < 6; k++)
        {
            const float bx = px - rx - 0.02f + k * 0.022f, h = 0.20f + 0.08f * hash1(k * 4.0f);
            const float sw = wave(u, 10.0f, k * 0.17f) * 0.025f;
            setColor(rgb(0.30f, 0.56f, 0.26f));
            glBegin(GL_TRIANGLE_STRIP);
            for (int s = 0; s <= 4; s++)
            {
                const float v = s / 4.0f;
                glVertex2f(bx - 0.004f + sw * v * v, py - 0.06f + h * v);
                glVertex2f(bx + 0.004f + sw * v * v, py - 0.06f + h * v);
            }
            glEnd();
            if (k % 2 == 0)
            {
                setColor(rgb(0.42f, 0.26f, 0.14f));
                ellipse(bx + sw, py - 0.06f + h + 0.015f, 0.008f, 0.026f, 8);
            }
        }
    }

    // --- gardener on a closed route (3 laps per loop), turning smoothly ---
    {
        const float a  = TAU * 3.0f * u;
        const float gx = -0.25f + 0.72f * std::cos(a);
        const float gy = -0.84f + 0.06f * std::sin(a);
        const float facing = clampf(-std::sin(a) * 6.0f, -1.0f, 1.0f);   // squashes through 0 when turning
        drawGardener(gx, gy, 0.46f, facing < 0 ? std::min(facing, -0.12f) : std::max(facing, 0.12f), u);
    }

    // --- flower beds (sorted far to near) ---
    for (const Flower& f : flowers)
        if (visible(f.x, f.y + f.h * 0.5f, f.h + f.size))
            drawFlower(f, u);

    // --- butterflies: Lissajous paths (integer cycles) + wing rotation ---
    {
        struct B { float cx, cy, ax, ay, a, b, p1, p2, size; Color col; };
        static const B bf[] = {
            {-1.15f, -0.52f, 0.50f, 0.17f, 7.0f, 11.0f, 0.00f, 0.25f, 0.045f, rgb(1.00f, 0.62f, 0.20f)},
            {1.10f, -0.62f, 0.45f, 0.20f, 9.0f, 13.0f, 0.30f, 0.10f, 0.040f, rgb(0.55f, 0.55f, 1.00f)},
            {-0.05f, -0.42f, 0.38f, 0.14f, 5.0f, 8.0f, 0.60f, 0.70f, 0.036f, rgb(1.00f, 0.45f, 0.70f)}};
        int i = 0;
        for (const B& b : bf)
        {
            const float x = b.cx + b.ax * wave(u, b.a, b.p1) + 0.02f * wave(u, 41.0f + i, 0.0f);
            const float y = b.cy + b.ay * wave(u, b.b, b.p2) + 0.02f * wave(u, 53.0f + i, 0.3f);
            drawButterfly(x, y, b.size, wave(u, 180.0f + 12.0f * i), b.col, 14.0f * wave(u, b.a, b.p1 + 0.25f));
            ++i;
        }
    }

    // --- foreground grass: a travelling sine wave sways every blade ---
    glBegin(GL_TRIANGLES);
    for (const Blade& b : blades)
    {
        if (std::fabs(toScreenX(b.x)) > WORLD_W + 0.1f) continue;
        const float sway = std::sin(TAU * 12.0f * u + b.x * 2.4f + b.phase * 2.0f) * 0.30f * b.h;
        setColor(b.col);
        glVertex2f(b.x - b.w, b.y);
        glVertex2f(b.x + b.w, b.y);
        glVertex2f(b.x + sway, b.y + b.h);
    }
    glEnd();

    popCamera();
}
