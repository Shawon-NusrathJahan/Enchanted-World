#include "garden.h"
#include "gfx.h"
#include "village.h"
#include "theme.h"
#include "gardensim.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace gfx;

namespace
{

const float SPAN = 2.0f * (WORLD_W + 0.6f);     // wrap width for clouds / birds (off-screen both ends)

Ridge farMts, hill1, hill2, lawn;

struct Blade  { float x, y, h, w, phase; Color col; };
std::vector<int>    flowerOrder;      // flower indices sorted far -> near (for drawing)
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

void drawWindmill(float x, float y, float angle)
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

    // blades: angle comes from the simulation (clicking the windmill makes it spin faster)
    glPushMatrix();
    glTranslatef(x, y + 0.36f, 0.0f);
    glRotatef(angle, 0.0f, 0.0f, 1.0f);
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

void drawDrop(float x, float y, float size, float alpha)
{
    setColor(rgb(0.30f, 0.62f, 0.95f, alpha));
    circle(x, y, size, 12);
    tri(x - size * 0.95f, y + size * 0.30f, x + size * 0.95f, y + size * 0.30f, x, y + size * 2.3f);
    setColor(rgb(1.0f, 1.0f, 1.0f, 0.75f * alpha));
    circle(x - size * 0.35f, y - size * 0.15f, size * 0.28f, 8);
}

// A flower drawn from its simulated state: seed -> sprout -> bud -> bloom, wilting and dying.
void drawFlower(const gardensim::Flower& f, float u)
{
    const float g    = f.growth;
    float wilt = clampf((0.65f - f.health) * 2.2f, 0.0f, 1.0f);
    if (f.water < 0.20f) wilt = std::max(wilt, clampf((0.20f - f.water) * 4.0f, 0.0f, 0.7f));
    if (f.dead) wilt = 1.0f;
    const float dir  = f.phase > 0.5f ? 1.0f : -1.0f;
    const float sway = wave(u, (float)f.cycles, f.phase) * (1.0f - 0.7f * wilt);

    // soil mound
    setColor(rgb(0.42f, 0.30f, 0.19f, 0.9f));
    ellipse(f.x, f.y - 0.004f, f.size * 0.85f, f.size * 0.28f, 12);
    if (g < 0.02f && !f.dead) { setColor(rgb(0.30f, 0.20f, 0.12f)); circle(f.x, f.y - 0.002f, f.size * 0.16f, 6); return; }

    const float H    = f.h * clampf(g / 0.6f, 0.08f, 1.0f);
    const float off  = sway * 0.035f + wilt * dir * 0.05f;
    const float droop = 0.28f * wilt;
    const Color stemC = mix(rgb(0.20f, 0.52f, 0.24f), rgb(0.46f, 0.36f, 0.20f), wilt);
    const Color leafC = mix(rgb(0.28f, 0.62f, 0.28f), rgb(0.52f, 0.42f, 0.22f), wilt);

    setColor(stemC);
    glBegin(GL_TRIANGLE_STRIP);
    for (int k = 0; k <= 6; k++)
    {
        const float v = k / 6.0f, px = f.x + off * v * v, py = f.y + H * v * (1.0f - droop * v * v);
        glVertex2f(px - 0.0045f, py);
        glVertex2f(px + 0.0045f, py);
    }
    glEnd();

    const float hx = f.x + off, hy = f.y + H * (1.0f - droop);
    const float lf = clampf((g - 0.10f) / 0.30f, 0.0f, 1.0f);
    if (lf > 0.0f)
    {
        setColor(leafC);
        const float lx = f.x + off * 0.06f, ly = f.y + H * 0.45f;
        ellipse(lx - 0.025f * lf, ly, 0.028f * lf, 0.010f * lf, 10);
        ellipse(lx + 0.025f * lf, ly + 0.03f * lf, 0.028f * lf, 0.010f * lf, 10);
    }

    const float s = f.size;
    glPushMatrix();
    glTranslatef(hx, hy, 0.0f);
    glRotatef(sway * 10.0f + wilt * dir * 40.0f, 0.0f, 0.0f, 1.0f);

    if (f.dead)                                         // withered head
    {
        setColor(rgb(0.40f, 0.30f, 0.20f));
        ellipse(0.0f, 0.0f, s * 0.38f, s * 0.22f, 10);
        setColor(rgb(0.30f, 0.22f, 0.14f));
        circle(0.0f, 0.0f, s * 0.12f, 8);
    }
    else if (g < 0.45f)                                 // sprout: two small leaves
    {
        const float k = 0.5f + g;
        setColor(leafC);
        ellipse(-0.014f * k, 0.006f, 0.020f * k, 0.008f * k, 8);
        ellipse(0.014f * k, 0.010f, 0.020f * k, 0.008f * k, 8);
    }
    else if (g < 0.75f)                                 // bud
    {
        const float k = 0.55f + 0.7f * (g - 0.45f) / 0.30f;
        setColor(rgb(0.30f, 0.60f, 0.28f));
        ellipse(0.0f, -s * 0.05f, s * 0.26f * k, s * 0.18f * k, 10);
        setColor(mix(f.col, rgb(0.52f, 0.40f, 0.28f), wilt * 0.85f));
        ellipse(0.0f, s * 0.30f * k, s * 0.24f * k, s * 0.44f * k, 12);
    }
    else                                                // open flower
    {
        const float bs = smoothstep(0.0f, 1.0f, (g - 0.75f) / 0.25f);
        const float sc = s * (0.45f + 0.55f * bs);
        const Color pc = mix(f.col, rgb(0.52f, 0.40f, 0.28f), wilt * 0.85f);
        if (f.petals > 0)
        {
            setColor(pc);
            for (int k = 0; k < f.petals; k++)
            {
                glPushMatrix();
                glRotatef(360.0f * k / f.petals, 0.0f, 0.0f, 1.0f);
                ellipse(0.0f, sc * 0.62f, sc * 0.36f, sc * 0.62f, 14);
                glPopMatrix();
            }
            setColor(shade(pc, 0.82f));
            for (int k = 0; k < f.petals; k++)
            {
                glPushMatrix();
                glRotatef(360.0f * k / f.petals + 180.0f / f.petals, 0.0f, 0.0f, 1.0f);
                ellipse(0.0f, sc * 0.38f, sc * 0.20f, sc * 0.38f, 12);
                glPopMatrix();
            }
            setColor(mix(rgb(0.98f, 0.80f, 0.15f), rgb(0.40f, 0.30f, 0.18f), wilt));
            circle(0.0f, 0.0f, sc * 0.30f, 14);
        }
        else                                            // tulip
        {
            setColor(pc);
            ellipse(0.0f, sc * 0.15f, sc * 0.55f, sc * 0.70f, 16);
            tri(-sc * 0.55f, sc * 0.30f, -sc * 0.12f, sc * 0.30f, -sc * 0.38f, sc * 1.10f);
            tri(-sc * 0.18f, sc * 0.30f, sc * 0.18f, sc * 0.30f, 0.0f, sc * 1.25f);
            tri(sc * 0.12f, sc * 0.30f, sc * 0.55f, sc * 0.30f, sc * 0.38f, sc * 1.10f);
            setColor(shade(pc, 0.80f));
            tri(-sc * 0.12f, sc * 0.30f, sc * 0.18f, sc * 0.30f, 0.0f, sc * 1.0f);
        }
    }
    glPopMatrix();

    // a water drop above every flower that is asking for water
    if (!f.dead && f.water < 0.45f)
    {
        const float need = clampf(1.0f - f.water / 0.45f, 0.0f, 1.0f);
        drawDrop(hx, hy + s * 1.7f + 0.035f + 0.006f * wave(u, 20.0f, f.phase), 0.0085f, 0.55f + 0.45f * need);
    }
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

void sparkle(float x, float y, float sz, float alpha)
{
    glColor4f(1.0f, 0.97f, 0.75f, alpha);
    glBegin(GL_QUADS);
    glVertex2f(x - sz, y); glVertex2f(x, y + sz * 0.22f); glVertex2f(x + sz, y); glVertex2f(x, y - sz * 0.22f);
    glVertex2f(x, y - sz); glVertex2f(x + sz * 0.22f, y); glVertex2f(x, y + sz); glVertex2f(x - sz * 0.22f, y);
    glEnd();
}

// Glow + orbiting sparkles + a trail of falling sparkles. Only butterflies that evaded the cursor get it.
void drawShimmer(const gardensim::Butterfly& b)
{
    if (b.shimmer <= 0.0f) return;
    const float fade  = clampf(b.shimmer / 0.7f, 0.0f, 1.0f);       // fades out over the last 0.7 s
    const float clock = b.flapPhase * 0.18f;
    const bool  oldTint = gfx::useTint;
    gfx::useTint = false;                                           // the glow is its own light source
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);                              // additive: glowing look

    glow(b.x, b.y, 0.115f + 0.015f * std::sin(clock * 3.0f), rgb(1.0f, 0.92f, 0.60f, 0.55f * fade), 28);
    glow(b.x, b.y, 0.060f, rgb(1.0f, 1.0f, 1.0f, 0.45f * fade), 20);
    for (int k = 0; k < 6; k++)
    {
        const float a  = clock + k * TAU / 6.0f;
        const float r  = 0.070f + 0.020f * std::sin(clock * 2.0f + k);
        const float tw = 0.5f + 0.5f * std::sin(clock * 5.0f + k * 1.7f);
        sparkle(b.x + std::cos(a) * r, b.y + std::sin(a) * r * 0.8f, 0.010f + 0.010f * tw, (0.45f + 0.55f * tw) * fade);
    }
    for (const gardensim::Spark& sp : b.sparks)
        if (sp.on) sparkle(sp.x, sp.y, 0.007f, (1.0f - sp.age / 0.9f) * fade);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gfx::useTint = oldTint;
}

void drawGardener(float x, float y, float scale, const gardensim::Gardener& g, float alphaScale)
{
    const float step = g.walking ? std::sin(g.walkPhase) : 0.0f;
    const float bob  = 0.012f * std::fabs(step);
    const float pour = g.watering, dip = g.filling;
    const float oldAlpha = gfx::alphaMul;
    gfx::alphaMul = g.alpha * alphaScale;

    glPushMatrix();
    glTranslatef(x, y + bob * scale, 0.0f);
    glScalef(scale * g.facing, scale, 1.0f);

    setColor(rgb(0.0f, 0.0f, 0.0f, 0.20f));
    ellipse(0.0f, 0.0f, 0.22f, 0.04f);

    for (int k = 0; k < 2; k++)                                     // legs (rotate about the hip)
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
    glPushMatrix();                                                 // back arm swings
    glTranslatef(0.0f, 0.76f, 0.0f);
    glRotatef(-24.0f * step, 0.0f, 0.0f, 1.0f);
    setColor(rgb(0.22f, 0.45f, 0.30f));
    rect(-0.03f, -0.28f, 0.03f, 0.0f);
    setColor(rgb(0.96f, 0.76f, 0.60f));
    circle(0.0f, -0.30f, 0.035f, 10);
    glPopMatrix();

    setColor(rgb(0.28f, 0.58f, 0.38f));                             // torso + apron
    quad(-0.12f, 0.44f, 0.12f, 0.44f, 0.11f, 0.80f, -0.11f, 0.80f);
    setColor(rgb(0.93f, 0.88f, 0.72f));
    quad(-0.08f, 0.44f, 0.09f, 0.44f, 0.08f, 0.68f, -0.07f, 0.68f);

    glPushMatrix();                                                 // front arm
    glTranslatef(0.0f, 0.76f, 0.0f);
    glRotatef(-62.0f - 20.0f * dip, 0.0f, 0.0f, 1.0f);
    setColor(rgb(0.30f, 0.62f, 0.42f));
    rect(-0.03f, -0.26f, 0.03f, 0.0f);
    glPopMatrix();

    // watering can: tilts to pour, dips toward the pond when filling
    const float cx = 0.22f, cy = 0.56f;
    glPushMatrix();
    glTranslatef(0.20f + 0.10f * dip, 0.62f - 0.30f * dip, 0.0f);
    glRotatef(-30.0f * pour - 12.0f * dip, 0.0f, 0.0f, 1.0f);
    glTranslatef(-0.20f, -0.62f, 0.0f);
    setColor(rgb(0.52f, 0.64f, 0.76f));
    ellipse(cx, cy, 0.075f, 0.065f, 16);
    if (g.can > 0.5f)                                               // water level shows in the can
    {
        setColor(rgb(0.30f, 0.60f, 0.92f, 0.9f));
        ellipse(cx, cy + 0.045f, 0.060f * (0.4f + 0.6f * g.can / gardensim::CAN_CAPACITY), 0.012f, 10);
    }
    setColor(rgb(0.40f, 0.52f, 0.64f));
    thickLine(cx + 0.04f, cy, cx + 0.17f, cy + 0.07f, 0.022f);      // spout
    setColor(rgb(0.96f, 0.76f, 0.60f));
    circle(0.20f, 0.62f, 0.032f, 10);                               // hand
    glPopMatrix();

    if (pour > 0.5f)                                                // droplets fall from the spout onto the flower
        for (int k = 0; k < 4; k++)
        {
            const float p = fract(g.time * 2.4f + k * 0.25f);
            setColor(rgb(0.45f, 0.75f, 0.98f, 0.9f * (1.0f - p * 0.6f)));
            circle(0.372f + 0.03f * p, 0.54f - 0.40f * p * p - 0.05f * p, 0.011f, 8);
        }

    setColor(rgb(0.96f, 0.76f, 0.60f));                             // head, face, straw hat
    circle(0.0f, 0.90f, 0.085f, 18);
    setColor(rgb(0.15f, 0.10f, 0.10f));
    circle(0.040f, 0.91f, 0.009f, 8);
    setColor(rgb(0.93f, 0.80f, 0.40f));
    ellipse(0.0f, 0.96f, 0.19f, 0.032f, 20);
    ellipse(0.0f, 0.99f, 0.095f, 0.065f, 20);
    setColor(rgb(0.80f, 0.20f, 0.20f));
    rect(-0.092f, 0.965f, 0.092f, 0.99f);
    glPopMatrix();
    gfx::alphaMul = oldAlpha;
}

}   // namespace

// ---------------------------------------------------------------------------
void initGarden()
{
    farMts = makeRidge(-2.8f, 2.8f, 0.02f, 0.12f, 0.30f, 5.3f, 2.2f);
    hill1  = makeRolling(-2.8f, 2.8f, 0.02f, 0.04f, 0.14f, 0.7f, 2.0f);
    hill2  = makeRolling(-2.8f, 2.8f, 0.02f, -0.10f, 0.10f, 3.4f, 2.7f);
    lawn   = makeRolling(-2.8f, 2.8f, 0.02f, -0.19f, 0.03f, 1.9f, 3.0f);

    gardensim::init();                                               // flowers, rows, gardener, butterflies
    gardensim::setWindmillHub(1.18f, ridgeAt(hill1, 1.18f) - 0.01f + 0.36f);
    flowerOrder.clear();
    for (size_t i = 0; i < gardensim::flowers().size(); i++) flowerOrder.push_back((int)i);
    std::stable_sort(flowerOrder.begin(), flowerOrder.end(),
                     [](int a, int b) { return gardensim::flowers()[a].y > gardensim::flowers()[b].y; });   // far first

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
    drawWindmill(1.18f, ridgeAt(hill1, 1.18f) - 0.01f, gardensim::windAngle());
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
        const float px = gardensim::POND_X, py = gardensim::POND_Y, rx = gardensim::POND_RX, ry = gardensim::POND_RY;
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

        // ripples appear ONLY where the user clicked the water (simulation: gardensim::ripples)
        glLineWidth(2.0f);
        for (const gardensim::Ripple& r : gardensim::ripples())
        {
            const float age = r.age / gardensim::RIPPLE_LIFE;
            for (int k = 0; k < 3; k++)
            {
                const float p = age - 0.10f * k;
                if (p <= 0.0f || p >= 1.0f) continue;
                const float rr = 0.03f + 0.26f * p, ry2 = rr * (ry / rx);
                glColor4f(1.0f, 1.0f, 1.0f, 0.85f * (1.0f - p) * (1.0f - p * 0.3f));
                glBegin(GL_LINES);
                for (int i = 0; i < 40; i++)
                {
                    const float a0 = TAU * i / 40.0f, a1 = TAU * (i + 1) / 40.0f;
                    const float x0 = r.x + std::cos(a0) * rr, y0 = r.y + std::sin(a0) * ry2;
                    const float x1 = r.x + std::cos(a1) * rr, y1 = r.y + std::sin(a1) * ry2;
                    auto inside = [&](float x, float y) { const float dx = (x - px) / rx, dy = (y - py) / ry; return dx * dx + dy * dy < 0.94f; };
                    if (inside(x0, y0) && inside(x1, y1)) { glVertex2f(x0, y0); glVertex2f(x1, y1); }
                }
                glEnd();
            }
        }
        glLineWidth(1.0f);
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

    // --- flowers + gardener, depth sorted by height on screen (far -> near) ---
    {
        const std::vector<gardensim::Flower>& fl = gardensim::flowers();
        const gardensim::Gardener& gd = gardensim::gardener();
        const bool gardenerShown = gd.alpha > 0.01f;
        size_t i = 0;
        for (; i < flowerOrder.size(); i++)
        {
            const gardensim::Flower& f = fl[flowerOrder[i]];
            if (gardenerShown && f.y <= gd.y) break;                     // from here on flowers are in front of him
            if (visible(f.x, f.y + f.h * 0.5f, f.h + f.size)) drawFlower(f, u);
        }
        if (gardenerShown) drawGardener(gd.x, gd.y, 0.46f, gd, 1.0f);
        for (; i < flowerOrder.size(); i++)
        {
            const gardensim::Flower& f = fl[flowerOrder[i]];
            if (visible(f.x, f.y + f.h * 0.5f, f.h + f.size)) drawFlower(f, u);
        }

        if (gd.dragged)                                                  // destination marker + ghost gardener
        {
            gfx::useTint = false;
            glColor4f(1.0f, 1.0f, 1.0f, 0.85f);
            glLineWidth(2.0f);
            ring(gd.targetX, gd.targetY, 0.075f, 0.026f, 28);
            glLineWidth(1.0f);
            const int n = 14;
            glBegin(GL_LINES);
            for (int k = 0; k < n; k += 2)
            {
                const float a0 = (float)k / n, a1 = (float)(k + 1) / n;
                glVertex2f(lerpf(gd.x, gd.targetX, a0), lerpf(gd.y, gd.targetY, a0));
                glVertex2f(lerpf(gd.x, gd.targetX, a1), lerpf(gd.y, gd.targetY, a1));
            }
            glEnd();
            gfx::useTint = true;
            drawGardener(gd.targetX, gd.targetY, 0.46f, gd, 0.45f);
        }
    }

    // --- butterflies: steering simulation (cursor avoidance) ---
    for (const gardensim::Butterfly& b : gardensim::butterflies())
    {
        const float tilt = clampf(b.vx * 40.0f, -28.0f, 28.0f);
        drawButterfly(b.x, b.y, b.size, std::sin(b.flapPhase), b.col, -tilt);
        drawShimmer(b);
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
