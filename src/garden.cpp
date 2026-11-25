#include "garden.h"
#include "gfx.h"
#include "village.h"
#include "theme.h"
#include "gardensim.h"
#include "cgmath.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace gfx;

namespace
{

const float SPAN = 2.0f * (WORLD_W + 0.6f);     // wrap width for clouds / birds (off-screen both ends)

Ridge farMts, nearMts, hill1, hill2, lawn;

struct Blade  { float x, y, h, w, phase; Color col; };
std::vector<int>    flowerOrder;      // flower indices sorted far -> near (for drawing)
std::vector<Blade>  blades;

GLuint staticList = 0;
Color  staticTint{-1, -1, -1, -1};

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

void drawSunRays(float x, float y, float r, float u)
{
    const bool old = gfx::useTint;
    gfx::useTint = false;
    const Color c = theme::sun(rgb(1.0f, 0.86f, 0.25f));
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glRotatef(360.0f * u, 0.0f, 0.0f, 1.0f);                       // one slow turn per loop
    for (int k = 0; k < 14; k++)
    {
        const float a = TAU * k / 14.0f, w = 0.10f, L = r * (k & 1 ? 1.65f : 1.95f);
        setColor(withAlpha(c, c.a * 0.85f));
        tri(std::cos(a - w) * r * 1.05f, std::sin(a - w) * r * 1.05f, std::cos(a + w) * r * 1.05f, std::sin(a + w) * r * 1.05f,
            std::cos(a) * L, std::sin(a) * L);
    }
    glPopMatrix();
    gfx::useTint = old;
}

void drawSkyAndFar(float u)
{
    Stop stops[] = {{-1.2f, rgb(0.92f, 0.97f, 1.0f)}, {0.05f, rgb(0.78f, 0.92f, 1.0f)},
                    {0.55f, rgb(0.42f, 0.72f, 0.98f)}, {1.60f, rgb(0.14f, 0.48f, 0.94f)}};
    for (Stop& st : stops) st.c = theme::skyColor(st.c, st.y);      // time-of-day sky
    gfx::useTint = false;                                           // sky is already final
    vGradient(-3.2f, 3.2f, stops, 4);

    const float sx = -0.20f, sy = 0.76f;
    glow(sx, sy, 0.75f, theme::sun(rgb(1.0f, 0.95f, 0.70f, 0.70f)), 48);
    gfx::useTint = true;
    drawSunRays(sx, sy, 0.095f, u);
    gfx::useTint = false;
    setColor(theme::sun(rgb(1.0f, 0.86f, 0.22f)));
    circle(sx, sy, 0.095f, 40);
    setColor(theme::sun(rgb(1.0f, 0.95f, 0.55f)));
    circle(sx - 0.02f, sy + 0.02f, 0.065f, 32);
    theme::drawStarsAndMoon(sx, sy, 0.095f, u);                     // integer-cycle twinkle => loops
    gfx::useTint = true;

    // clouds: translation, wrapping off-screen => seamless
    drawCloud(loopX(-1.3f, u, 1), 0.80f, 0.62f, 0.0f);
    drawCloud(loopX(0.6f, u, 1), 0.62f, 0.46f, 0.0f);
    drawCloud(loopX(1.6f, u, 2), 0.88f, 0.40f, 0.0f);
    drawCloud(loopX(-0.2f, u, 2), 0.95f, 0.30f, 0.0f, 0.9f);

    // birds: cross the sky several times per loop, wings flap
    for (int i = 0; i < 4; i++)
    {
        const float bx = loopX(-1.8f + i * 0.9f, u, 6.0f + (i & 1));
        const float by = 0.52f + 0.06f * i + 0.04f * wave(u, 3.0f + i, i * 0.21f);
        drawBird(bx, by, 0.028f, wave(u, 150.0f + 10.0f * i, i * 0.3f), rgb(0.14f, 0.18f, 0.28f));
    }
}

// a lush, reference-style tree: trunk with roots and branches, many leaf clusters in 3 greens
void drawLushTree(float x, float y, float h, float sway)
{
    setColor(rgb(0.45f, 0.28f, 0.15f));
    quad(x - 0.09f * h, y, x + 0.09f * h, y, x + 0.045f * h, y + 0.55f * h, x - 0.045f * h, y + 0.55f * h);
    tri(x - 0.20f * h, y - 0.01f, x - 0.05f * h, y, x - 0.05f * h, y + 0.12f * h);     // roots
    tri(x + 0.20f * h, y - 0.01f, x + 0.05f * h, y, x + 0.05f * h, y + 0.12f * h);
    setColor(rgb(0.36f, 0.22f, 0.12f));
    rect(x + 0.01f * h, y + 0.05f * h, x + 0.03f * h, y + 0.45f * h);                  // bark line
    thickLine(x, y + 0.45f * h, x - 0.28f * h, y + 0.70f * h, 0.04f * h);             // branches
    thickLine(x, y + 0.48f * h, x + 0.26f * h, y + 0.74f * h, 0.035f * h);
    const float cx = x + sway * 0.02f * h, cy = y + 0.86f * h;
    static const float C[][3] = {{-0.38f, -0.08f, 0.22f}, {0.38f, -0.06f, 0.22f}, {-0.20f, 0.10f, 0.26f}, {0.20f, 0.12f, 0.26f},
                                 {0.0f, 0.26f, 0.26f}, {-0.30f, 0.28f, 0.18f}, {0.30f, 0.30f, 0.18f}, {0.0f, -0.10f, 0.24f},
                                 {-0.52f, 0.10f, 0.15f}, {0.52f, 0.12f, 0.15f}};
    const Color shades[3] = {rgb(0.14f, 0.44f, 0.18f), rgb(0.30f, 0.62f, 0.20f), rgb(0.50f, 0.80f, 0.26f)};
    for (int layer = 0; layer < 3; layer++)
    {
        setColor(shades[layer]);
        for (int k = 0; k < 10; k++)
        {
            const float* c = C[k];
            const float off = layer * 0.035f, sc = 1.0f - layer * 0.25f;
            circle(cx + (c[0] - off * 0.6f) * h, cy + (c[1] + off) * h, c[2] * sc * h, 22);
        }
    }
    setColor(rgb(0.62f, 0.90f, 0.32f, 0.8f));                         // sun-lit leaf tips
    for (int k = 0; k < 14; k++)
        circle(cx + (cg::hashU(k * 3u) - 0.6f) * 0.8f * h, cy + (0.05f + 0.35f * cg::hashU(k * 7u)) * h, 0.03f * h, 10);
}

// one decorative flower (borders): kind 0 daisy, 1 rose, 2 tulip, 3 sunflower, 4 lavender spike
void decoFlower(float x, float y, float s, int kind, const Color& c, float sway)
{
    const float hx = x + sway * s * 0.4f, hy = y + s * 2.2f;
    setColor(rgb(0.22f, 0.55f, 0.22f));
    thickLine(x, y, hx, hy, s * 0.14f);
    setColor(rgb(0.30f, 0.66f, 0.26f));
    ellipse(x - s * 0.35f, y + s * 0.8f, s * 0.38f, s * 0.14f, 10);
    ellipse(x + s * 0.35f, y + s * 1.1f, s * 0.38f, s * 0.14f, 10);
    switch (kind)
    {
    case 0:                                                             // daisy
        setColor(c);
        for (int k = 0; k < 10; k++) { const float a = TAU * k / 10.0f; ellipse(hx + std::cos(a) * s * 0.55f, hy + std::sin(a) * s * 0.55f, s * 0.32f, s * 0.32f, 10); }
        setColor(rgb(1.0f, 0.78f, 0.15f)); circle(hx, hy, s * 0.35f, 14);
        break;
    case 1:                                                             // rose
        setColor(shade(c, 0.80f)); circle(hx, hy, s * 0.62f, 16);
        setColor(c); circle(hx - s * 0.08f, hy + s * 0.08f, s * 0.48f, 16);
        setColor(shade(c, 0.75f)); circle(hx, hy + s * 0.05f, s * 0.22f, 12);
        break;
    case 2:                                                             // tulip
        setColor(c);
        ellipse(hx, hy, s * 0.42f, s * 0.55f, 14);
        tri(hx - s * 0.42f, hy, hx - s * 0.10f, hy, hx - s * 0.30f, hy + s * 0.78f);
        tri(hx + s * 0.42f, hy, hx + s * 0.10f, hy, hx + s * 0.30f, hy + s * 0.78f);
        break;
    case 3:                                                             // sunflower
        setColor(rgb(1.0f, 0.80f, 0.10f));
        for (int k = 0; k < 14; k++) { const float a = TAU * k / 14.0f; ellipse(hx + std::cos(a) * s * 0.75f, hy + std::sin(a) * s * 0.75f, s * 0.30f, s * 0.30f, 8); }
        setColor(rgb(0.45f, 0.28f, 0.10f)); circle(hx, hy, s * 0.55f, 18);
        break;
    default:                                                            // lavender / lupin spike
        for (int k = 0; k < 7; k++)
        {
            setColor(k & 1 ? c : shade(c, 0.85f));
            ellipse(x + sway * s * 0.4f * (k / 7.0f), y + s * (1.2f + 0.35f * k), s * (0.32f - 0.025f * k), s * 0.22f, 10);
        }
        break;
    }
}

// a bush covered in blossoms (lush foreground beds)
void blossomBush(float x, float y, float r, uint32_t seed)
{
    drawBush(x, y, r, rgb(0.12f, 0.42f, 0.18f), rgb(0.26f, 0.62f, 0.26f));
    static const Color B[] = {rgb(1.0f, 0.40f, 0.62f), rgb(0.95f, 0.20f, 0.25f), rgb(1.0f, 0.96f, 0.98f), rgb(1.0f, 0.80f, 0.20f), rgb(0.68f, 0.45f, 0.95f)};
    const Color c = B[seed % 5];
    for (int k = 0; k < 14; k++)
    {
        const float a = PI * cg::hashU(seed * 31u + k * 7u), d = r * (0.4f + 0.9f * cg::hashU(seed + k * 13u));
        const float bx = x + std::cos(a) * d * 1.3f, by = y + r * 0.5f + std::sin(a) * d;
        setColor(c); circle(bx, by, r * 0.16f, 10);
        setColor(rgb(1.0f, 0.90f, 0.40f)); circle(bx, by, r * 0.06f, 6);
    }
}

const Color DECO[] = {rgb(1.0f, 0.42f, 0.62f), rgb(0.95f, 0.18f, 0.22f), rgb(1.0f, 0.95f, 0.98f), rgb(0.62f, 0.42f, 0.95f),
                      rgb(1.0f, 0.62f, 0.20f), rgb(0.98f, 0.55f, 0.80f)};

void flowerCluster(float x, float y, float spread, float s, int n, uint32_t seed, float u, bool animate)
{
    for (int k = 0; k < n; k++)
    {
        const float fx = x + (cg::hashU(seed + k * 13u) - 0.5f) * spread;
        const float fy = y + (cg::hashU(seed + k * 7u) - 0.5f) * spread * 0.18f;
        const int kind = (int)(cg::hashU(seed + k * 3u) * 5.0f) % 5;
        const Color c = DECO[(int)(cg::hashU(seed + k * 19u) * 6.0f) % 6];
        const float sw = animate ? wave(u, 8.0f + (k % 3), cg::hashU(seed + k)) : 0.0f;
        decoFlower(fx, fy, s * (0.8f + 0.4f * cg::hashU(seed + k * 5u)), kind, kind == 4 ? rgb(0.55f, 0.40f, 0.92f) : c, sw);
    }
}

// wooden arch bridge from the garden (near end) to the far bank, seen from above at an angle.
// Deck = the arc gardensim::bridgeAt(u); its width grows toward the viewer (perspective).
cg::V2 bridgeSide(float u, float side)
{
    const cg::V2 c = gardensim::bridgeAt(u);
    const float w = 0.035f + 0.030f * (1.0f - u);                     // wider near the viewer
    return cg::V2(c.x + side * w * 0.80f, c.y + side * w * 0.55f);
}
void drawBridge(int part)                       // part 0: far rail + deck, part 1: near rail
{
    const int N = 30;
    auto rail = [&](float side, const Color& c)
    {
        for (int i = 0; i <= 8; i++)
        {
            const cg::V2 p = bridgeSide(i / 8.0f, side);
            const float h = 0.050f + 0.020f * (1.0f - i / 8.0f);
            setColor(shade(c, 0.85f)); rect(p.x - 0.007f, p.y, p.x + 0.007f, p.y + h);
            setColor(shade(c, 1.15f)); circle(p.x, p.y + h, 0.009f, 8);
        }
        setColor(c);
        glBegin(GL_TRIANGLE_STRIP);
        for (int i = 0; i <= N; i++)
        {
            const float u = (float)i / N; const cg::V2 p = bridgeSide(u, side);
            const float h = 0.050f + 0.020f * (1.0f - u);
            glVertex2f(p.x, p.y + h); glVertex2f(p.x, p.y + h - 0.012f);
        }
        glEnd();
    };
    if (part == 0)
    {
        rail(+1.0f, rgb(0.62f, 0.40f, 0.22f));                          // far rail (behind the deck)
        setColor(rgb(0.40f, 0.25f, 0.13f));                             // deck side (thickness)
        glBegin(GL_TRIANGLE_STRIP);
        for (int i = 0; i <= N; i++) { const cg::V2 p = bridgeSide((float)i / N, -1.0f); glVertex2f(p.x, p.y); glVertex2f(p.x, p.y - 0.025f); }
        glEnd();
        glBegin(GL_QUAD_STRIP);                                          // deck
        for (int i = 0; i <= N; i++)
        {
            const float u = (float)i / N; const cg::V2 a = bridgeSide(u, -1.0f), b = bridgeSide(u, 1.0f);
            const Color c = (i & 1) ? rgb(0.78f, 0.55f, 0.30f) : rgb(0.70f, 0.48f, 0.26f);
            vtx(c, a.x, a.y); vtx(c, b.x, b.y);
        }
        glEnd();
        setColor(rgb(0.48f, 0.30f, 0.16f, 0.7f));                       // plank gaps
        glBegin(GL_LINES);
        for (int i = 1; i < N; i++) { const cg::V2 a = bridgeSide((float)i / N, -1.0f), b = bridgeSide((float)i / N, 1.0f); glVertex2f(a.x, a.y); glVertex2f(b.x, b.y); }
        glEnd();
    }
    else rail(-1.0f, rgb(0.70f, 0.46f, 0.24f));                        // near rail (in front)
}

// everything that never moves is compiled once into a display list (rebuilt when the tint changes)
void buildStatic()
{
    drawRidge(farMts, -0.3f, rgb(0.60f, 0.74f, 0.96f), rgb(0.80f, 0.90f, 1.0f));
    drawRidgeShadows(farMts, rgb(0.46f, 0.60f, 0.88f, 0.50f));
    drawSnow(farMts, 0.33f, rgb(1.0f, 1.0f, 1.0f), rgb(0.88f, 0.92f, 1.0f));
    drawRidge(nearMts, -0.3f, rgb(0.34f, 0.56f, 0.90f), rgb(0.52f, 0.74f, 0.92f));
    drawRidgeShadows(nearMts, rgb(0.20f, 0.36f, 0.72f, 0.55f));
    drawRidgeLights(nearMts, rgb(0.55f, 0.74f, 0.98f, 0.45f));
    drawSnow(nearMts, 0.27f, rgb(1.0f, 1.0f, 1.0f), rgb(0.85f, 0.90f, 1.0f));
    drawRidge(hill1, -0.4f, rgb(0.40f, 0.74f, 0.40f), rgb(0.56f, 0.86f, 0.46f));
}

}   // namespace (art helpers)
namespace
{

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
    std::vector<PeakDef> far = {{-1.3f, 0.30f, 0.45f}, {-0.55f, 0.36f, 0.45f}, {0.25f, 0.30f, 0.40f}, {0.95f, 0.38f, 0.45f}, {1.75f, 0.30f, 0.45f}};
    std::vector<PeakDef> near = {{-0.95f, 0.22f, 0.40f}, {-0.15f, 0.26f, 0.42f}, {0.65f, 0.20f, 0.38f}, {1.45f, 0.24f, 0.42f}};
    farMts  = makePeakRidge(-2.8f, 2.8f, 0.01f, 0.06f, far, 2.1f, 0.03f, false);
    nearMts = makePeakRidge(-2.8f, 2.8f, 0.01f, 0.00f, near, 4.7f, 0.025f, false);
    hill1  = makeRolling(-2.8f, 2.8f, 0.02f, 0.02f, 0.10f, 0.7f, 2.0f);
    hill2  = makeRolling(-2.8f, 2.8f, 0.02f, -0.12f, 0.08f, 3.4f, 2.7f);
    lawn   = makeRolling(-2.8f, 2.8f, 0.02f, -0.20f, 0.025f, 1.9f, 3.0f);


    gardensim::init();                                               // flowers, rows, gardener, butterflies
    gardensim::setWindmillHub(0.30f, ridgeAt(hill1, 0.30f) - 0.01f + 0.36f);
    flowerOrder.clear();
    for (size_t i = 0; i < gardensim::flowers().size(); i++) flowerOrder.push_back((int)i);
    std::stable_sort(flowerOrder.begin(), flowerOrder.end(),
                     [](int a, int b) { return gardensim::flowers()[a].y > gardensim::flowers()[b].y; });   // far first

    blades.clear();
    for (int i = 0; i < 220; i++)
    {
        Blade b;
        b.x     = lerpf(-WORLD_W - 0.1f, WORLD_W + 0.1f, hash1(i * 3.7f));
        b.y     = lerpf(-1.04f, -0.92f, hash1(i * 8.1f));
        b.h     = 0.06f + 0.09f * hash1(i * 5.5f);
        b.w     = 0.012f + 0.008f * hash1(i * 1.3f);
        b.phase = hash1(i * 2.2f);
        b.col   = mix(rgb(0.22f, 0.58f, 0.18f), rgb(0.50f, 0.86f, 0.28f), hash1(i * 6.6f));
        blades.push_back(b);
    }
}

void drawGarden(const AnimState& st)
{
    const float u = (float)(std::fmod(st.gardenTime, GARDEN_LOOP_SECONDS) / GARDEN_LOOP_SECONDS);
    const float T = (float)st.gardenTime;

    // while fading in, the garden settles from a slight zoom to rest (eased)
    const float settle = 1.0f + 0.30f * (1.0f - smootherstep(st.gardenBlend));
    pushCamera(settle, 0.0f, -0.10f, 0.0f, 0.0f);

    drawSkyAndFar(u);

    // ---- static background (display list, rebuilt only when the time-of-day tint changes) ----
    {
        const Color t = gfx::tintColor;
        if (!staticList || std::fabs(t.r - staticTint.r) + std::fabs(t.g - staticTint.g) + std::fabs(t.b - staticTint.b) > 0.004f)
        {
            if (!staticList) staticList = glGenLists(1);
            glNewList(staticList, GL_COMPILE);
            buildStatic();
            glEndList();
            staticTint = t;
        }
        glCallList(staticList);
    }

    drawWindmill(0.30f, ridgeAt(hill1, 0.30f) - 0.01f, gardensim::windAngle());
    drawRidge(hill2, -0.5f, rgb(0.32f, 0.68f, 0.34f), rgb(0.48f, 0.82f, 0.40f));
    for (int i = 0; i < 16; i++)                                     // distant tree line
    {
        const float x = -1.0f + i * 0.20f + 0.06f * hash1(i * 3.0f);
        const float h = 0.14f + 0.08f * hash1(i * 7.0f);
        if (visible(x, ridgeAt(hill2, x) + h * 0.6f, h * 0.6f))
            drawRoundTree(x, ridgeAt(hill2, x) - 0.015f, h, wave(u, 10.0f, hash1(i * 1.7f)),
                          rgb(0.16f, 0.50f, 0.24f), rgb(0.34f, 0.72f, 0.30f));
    }

    // ---- lawn ----
    drawRidge(lawn, -1.3f, rgb(0.50f, 0.84f, 0.36f), rgb(0.30f, 0.66f, 0.24f));
    setColor(rgb(0.70f, 0.94f, 0.46f, 0.35f));
    ellipse(0.35f, -0.55f, 1.2f, 0.30f, 48);                         // sunlit patch

    // ---- stepping-stone path from the cottage door to the viewer (Catmull-Rom spine) ----
    {
        static const float PX[] = {1.10f, 1.08f, 0.92f, 0.50f, 0.22f, 0.20f, 0.26f};
        static const float PY[] = {-0.30f, -0.32f, -0.40f, -0.58f, -0.80f, -1.05f, -1.30f};
        for (int seg = 0; seg < 5; seg++)
            for (int k = 0; k < 4; k++)
            {
                const float t = (k + 0.5f) / 4.0f;
                const float cx = catmull(PX[seg], PX[seg + 1], PX[seg + 2], PX[std::min(seg + 3, 6)], t);
                const float cy = catmull(PY[seg], PY[seg + 1], PY[seg + 2], PY[std::min(seg + 3, 6)], t);
                const float depth = clampf(-cy, 0.2f, 1.2f);
                const float side = ((seg * 4 + k) & 1 ? 1.0f : -1.0f) * 0.035f * depth;
                const float rx = 0.040f + 0.065f * depth, ry = 0.012f + 0.020f * depth;
                setColor(rgb(0.55f, 0.48f, 0.38f));
                ellipse(cx + side, cy - 0.006f, rx * 1.05f, ry * 1.1f, 24);
                setColor(rgb(0.92f, 0.80f, 0.60f));
                ellipse(cx + side, cy, rx, ry, 24);
                setColor(rgb(1.0f, 0.92f, 0.76f, 0.6f));
                ellipse(cx + side - rx * 0.2f, cy + ry * 0.3f, rx * 0.5f, ry * 0.4f, 16);
            }
    }

    // branch path (pink arrow in the brief): from the main path to the bridge foot
    {
        const cg::V2 P[] = {cg::V2(0.10f, -0.80f), cg::V2(-0.04f, -0.795f), cg::V2(-0.18f, -0.80f), cg::V2(-0.31f, -0.815f), cg::V2(-0.43f, -0.84f)};
        for (int k = 0; k < 5; k++)
        {
            const float depth = clampf(-P[k].y, 0.2f, 1.2f), rx = 0.040f + 0.050f * depth, ry = 0.012f + 0.016f * depth;
            setColor(rgb(0.55f, 0.48f, 0.38f)); ellipse(P[k].x, P[k].y - 0.006f, rx * 1.05f, ry * 1.1f, 24);
            setColor(rgb(0.92f, 0.80f, 0.60f)); ellipse(P[k].x, P[k].y, rx, ry, 24);
            setColor(rgb(1.0f, 0.92f, 0.76f, 0.6f)); ellipse(P[k].x - rx * 0.2f, P[k].y + ry * 0.3f, rx * 0.5f, ry * 0.4f, 16);
        }
    }

    // ---- cottage (the same house style as the village) + pots, roses, pine ----
    drawPine(1.72f, -0.24f, 0.95f, wave(u, 5.0f, 0.2f) * 0.02f, 1.0f);
    drawHouse(1.12f, -0.17f, 0.64f, 0, true, GARDEN_LOOP_SECONDS * u);
    // permanent decorative flowers (never wither): climbing roses along both roof slopes,
    // hanging baskets under the eaves, a flower border along the house wall
    {
        const float hx = 1.12f, hy = -0.17f, k = 0.64f;
        for (int i = 0; i < 9; i++)
        {
            const float t2 = (i + 0.5f) / 9.0f;
            for (int side = -1; side <= 1; side += 2)
            {
                const float rx = hx + side * 0.64f * k * (1.0f - t2) , ry = hy + (0.69f + 0.60f * t2) * k;
                if (t2 > 0.80f) continue;
                setColor(rgb(0.20f, 0.50f, 0.22f)); ellipse(rx, ry - 0.006f, 0.030f, 0.020f, 12);
                setColor(rgb(0.30f, 0.62f, 0.28f)); ellipse(rx - side * 0.012f, ry + 0.004f, 0.014f, 0.009f, 8);
                decoFlower(rx, ry, 0.017f, 1, DECO[(i + (side > 0 ? 2 : 0)) % 6], wave(u, 6.0f, i * 0.1f) * 0.3f);
                decoFlower(rx + side * 0.02f, ry - 0.012f, 0.012f, 0, DECO[(i + 3) % 6], 0.0f);
            }
        }
        for (int side = -1; side <= 1; side += 2)                        // hanging baskets
        {
            const float bx = hx + side * 0.36f, by = hy + 0.60f * k;
            setColor(rgb(0.35f, 0.25f, 0.18f)); thickLine(bx, by + 0.06f, bx, by + 0.025f, 0.004f);
            setColor(rgb(0.55f, 0.36f, 0.20f)); ellipse(bx, by, 0.035f, 0.022f, 14);
            flowerCluster(bx, by + 0.012f, 0.07f, 0.010f, 5, 140u + side, u, true);
            for (int d = 0; d < 4; d++) { setColor(DECO[(d + 1) % 6]); circle(bx - 0.025f + d * 0.017f, by - 0.02f - 0.012f * (d & 1), 0.006f, 6); }
        }
        flowerCluster(hx - 0.22f, hy + 0.035f, 0.18f, 0.011f, 7, 150u, u, true);    // border along the wall
        flowerCluster(hx + 0.22f, hy + 0.035f, 0.18f, 0.011f, 7, 151u, u, true);
    }
    for (int side = -1; side <= 1; side += 2)                        // flower pots beside the door
    {
        const float px = 1.12f + side * 0.15f, py = -0.185f;
        setColor(rgb(0.80f, 0.40f, 0.22f)); quad(px - 0.035f, py, px + 0.035f, py, px + 0.045f, py + 0.06f, px - 0.045f, py + 0.06f);
        flowerCluster(px, py + 0.055f, 0.06f, 0.010f, 4, 50u + side, u, true);
    }
    flowerCluster(0.78f, -0.21f, 0.30f, 0.013f, 9, 61u, u, true);   // beds hugging the cottage
    flowerCluster(1.50f, -0.21f, 0.26f, 0.013f, 8, 67u, u, true);
    // door stone: drop the gardener HERE to send him inside (it glows while you carry him)
    {
        const bool carrying = gardensim::gardener().dragged;
        setColor(rgb(0.55f, 0.50f, 0.44f)); ellipse(gardensim::STONE_X, gardensim::STONE_Y - 0.006f, 0.088f, 0.026f, 24);
        setColor(rgb(0.96f, 0.86f, 0.66f)); ellipse(gardensim::STONE_X, gardensim::STONE_Y, 0.083f, 0.022f, 24);
        if (carrying) { gfx::useTint = false; setColor(rgb(1.0f, 0.95f, 0.55f, 0.45f + 0.25f * std::sin(T * 6.0f))); ellipse(gardensim::STONE_X, gardensim::STONE_Y, 0.10f, 0.03f, 24); gfx::useTint = true; }
    }

    // ---- fence with a gate in front of the door, big tree, bushes ----
    {
        const Color fc = rgb(0.86f, 0.62f, 0.38f);
        const float fy = gardensim::FENCE_Y, fh = 0.12f;
        drawFence(-1.95f, gardensim::GATE_X0, fy, fh, fc);
        drawFence(gardensim::GATE_X1, 1.95f, fy, fh, fc);
        // gate: two leaves on hinges; opening = the leaf turns away (its width shrinks like a
        // rotation about a vertical axis: w' = w cos(theta))
        const float open = gardensim::gateOpen(), th = open * 1.35f, half = 0.5f * (gardensim::GATE_X1 - gardensim::GATE_X0);
        for (int side = -1; side <= 1; side += 2)
        {
            const float hinge = side < 0 ? gardensim::GATE_X0 : gardensim::GATE_X1;
            const float w = half * std::cos(th) * (side < 0 ? 1.0f : -1.0f), lift = 0.02f * std::sin(th);
            setColor(rgb(0.50f, 0.32f, 0.18f)); rect(hinge - 0.012f, fy - 0.01f, hinge + 0.012f, fy + fh + 0.03f);     // posts
            setColor(shade(fc, 1.05f));
            for (int b2 = 0; b2 < 3; b2++) { const float f0 = (b2 + 0.2f) / 3.0f, f1 = (b2 + 0.8f) / 3.0f; quad(hinge + w * f0, fy - lift * f0, hinge + w * f1, fy - lift * f1, hinge + w * f1, fy + fh - 0.01f - lift * f1, hinge + w * f0, fy + fh - 0.01f - lift * f0); }
            setColor(shade(fc, 0.8f)); quad(hinge, fy + 0.025f, hinge + w, fy + 0.025f - lift, hinge + w, fy + 0.045f - lift, hinge, fy + 0.045f);
            quad(hinge, fy + fh - 0.045f, hinge + w, fy + fh - 0.045f - lift, hinge + w, fy + fh - 0.025f - lift, hinge, fy + fh - 0.025f);
        }
        // climbing roses along the fence (permanent)
        for (int k = 0; k < 22; k++)
        {
            const float x = -1.85f + k * 0.17f + 0.04f * hash1(k * 2.3f);
            if (x > gardensim::GATE_X0 - 0.04f && x < gardensim::GATE_X1 + 0.04f) continue;
            setColor(rgb(0.22f, 0.52f, 0.22f)); circle(x, fy + fh * 0.75f, 0.020f, 10);
            decoFlower(x, fy + fh * 0.8f, 0.010f, 1, DECO[k % 6], 0.0f);
        }
    }
    flowerCluster(-0.10f, -0.25f, 0.80f, 0.010f, 12, 71u, u, true);
    blossomBush(0.62f, -0.28f, 0.075f, 4u);
    blossomBush(1.58f, -0.255f, 0.06f, 5u);
    blossomBush(-1.62f, -0.265f, 0.06f, 6u);
    drawLushTree(-0.50f, -0.23f, 0.82f, wave(u, 6.0f, 0.1f));

    // ---- the lake: runs off the left and bottom edges; stencil-masked reflections; ripples;
    //      a cove with lilies where the can is filled; shore stones; the bridge to the far bank ----
    {
        const std::vector<cg::V2>& sh = gardensim::lakeShore();
        const cg::V2 anchor(-2.2f, -1.25f);                          // the shore is star-shaped from here
        auto fan = [&](float grow, const Color& c0, const Color& c1)
        {
            glBegin(GL_TRIANGLE_FAN);
            vtx(c1, anchor.x, anchor.y);
            for (size_t i = sh.size(); i-- > 0;)
            {
                const cg::V2 d = sh[i] - anchor; const float l = cg::len(d);
                const cg::V2 p = anchor + d * ((l + grow) / l);
                vtx(mix(c1, c0, clampf((p.y + 1.0f) / 0.65f, 0.0f, 1.0f)), p.x, p.y);
            }
            glEnd();
        };
        fan(0.075f, rgb(0.62f, 0.66f, 0.42f), rgb(0.55f, 0.60f, 0.40f));   // muddy bank
        fan(0.045f, rgb(0.86f, 0.80f, 0.62f), rgb(0.80f, 0.74f, 0.58f));   // sandy shore

        // 1) the water body also writes 1 into the STENCIL buffer
        glClear(GL_STENCIL_BUFFER_BIT);
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        fan(0.0f, rgb(0.52f, 0.82f, 0.98f), rgb(0.16f, 0.46f, 0.78f));
        // 2) planar reflection of the far bank (fence, tree, bridge) about the far shore line
        //    (homogeneous reflection matrix), drawn ONLY where the stencil is 1 (inside the water)
        glStencilFunc(GL_EQUAL, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        {
            float m[16];
            cg::Mat3::reflectY(-0.37f).toGL(m);
            glPushMatrix();
            glMultMatrixf(m);
            const float oldA = gfx::alphaMul;
            gfx::alphaMul = 0.16f;
            drawLushTree(-0.50f, -0.23f, 0.82f, wave(u, 6.0f, 0.1f));
            gfx::alphaMul = oldA;
            glPopMatrix();
            gfx::alphaMul = 0.20f;                                      // the bridge mirrored about its own water line
            glPushMatrix();
            const cg::V2 a0(gardensim::BRIDGE_X0, gardensim::BRIDGE_Y0), a1(gardensim::BRIDGE_X1, gardensim::BRIDGE_Y1);
            // water is horizontal, so each deck point is mirrored VERTICALLY about the water line
            // under it (the bridge's chord y = a x + b):  y' = 2(a x + b) - y  (an affine skew reflection)
            const float ka = (a1.y - a0.y) / (a1.x - a0.x), kb = a0.y - ka * a0.x;
            cg::Mat3 R;
            R.m[3] = 2.0f * ka; R.m[4] = -1.0f; R.m[5] = 2.0f * kb;
            R.toGL(m);
            glMultMatrixf(m);
            drawBridge(0);
            glPopMatrix();
            gfx::alphaMul = oldA;
        }
        // soft depth gradient + sky glints
        setColor(rgb(0.80f, 0.94f, 1.0f, 0.30f));
        ellipse(-1.10f, -0.50f, 0.50f, 0.05f, 40);
        for (int k = 0; k < 18; k++)
        {
            const float gx = -1.75f + 1.35f * hash1(k * 3.3f), gy = -0.42f - 0.55f * hash1(k * 7.7f);
            if (!gardensim::inLake(gx, gy)) continue;
            const float tw = 0.5f + 0.5f * std::sin(T * 2.2f + k * 1.7f);
            setColor(rgb(1, 1, 1, 0.55f * tw));
            rect(gx - 0.03f * tw, gy - 0.002f, gx + 0.03f * tw, gy + 0.002f);
        }
        // gentle current lines drifting out to the open water (left / down)
        for (int k = 0; k < 12; k++)
        {
            const float ph = fract(T * 0.05f + k / 12.0f);
            const float gx = -0.55f - 1.3f * ph - 0.1f * hash1(k * 9.1f), gy = -0.45f - 0.45f * hash1(k * 4.4f) - 0.15f * ph;
            setColor(rgb(1, 1, 1, 0.30f * std::sin(ph * PI)));
            rect(gx - 0.05f, gy - 0.0015f, gx + 0.05f, gy + 0.0015f);
        }
        // ripples appear where the user clicked the water (simulation: gardensim::ripples)
        glLineWidth(2.0f);
        for (const gardensim::Ripple& r : gardensim::ripples())
        {
            const float age = r.age / gardensim::RIPPLE_LIFE;
            for (int k = 0; k < 3; k++)
            {
                const float p = age - 0.10f * k;
                if (p <= 0.0f || p >= 1.0f) continue;
                const float rr = 0.03f + 0.26f * p;
                glColor4f(1.0f, 1.0f, 1.0f, 0.85f * (1.0f - p) * (1.0f - p * 0.3f));
                ring(r.x, r.y, rr, rr * 0.32f, 56);                     // the stencil clips it to the water
            }
        }
        glLineWidth(1.0f);
        // the can dips into the cove: small rings while he fills it
        if (gardensim::gardener().filling > 0.3f)
            for (int k = 0; k < 3; k++)
            {
                const float p = fract(T * 1.2f + k / 3.0f), rr = 0.02f + 0.08f * p;
                setColor(rgb(1, 1, 1, 0.7f * (1.0f - p)));
                ring(gardensim::FILL_X - 0.13f, gardensim::FILL_Y - 0.01f, rr, rr * 0.32f, 32);
            }
        glDisable(GL_STENCIL_TEST);

        // lily pads + lotus flowers in the cove and along the shore
        const float LP[][2] = {{-0.40f, -0.66f}, {-0.36f, -0.71f}, {-0.48f, -0.73f}, {-0.70f, -0.48f}, {-1.05f, -0.45f}, {-1.45f, -0.62f}, {-0.98f, -0.78f}};
        for (int k = 0; k < 7; k++)
        {
            const float lx = LP[k][0], ly = LP[k][1] + 0.006f * wave(u, 15.0f, k * 0.3f), s2 = 0.8f + 0.5f * (-ly - 0.4f);
            setColor(rgb(0.20f, 0.62f, 0.30f));
            ellipse(lx, ly, 0.050f * s2, 0.017f * s2, 20);
            setColor(rgb(0.40f, 0.76f, 0.94f));
            tri(lx, ly, lx + 0.05f * s2, ly + 0.006f, lx + 0.05f * s2, ly - 0.006f);
            if (k % 2 == 0)
            {
                setColor(rgb(1.0f, 0.62f, 0.80f));
                for (int p2 = -2; p2 <= 2; p2++) ellipse(lx + p2 * 0.008f * s2, ly + 0.018f * s2 - std::abs(p2) * 0.004f, 0.009f * s2, 0.019f * s2, 10);
                setColor(rgb(1.0f, 0.85f, 0.35f));
                circle(lx, ly + 0.012f * s2, 0.007f * s2, 8);
            }
        }
        // ducks paddling on the open water
        for (int k = 0; k < 3; k++)
        {
            const float dx = -1.20f + 0.08f * k + 0.25f * std::sin(T * 0.12f + 0.4f), dy = -0.58f - 0.03f * k;
            setColor(rgb(1, 1, 1)); ellipse(dx, dy, 0.024f, 0.012f, 12); circle(dx - 0.018f, dy + 0.014f, 0.009f, 10);
            setColor(rgb(1.0f, 0.6f, 0.1f)); tri(dx - 0.026f, dy + 0.016f, dx - 0.036f, dy + 0.013f, dx - 0.026f, dy + 0.011f);
            setColor(rgb(1, 1, 1, 0.4f)); ring(dx + 0.01f, dy - 0.005f, 0.04f, 0.010f, 20);
        }
        // shore stones: placed along the shore by ARC LENGTH with a minimum spacing, so they
        // never overlap; none where the path, the bridge feet or the filling spot meet the water
        {
            float acc = 0.0f; int k = 0;
            for (size_t i = 1; i < sh.size(); i++)
            {
                const cg::V2 a = sh[i - 1], d = sh[i] - a; const float L = cg::len(d);
                for (float sdist = std::fmod(0.11f - acc, 0.11f); sdist < L; sdist += 0.11f, k++)
                {
                    const cg::V2 p = a + d * (sdist / L);
                    if (p.x < -1.85f || p.y < -1.02f) continue;
                    if (std::hypot(p.x - gardensim::FILL_X, p.y - gardensim::FILL_Y) < 0.14f) continue;
                    if (std::hypot(p.x - gardensim::BRIDGE_X0, p.y - gardensim::BRIDGE_Y0) < 0.14f) continue;
                    if (std::hypot(p.x - gardensim::BRIDGE_X1, p.y - gardensim::BRIDGE_Y1) < 0.14f) continue;
                    const float s2 = 0.75f + 0.45f * (-p.y - 0.35f);
                    setColor(shade(rgb(0.72f, 0.70f, 0.66f), 0.80f + 0.25f * hash1(k * 5.0f)));
                    ellipse(p.x, p.y + 0.004f, 0.034f * s2, 0.016f * s2, 14);
                    setColor(rgb(1, 1, 1, 0.25f)); ellipse(p.x - 0.008f * s2, p.y + 0.010f, 0.014f * s2, 0.006f * s2, 10);
                }
                acc = std::fmod(acc + L, 0.11f);
            }
        }
        // reeds at the cove (next to where the gardener fills the can)
        for (int k = 0; k < 8; k++)
        {
            const float bx = -0.33f + k * 0.018f, by = -0.585f - 0.008f * (k & 1), h = 0.16f + 0.08f * hash1(k * 4.0f);
            const float sw = wave(u, 10.0f, k * 0.17f) * 0.025f;
            setColor(rgb(0.30f, 0.60f, 0.26f));
            glBegin(GL_TRIANGLE_STRIP);
            for (int s2 = 0; s2 <= 4; s2++)
            {
                const float v = s2 / 4.0f;
                glVertex2f(bx - 0.004f + sw * v * v, by + h * v);
                glVertex2f(bx + 0.004f + sw * v * v, by + h * v);
            }
            glEnd();
            if (k % 2 == 0) { setColor(rgb(0.42f, 0.26f, 0.14f)); ellipse(bx + sw, by + h + 0.015f, 0.008f, 0.026f, 8); }
        }
        // the bridge: far rail + deck now, the near rail after the gardener (if he stands on it)
        drawBridge(0);
        // flower pots at both bridge feet + flowers along the far bank (permanent decoration)
        flowerCluster(gardensim::BRIDGE_X0 + 0.10f, gardensim::BRIDGE_Y0 - 0.01f, 0.10f, 0.016f, 5, 160u, u, true);
        flowerCluster(gardensim::BRIDGE_X1 + 0.10f, gardensim::BRIDGE_Y1 + 0.01f, 0.08f, 0.010f, 4, 161u, u, true);
        flowerCluster(-1.30f, -0.255f, 0.9f, 0.009f, 12, 162u, u, true);
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
        if (gardenerShown)
        {
            if (gd.dragged)                                              // carried: lifted, with a shadow where he will land
            {
                setColor(rgb(0, 0, 0, 0.22f)); ellipse(gd.x, gd.y - 0.03f, 0.07f, 0.02f, 20);
                drawGardener(gd.x, gd.y + 0.02f, 0.46f, gd, 1.0f);
            }
            else drawGardener(gd.x, gd.y, 0.46f, gd, 1.0f);
        }
        for (; i < flowerOrder.size(); i++)
        {
            const gardensim::Flower& f = fl[flowerOrder[i]];
            if (visible(f.x, f.y + f.h * 0.5f, f.h + f.size)) drawFlower(f, u);
        }

        drawBridge(1);                                                   // near rail of the bridge
        if (false)
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
        const float tilt = clampf(b.vx * 30.0f, -24.0f, 24.0f);
        drawButterfly(b.x, b.y, b.size, std::sin(b.flapPhase), b.col, -tilt);
        drawShimmer(b);
    }

    // --- foreground: big border flowers + grass (travelling sine wave sways every blade) ---
    for (int k = 0; k < 6; k++) blossomBush(0.80f + k * 0.19f, -1.06f + 0.02f * (k & 1), 0.08f, 120u + k);
    flowerCluster(-0.20f, -1.00f, 0.50f, 0.022f, 8, 81u, u, true);
    flowerCluster(1.25f, -0.99f, 0.95f, 0.024f, 14, 91u, u, true);
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

    // drifting petals (stateless particles, whole loop periodic)
    for (int k = 0; k < 14; k++)
    {
        const float ph = fract(u * (3.0f + (k % 3)) + cg::hashU(k * 7u));
        const float x = -1.9f + 3.8f * fract(cg::hashU(k * 3u) + ph * 0.8f), y = 0.55f - 1.5f * ph + 0.05f * std::sin(ph * 20.0f + k);
        setColor(withAlpha(DECO[k % 6], 0.85f));
        glPushMatrix(); glTranslatef(x, y, 0); glRotatef(ph * 720.0f + k * 40.0f, 0, 0, 1);
        ellipse(0.0f, 0.0f, 0.010f, 0.005f, 8);
        glPopMatrix();
    }

    popCamera();
}
