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

// waterfall from the mountain into the pond + the stream above it (cubic Bezier ribbons)
struct Ribbon { cg::V2 p0, p1, p2, p3; float w0, w1; cg::ArcLength arc; };
Ribbon fall_, upperStream_;
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

cg::V2 ribbonAt(const Ribbon& r, float u) { return cg::cubicBezier(r.p0, r.p1, r.p2, r.p3, u); }

// water ribbon with streaks that flow along the arc length (constant speed)
void drawWater(const Ribbon& r, float time, float speed, float alpha)
{
    const int N = 40;
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= N; i++)
    {
        const float u = (float)i / N;
        const cg::V2 p = ribbonAt(r, u), d = cg::cubicBezierTangent(r.p0, r.p1, r.p2, r.p3, u);
        const float l = std::max(1e-5f, cg::len(d)), w = lerpf(r.w0, r.w1, u);
        vtx(rgb(0.70f, 0.90f, 1.0f, alpha), p.x - d.y / l * w, p.y + d.x / l * w);
        vtx(rgb(0.46f, 0.76f, 0.98f, alpha), p.x + d.y / l * w, p.y - d.x / l * w);
    }
    glEnd();
    const float L = r.arc.total();
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    for (int lane = 0; lane < 6; lane++)
    {
        const float off = lane / 5.0f * 1.5f - 0.75f;
        for (int k = 0; k < 9; k++)
        {
            const float ph = cg::hashU(lane * 17u + k * 3u);
            const float s0 = fract(ph + k / 9.0f + time * speed / std::max(0.01f, L)) * L;
            const float s1 = std::min(L, s0 + 0.03f + 0.03f * cg::hashU(lane * 7u + k));
            const float u0 = r.arc.paramAt(s0), u1 = r.arc.paramAt(s1);
            const cg::V2 a = ribbonAt(r, u0), b = ribbonAt(r, u1), d = cg::cubicBezierTangent(r.p0, r.p1, r.p2, r.p3, u0);
            const float l = std::max(1e-5f, cg::len(d)), w = lerpf(r.w0, r.w1, u0) * off;
            setColor(rgb(1.0f, 1.0f, 1.0f, 0.85f * alpha));
            glVertex2f(a.x - d.y / l * w, a.y + d.x / l * w);
            glVertex2f(b.x - d.y / l * w, b.y + d.x / l * w);
        }
    }
    glEnd();
    glLineWidth(1.0f);
}

// stateless particles: splash + mist where the waterfall hits the pond
void drawSplash(float x, float y, float time)
{
    for (int k = 0; k < 34; k++)
    {
        const float life = 0.7f + 0.5f * cg::hashU(k * 7u + 1u);
        const float age = fract(time / life + cg::hashU(k * 3u + 2u)) * life;
        const float vx = (cg::hashU(k * 11u + 3u) - 0.5f) * 0.55f, vy = 0.18f + 0.22f * cg::hashU(k * 5u + 4u);
        const float px = x + vx * age, py = y + vy * age - 0.55f * age * age;
        if (py < y - 0.03f) continue;
        setColor(rgb(1.0f, 1.0f, 1.0f, 0.75f * (1.0f - age / life)));
        circle(px, py, 0.005f + 0.008f * age, 8);
    }
    for (int k = 0; k < 6; k++)
    {
        const float p = fract(time * 0.35f + k / 6.0f);
        setColor(rgb(1.0f, 1.0f, 1.0f, 0.22f * (1.0f - p)));
        ellipse(x + (cg::hashU(k * 5u) - 0.5f) * 0.12f, y + 0.02f + 0.10f * p, 0.04f + 0.08f * p, 0.03f + 0.05f * p, 18);
    }
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

// the rocky cliff on the left that the waterfall pours over
void drawCliff()
{
    const float P[][2] = {{-1.95f, -0.42f}, {-1.95f, 0.62f}, {-1.80f, 0.58f}, {-1.62f, 0.49f}, {-1.48f, 0.38f},
                          {-1.40f, 0.22f}, {-1.33f, 0.05f}, {-1.27f, -0.18f}, {-1.20f, -0.40f}};
    glBegin(GL_TRIANGLE_FAN);
    vtx(rgb(0.50f, 0.54f, 0.68f), -1.62f, 0.0f);
    for (const auto& p : P) vtx(p[1] > 0.3f ? rgb(0.66f, 0.70f, 0.82f) : rgb(0.56f, 0.60f, 0.74f), p[0], p[1]);
    glEnd();
    setColor(rgb(0.38f, 0.42f, 0.58f, 0.6f));                          // shadowed right face
    quad(-1.48f, 0.38f, -1.40f, 0.22f, -1.27f, -0.18f, -1.36f, -0.20f);
    setColor(rgb(0.45f, 0.48f, 0.62f));                                // rock ledges
    for (int k = 0; k < 6; k++)
    {
        const float y = 0.30f - 0.12f * k, x = -1.90f + 0.05f * (k & 1);
        rect(x, y, x + 0.30f + 0.04f * k, y + 0.012f);
    }
    setColor(rgb(0.34f, 0.66f, 0.30f));                                // grassy top + moss
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 1; i < 6; i++) { vtx(rgb(0.42f, 0.74f, 0.32f), P[i][0], P[i][1] + 0.015f); vtx(rgb(0.30f, 0.58f, 0.28f), P[i][0], P[i][1] - 0.04f); }
    glEnd();
    drawPine(-1.82f, 0.45f, 0.30f, 0.0f, 1.0f);
    drawPine(-1.66f, 0.44f, 0.24f, 0.0f, 0.9f);
    drawBush(-1.30f, -0.36f, 0.06f, rgb(0.16f, 0.46f, 0.22f), rgb(0.32f, 0.66f, 0.30f));
}

void drawBridge()                                                      // wooden arch over the pond (Bezier deck)
{
    const cg::V2 a(-1.18f, -0.45f), c(-0.93f, -0.30f), b(-0.66f, -0.45f);
    const int N = 24;
    setColor(rgb(0.48f, 0.30f, 0.16f));
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= N; i++) { const cg::V2 p = cg::quadBezier(a, c, b, (float)i / N); glVertex2f(p.x, p.y); glVertex2f(p.x, p.y - 0.035f); }
    glEnd();
    setColor(rgb(0.70f, 0.46f, 0.24f));
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= N; i++) { const cg::V2 p = cg::quadBezier(a, c, b, (float)i / N); glVertex2f(p.x, p.y + 0.012f); glVertex2f(p.x, p.y - 0.010f); }
    glEnd();
    for (int i = 0; i <= 8; i++)                                       // posts + rail
    {
        const cg::V2 p = cg::quadBezier(a, c, b, i / 8.0f);
        setColor(rgb(0.56f, 0.34f, 0.18f));
        rect(p.x - 0.008f, p.y, p.x + 0.008f, p.y + 0.075f);
        setColor(rgb(0.74f, 0.50f, 0.26f));
        circle(p.x, p.y + 0.078f, 0.011f, 10);
    }
    setColor(rgb(0.66f, 0.42f, 0.22f));
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= N; i++) { const cg::V2 p = cg::quadBezier(a, c, b, (float)i / N); glVertex2f(p.x, p.y + 0.072f); glVertex2f(p.x, p.y + 0.058f); }
    glEnd();
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
    drawCliff();
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

    fall_ = Ribbon{cg::V2(-1.465f, 0.375f), cg::V2(-1.43f, 0.33f), cg::V2(-1.40f, 0.02f), cg::V2(gardensim::FALL_X, gardensim::FALL_Y), 0.028f, 0.060f, {}};
    fall_.arc.build([](float t) { return ribbonAt(fall_, t); });
    upperStream_ = Ribbon{cg::V2(-1.86f, 0.62f), cg::V2(-1.70f, 0.56f), cg::V2(-1.56f, 0.47f), cg::V2(-1.468f, 0.385f), 0.008f, 0.024f, {}};
    upperStream_.arc.build([](float t) { return ribbonAt(upperStream_, t); });

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
    // the mountain stream and the waterfall into the pond (always flowing)
    drawWater(upperStream_, T, 0.20f, 0.9f);
    drawWater(fall_, T, 0.55f, 0.92f);

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
        static const float PX[] = {1.12f, 1.10f, 0.78f, 0.42f, 0.22f, 0.20f, 0.26f};
        static const float PY[] = {-0.19f, -0.22f, -0.40f, -0.60f, -0.82f, -1.05f, -1.30f};
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

    // ---- cottage (the same house style as the village) + pots, roses, pine ----
    drawPine(1.72f, -0.24f, 0.95f, wave(u, 5.0f, 0.2f) * 0.02f, 1.0f);
    drawHouse(1.12f, -0.17f, 0.64f, 0, true, GARDEN_LOOP_SECONDS * u);
    for (int side = -1; side <= 1; side += 2)                        // flower pots beside the door
    {
        const float px = 1.12f + side * 0.15f, py = -0.185f;
        setColor(rgb(0.80f, 0.40f, 0.22f)); quad(px - 0.035f, py, px + 0.035f, py, px + 0.045f, py + 0.06f, px - 0.045f, py + 0.06f);
        flowerCluster(px, py + 0.055f, 0.06f, 0.010f, 4, 50u + side, u, true);
    }
    flowerCluster(0.78f, -0.235f, 0.30f, 0.013f, 9, 61u, u, true);  // beds hugging the cottage
    flowerCluster(1.48f, -0.235f, 0.26f, 0.013f, 8, 67u, u, true);

    // ---- fence, big tree, bushes ----
    drawFence(-1.12f, 1.85f, -0.235f, 0.12f, rgb(0.86f, 0.62f, 0.38f));
    flowerCluster(-0.10f, -0.245f, 0.80f, 0.010f, 12, 71u, u, true);
    blossomBush(-0.98f, -0.28f, 0.07f, 3u);
    blossomBush(0.62f, -0.28f, 0.075f, 4u);
    blossomBush(1.58f, -0.24f, 0.06f, 5u);
    drawLushTree(-0.50f, -0.23f, 0.82f, wave(u, 6.0f, 0.1f));

    // ---- pond: water, stencil-masked reflections, ripples, lilies, rocks, reeds ----
    {
        const float px = gardensim::POND_X, py = gardensim::POND_Y, rx = gardensim::POND_RX, ry = gardensim::POND_RY;
        setColor(rgb(0.0f, 0.0f, 0.0f, 0.16f));
        ellipse(px, py - 0.03f, rx + 0.07f, ry + 0.05f, 64);
        setColor(rgb(0.60f, 0.58f, 0.54f));
        ellipse(px, py, rx + 0.05f, ry + 0.035f, 64);

        // 1) the water body also writes 1 into the STENCIL buffer
        glClear(GL_STENCIL_BUFFER_BIT);
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glBegin(GL_TRIANGLE_FAN);
        vtx(rgb(0.22f, 0.56f, 0.86f), px, py);
        for (int i = 0; i <= 72; i++)
        {
            const float a = TAU * i / 72.0f;
            vtx(mix(rgb(0.30f, 0.66f, 0.94f), rgb(0.56f, 0.86f, 1.0f), 0.5f + 0.5f * std::sin(a)), px + std::cos(a) * rx, py + std::sin(a) * ry);
        }
        glEnd();
        // 2) planar reflection: the cliff, waterfall and bridge mirrored about the far shore
        //    (homogeneous reflection matrix), drawn ONLY where the stencil is 1 (inside the water)
        glStencilFunc(GL_EQUAL, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        {
            float m[16];
            cg::Mat3::reflectY(py + ry * 0.55f).toGL(m);
            glPushMatrix();
            glMultMatrixf(m);
            const float oldA = gfx::alphaMul;
            gfx::alphaMul = 0.35f;
            drawCliff();
            drawWater(fall_, T, 0.55f, 0.8f);
            drawBridge();
            drawLushTree(-0.50f, -0.23f, 0.82f, wave(u, 6.0f, 0.1f));
            gfx::alphaMul = oldA;
            glPopMatrix();
        }
        setColor(rgb(0.80f, 0.94f, 1.0f, 0.35f));
        ellipse(px + 0.12f, py + 0.04f, rx * 0.42f, ry * 0.22f, 40);    // sky glint
        for (int k = 0; k < 5; k++)                                     // rings spreading from the plunge pool
        {
            const float p = fract(T * 0.5f + k / 5.0f), rr = 0.03f + 0.22f * p;
            setColor(rgb(1.0f, 1.0f, 1.0f, 0.6f * (1.0f - p)));
            ring(gardensim::FALL_X + 0.02f, gardensim::FALL_Y - 0.02f, rr, rr * (ry / rx), 48);
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
                ring(r.x, r.y, rr, rr * (ry / rx), 56);                 // the stencil clips it to the pond
            }
        }
        glLineWidth(1.0f);
        glDisable(GL_STENCIL_TEST);

        drawSplash(gardensim::FALL_X, gardensim::FALL_Y, T);
        drawBridge();
        // lily pads + lotus flowers bob gently
        for (int k = 0; k < 5; k++)
        {
            const float lx = px - 0.30f + k * 0.16f, ly = py - 0.06f + 0.06f * (k & 1) + 0.006f * wave(u, 15.0f, k * 0.3f);
            setColor(rgb(0.20f, 0.62f, 0.30f));
            ellipse(lx, ly, 0.065f, 0.022f, 20);
            setColor(rgb(0.40f, 0.76f, 0.94f));
            tri(lx, ly, lx + 0.07f, ly + 0.008f, lx + 0.07f, ly - 0.008f);
            if (k % 2 == 0)
            {
                setColor(rgb(1.0f, 0.62f, 0.80f));
                for (int p2 = -2; p2 <= 2; p2++) ellipse(lx + p2 * 0.009f, ly + 0.022f - std::abs(p2) * 0.004f, 0.010f, 0.022f, 10);
                setColor(rgb(1.0f, 0.85f, 0.35f));
                circle(lx, ly + 0.014f, 0.008f, 8);
            }
        }
        for (int k = 0; k < 16; k++)                                     // rim stones
        {
            const float a = TAU * k / 16.0f + 0.2f;
            if (std::sin(a) > 0.55f && std::cos(a) < -0.2f) continue;     // gap where the waterfall enters
            setColor(shade(rgb(0.74f, 0.72f, 0.68f), 0.85f + 0.2f * hash1(k * 5.0f)));
            ellipse(px + std::cos(a) * (rx + 0.05f), py + std::sin(a) * (ry + 0.04f), 0.05f, 0.028f, 14);
        }
        for (int k = 0; k < 7; k++)                                      // reeds on the right bank
        {
            const float bx = px + rx - 0.04f + k * 0.022f, h = 0.20f + 0.08f * hash1(k * 4.0f);
            const float sw = wave(u, 10.0f, k * 0.17f) * 0.025f;
            setColor(rgb(0.30f, 0.60f, 0.26f));
            glBegin(GL_TRIANGLE_STRIP);
            for (int s2 = 0; s2 <= 4; s2++)
            {
                const float v = s2 / 4.0f;
                glVertex2f(bx - 0.004f + sw * v * v, py - 0.06f + h * v);
                glVertex2f(bx + 0.004f + sw * v * v, py - 0.06f + h * v);
            }
            glEnd();
            if (k % 2 == 0) { setColor(rgb(0.42f, 0.26f, 0.14f)); ellipse(bx + sw, py - 0.06f + h + 0.015f, 0.008f, 0.026f, 8); }
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
        const float tilt = clampf(b.vx * 30.0f, -24.0f, 24.0f);
        drawButterfly(b.x, b.y, b.size, std::sin(b.flapPhase), b.col, -tilt);
        drawShimmer(b);
    }

    // --- foreground: big border flowers + grass (travelling sine wave sways every blade) ---
    for (int k = 0; k < 5; k++) blossomBush(-1.70f + k * 0.17f, -1.06f + 0.02f * (k & 1), 0.075f, 100u + k);
    for (int k = 0; k < 6; k++) blossomBush(0.80f + k * 0.19f, -1.06f + 0.02f * (k & 1), 0.08f, 120u + k);
    flowerCluster(-1.25f, -0.99f, 0.80f, 0.024f, 12, 81u, u, true);
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
