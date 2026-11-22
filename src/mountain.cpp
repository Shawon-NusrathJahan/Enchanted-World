#include "mountain.h"
#include "gfx.h"
#include "village.h"
#include "theme.h"

#include <algorithm>
#include <utility>
#include <vector>

using namespace gfx;

namespace
{

// ---------------------------------------------------------------------------
// Camera. Every layer is scaled about the pivot P (the village). A layer's
// "depth factor" f decides how strongly it reacts:
//     scale_i = 1 + (zoom - 1) * f_i          (forward journey)
//     pan_i   = -camX * f_i * PARALLAX        (slow sideways drift)
// f = 0 is infinitely far away, f = 1 is the village, f > 1 is in front of it.
// ---------------------------------------------------------------------------
const float PX = 0.0f, PY = -0.09f;
const float PARALLAX = 1.0f;

const float F_SKY    = 0.04f;
const float F_FAR0   = 0.06f;     // snowy giants (full width backdrop)
const float F_FAR_A  = 0.12f;     // blue alpine ridge
const float F_FAR_B  = 0.22f;     // grey-violet crags
const float F_MID    = 0.34f;     // green rounded mountains
const float F_HILLS  = 0.55f;     // forested hills
const float F_VALLEY = 1.00f;

struct Pine { float x, y, h, phase; };

// Mood: 0 = grey, misty, overcast (opening)  ..  1 = warm and sunny
float gMood = 1.0f;
Color P(const Color& moody, const Color& sunny) { return mix(moody, sunny, gMood); }

Ridge far0, farA, farB, midR, hillsR, valleyTop;
std::vector<Pine> midPines, hillPines, valleyPinesBack, valleyPinesFront;

// ---------------------------------------------------------------------------
// Massifs: whole mountains that sit in front of the village. They come in
// left/right pairs. At the start each pair is closed in the middle; during the
// reveal every left mountain slides left and every right one slides right
// (nearer pairs travel further), opening a gap that shows the village.
// ---------------------------------------------------------------------------
struct Massif
{
    float cx, dir, f, slide, span, base, snowY;   // snowY < 0 : no snow
    int   kind;                                   // 0 alpine, 1 rocky green, 2 forest
    bool  fall;
    float fallX;
    Ridge r;
    std::vector<Pine> pines;
};
std::vector<Massif> massifs;

Massif makeMassif(float cx, float dir, float f, float slide, float base, float snowY, int kind,
                  const std::vector<PeakDef>& peaks, float seed, float rough, float treeLine, int nPines)
{
    Massif m;
    m.cx = cx; m.dir = dir; m.f = f; m.slide = slide; m.base = base; m.snowY = snowY; m.kind = kind;
    m.span = 1.90f; m.fall = false; m.fallX = 0.0f;
    m.r = makePeakRidge(-m.span, m.span, 0.02f, base, peaks, seed, rough, false);
    // foothills: both ends sink smoothly below the bottom of the picture
    for (size_t i = 0; i < m.r.x.size(); i++)
    {
        const float x = m.r.x[i];
        const float inner = dir < 0.0f ? (m.span - x) : (x + m.span);      // distance to the end facing the centre
        const float k = smoothstep(clampf(inner / 0.95f, 0.0f, 1.0f));
        m.r.y[i] = lerpf(-1.3f, m.r.y[i], k);
    }
    for (int i = 0; i < nPines; i++)
    {
        const float x  = -m.span * 0.90f + 2.0f * m.span * 0.90f * hash1(i * 7.13f + seed);
        const float yh = std::min(ridgeAt(m.r, x) - 0.04f, treeLine);
        const float yl = std::max(-1.05f, yh - 0.65f);
        if (yh < -1.0f) continue;
        const float y  = lerpf(yl, yh, std::pow(hash1(i * 3.91f + seed * 2.0f), 0.8f));
        const float h  = (0.045f + 0.045f * hash1(i * 5.5f + seed)) * (0.55f + f) + 0.10f * (yh - y);
        m.pines.push_back({x, y, h, hash1(i * 1.3f + seed)});
    }
    std::stable_sort(m.pines.begin(), m.pines.end(), [](const Pine& a, const Pine& b) { return a.y > b.y; });
    return m;
}

// Animated waterfall running down a mountain flank (local massif coordinates).
void drawFall(float x, float yTop, float yEnd, float width, float t, float seed)
{
    const int N = 40;
    auto cxAt = [&](float y) { return x + 0.012f * std::sin(y * 14.0f + seed); };
    auto wd   = [&](float y) { return width * (0.55f + 0.9f * (yTop - y) / (yTop - yEnd)); };

    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= N; i++)
    {
        const float y = lerpf(yTop, yEnd, (float)i / N);
        const float a = (0.60f + 0.25f * std::sin(y * 28.0f - t * 8.0f)) * smoothstep(0.0f, 0.05f, yTop - y);
        vtx(rgb(0.95f, 0.98f, 1.0f, a), cxAt(y) - wd(y) * 0.5f, y);
        vtx(rgb(0.95f, 0.98f, 1.0f, a), cxAt(y) + wd(y) * 0.5f, y);
    }
    glEnd();
    for (int k = 0; k < 5; k++)
    {
        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= N; i++)
        {
            const float y = lerpf(yTop, yEnd, (float)i / N);
            const float a = clampf(std::sin(y * 22.0f - t * 11.0f + k * 2.1f), 0.0f, 1.0f) * 0.8f;
            const float xx = cxAt(y) + (k - 2.0f) / 5.0f * wd(y);
            vtx(rgb(1.0f, 1.0f, 1.0f, a), xx - 0.003f, y);
            vtx(rgb(1.0f, 1.0f, 1.0f, a), xx + 0.003f, y);
        }
        glEnd();
    }
    for (int i = 0; i < 6; i++)                                      // spray at the foot
    {
        const float p = fract(t * 0.25f + i / 6.0f);
        glow(x + (i - 2.5f) * 0.03f * (0.5f + p), yEnd + 0.02f + p * 0.22f, (0.07f + 0.16f * p) * (0.5f + width * 6.0f),
             rgb(1.0f, 1.0f, 1.0f, 0.5f * (1.0f - p)), 20);
    }
}


// ---------------------------------------------------------------------------
// Layer helpers
// ---------------------------------------------------------------------------
void beginLayer(float f, const AnimState& s)
{
    const float scale = 1.0f + (s.zoom - 1.0f) * f;
    const float panX  = -s.camX * f * PARALLAX;
    const float panY  = -PY * f * s.travel;          // journey brings the village to centre
    pushCamera(scale, PX, PY, panX, panY);
}

float wrap(float v, float lo, float hi)
{
    const float w = hi - lo;
    return lo + std::fmod(std::fmod(v - lo, w) + w, w);
}

// Soft drifting fog band: overlapping flat ellipses that fade out at the rim.
void drawMist(float y, float thick, float alpha, float t, float speed, float seed)
{
    const float a = alpha * (1.0f + 0.7f * (1.0f - gMood));          // foggier while moody
    for (int i = 0; i < 6; i++)
    {
        const float cx = wrap(-2.8f + i * 1.1f + speed * t + seed * 0.7f, -3.2f, 3.2f);
        const float cy = y + 0.03f * std::sin(t * 0.3f + i * 1.9f + seed);
        const float rx = 0.9f + 0.3f * hash1(i * 3.3f + seed);
        glBegin(GL_TRIANGLE_FAN);
        gfx::paint(0.95f, 0.97f, 0.98f, clampf(a, 0.0f, 0.9f));
        glVertex2f(cx, cy);
        gfx::paint(0.95f, 0.97f, 0.98f, 0.0f);
        for (int k = 0; k <= 24; k++)
            glVertex2f(cx + std::cos(TAU * k / 24) * rx, cy + std::sin(TAU * k / 24) * thick);
        glEnd();
    }
}

void drawPines(const std::vector<Pine>& list, float t, float dark)
{
    for (const Pine& p : list)
        if (visible(p.x, p.y + p.h * 0.5f, p.h * 0.5f))
            drawPine(p.x, p.y, p.h, 0.008f * std::sin(t + p.phase * 6.0f), dark);
}

// ---------------------------------------------------------------------------
// Scene parts
// ---------------------------------------------------------------------------
void drawSky(float t)
{
    Stop stops[] = {
        {-1.6f, P(rgb(0.94f, 0.95f, 0.95f), rgb(0.99f, 0.90f, 0.82f))},
        {-0.1f, P(rgb(0.94f, 0.95f, 0.95f), rgb(0.99f, 0.90f, 0.82f))},
        {0.12f, P(rgb(0.88f, 0.92f, 0.94f), rgb(0.80f, 0.89f, 0.96f))},
        {0.45f, P(rgb(0.76f, 0.84f, 0.90f), rgb(0.50f, 0.74f, 0.94f))},
        {0.80f, P(rgb(0.62f, 0.72f, 0.82f), rgb(0.28f, 0.52f, 0.88f))},
        {1.60f, P(rgb(0.52f, 0.63f, 0.76f), rgb(0.13f, 0.30f, 0.68f))},
        {2.40f, P(rgb(0.52f, 0.63f, 0.76f), rgb(0.13f, 0.30f, 0.68f))}};
    for (Stop& st : stops) st.c = theme::skyColor(st.c, st.y);      // time-of-day sky
    gfx::useTint = false;                                           // sky is already final
    vGradient(-3.2f, 3.2f, stops, 7);

    glow(0.10f, 0.28f, 1.35f, theme::sun(P(rgb(1.0f, 1.0f, 1.0f, 0.55f), rgb(1.0f, 0.88f, 0.70f, 0.85f))), 48);
    glow(0.10f, 0.28f, 0.50f, theme::sun(P(rgb(1.0f, 1.0f, 1.0f, 0.55f), rgb(1.0f, 0.96f, 0.85f, 0.9f))), 40);
    theme::drawStarsAndMoon(-0.80f, 0.62f, 0.09f, fract(t / 60.0f));
    gfx::useTint = true;

    struct C { float x, y, s, speed, pink; };
    static const C clouds[] = {{-1.30f, 0.84f, 0.85f, 0.010f, 0.85f}, {1.35f, 0.84f, 0.95f, 0.007f, 0.80f},
                               {0.15f, 0.86f, 0.45f, 0.012f, 0.55f},  {-0.55f, 0.90f, 0.35f, 0.015f, 0.40f},
                               {0.80f, 0.90f, 0.38f, 0.009f, 0.45f}};
    for (const C& c : clouds) drawCloud(wrap(c.x + c.speed * t, -3.2f, 3.2f), c.y, c.s, c.pink * gMood, 0.75f + 0.25f * gMood);

    for (int i = 0; i < 5; i++)
    {
        const float bx = wrap(-1.4f + i * 0.45f + 0.06f * t, -3.0f, 3.0f);
        const float by = 0.70f + 0.05f * std::sin(i * 2.3f + t * 0.7f) + 0.04f * i;
        drawBird(bx, by, 0.024f, std::sin(t * 7.0f + i * 1.7f), rgb(0.16f, 0.20f, 0.32f));
    }
}

void drawValley(float t, const AnimState& s)
{
    // turquoise lake behind the village (the ground below covers its lower half)
    {
        const float cx = 0.10f, cy = 0.10f;
        setColor(P(rgb(0.40f, 0.52f, 0.46f), rgb(0.55f, 0.72f, 0.56f)));
        ellipse(cx, cy, 1.02f, 0.125f, 48);                                  // shore
        setColor(P(rgb(0.16f, 0.58f, 0.62f), rgb(0.22f, 0.72f, 0.76f)));
        ellipse(cx, cy, 0.97f, 0.112f, 48);
        setColor(P(rgb(0.50f, 0.82f, 0.82f, 0.55f), rgb(0.60f, 0.90f, 0.88f, 0.55f)));
        ellipse(cx, cy + 0.04f, 0.80f, 0.055f, 48);
        setColor(rgb(1.0f, 1.0f, 1.0f, 0.35f));                              // pale reflection wedge
        tri(0.45f, cy + 0.10f, 0.20f, cy - 0.02f, 0.70f, cy - 0.02f);
        for (int k = 0; k < 9; k++)                                           // shimmer
        {
            const float x = cx - 0.75f + k * 0.19f + 0.04f * std::sin(t * 0.9f + k);
            setColor(rgb(1.0f, 1.0f, 1.0f, 0.22f + 0.18f * std::sin(t * 1.3f + k * 1.7f)));
            ellipse(x, cy + 0.03f * std::sin(k * 2.0f), 0.07f, 0.004f, 10);
        }
        drawMist(cy - 0.01f, 0.05f, 0.18f, t, 0.02f, 9.0f);
        for (int k = 0; k < 16; k++)                                          // pines on the far shore
        {
            const float a = PI * (0.08f + 0.84f * k / 15.0f);
            drawPine(cx + 1.0f * std::cos(a), cy + 0.12f * std::sin(a) - 0.015f, 0.05f + 0.03f * hash1(k * 2.0f), 0.0f, 0.8f);
        }
    }

    drawRidge(valleyTop, -1.9f, rgb(0.62f, 0.78f, 0.42f), rgb(0.20f, 0.45f, 0.22f));

    struct Blob { float x, y, rx, ry; Color c; };
    static const Blob blobs[] = {
        {-1.20f, -0.30f, 0.95f, 0.26f, rgb(0.74f, 0.84f, 0.44f, 0.75f)},
        {1.25f, -0.38f, 0.95f, 0.24f, rgb(0.52f, 0.72f, 0.34f, 0.75f)},
        {-0.40f, -0.75f, 1.00f, 0.30f, rgb(0.40f, 0.64f, 0.30f, 0.65f)},
        {0.55f, -0.95f, 1.20f, 0.30f, rgb(0.52f, 0.72f, 0.34f, 0.65f)},
        {-1.50f, -0.85f, 0.90f, 0.32f, rgb(0.30f, 0.55f, 0.26f, 0.65f)}};
    for (const Blob& b : blobs)
    {
        glBegin(GL_TRIANGLE_FAN);
        setColor(b.c);
        glVertex2f(b.x, b.y);
        gfx::paint(b.c.r, b.c.g, b.c.b, 0.0f);
        for (int i = 0; i <= 32; i++)
            glVertex2f(b.x + std::cos(TAU * i / 32) * b.rx, b.y + std::sin(TAU * i / 32) * b.ry);
        glEnd();
    }

    struct Field { float yt, xl0, xr0, yb, xl1, xr1; Color base, row; };
    static const Field fields[] = {
        {-0.19f, 0.40f, 0.80f, -0.64f, 0.46f, 1.28f, rgb(0.88f, 0.80f, 0.48f), rgb(0.78f, 0.68f, 0.36f)},
        {-0.21f, -1.00f, -0.58f, -0.66f, -1.42f, -0.70f, rgb(0.64f, 0.80f, 0.40f), rgb(0.52f, 0.70f, 0.32f)}};
    for (const Field& f : fields)
    {
        setColor(f.base);
        quad(f.xl0, f.yt, f.xr0, f.yt, f.xr1, f.yb, f.xl1, f.yb);
        setColor(f.row);
        for (int i = 1; i < 10; i++)
        {
            const float k = i / 10.0f;
            thickLine(lerpf(f.xl0, f.xr0, k), f.yt, lerpf(f.xl1, f.xr1, k), f.yb, 0.006f);
        }
    }

    setColor(rgb(0.66f, 0.80f, 0.44f, 0.75f));
    ellipse(0.0f, -0.085f, 0.70f, 0.15f, 40);
    setColor(rgb(0.80f, 0.70f, 0.48f, 0.55f));
    ellipse(0.0f, -0.17f, 0.42f, 0.04f, 40);

    const int RN = 40;
    auto riverX = [](float v) { return -0.72f - 0.50f * v + 0.15f * std::sin(5.0f * v); };
    auto riverY = [](float v) { return 0.02f - 1.75f * v; };
    auto riverW = [](float v) { return 0.010f + 0.085f * v * v + 0.02f * v; };
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= RN; i++)
    {
        const float v = (float)i / RN, w = riverW(v) * 1.35f;
        vtx(rgb(0.60f, 0.74f, 0.46f), riverX(v) - w, riverY(v));
        vtx(rgb(0.60f, 0.74f, 0.46f), riverX(v) + w, riverY(v));
    }
    glEnd();
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= RN; i++)
    {
        const float v = (float)i / RN, w = riverW(v);
        vtx(rgb(0.50f, 0.80f, 0.92f), riverX(v) - w, riverY(v));
        vtx(rgb(0.30f, 0.62f, 0.82f), riverX(v) + w, riverY(v));
    }
    glEnd();
    for (int i = 0; i < 8; i++)
    {
        const float v = fract(t * 0.05f + i / 8.0f);
        setColor(rgb(1.0f, 1.0f, 1.0f, 0.55f * std::sin(v * PI)));
        ellipse(riverX(v), riverY(v), riverW(v) * 0.55f, 0.004f + 0.006f * v, 10);
    }

    const int PN = 40;
    auto pathX = [](float v) { return -0.06f + 0.70f * v + 0.18f * v * std::sin(4.0f * v + 0.5f); };
    auto pathY = [](float v) { return -0.10f - 1.55f * v; };
    auto pathW = [](float v) { return 0.026f + 0.17f * v; };
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= PN; i++)
    {
        const float v = (float)i / PN, w = pathW(v) * 1.25f;
        vtx(rgb(0.70f, 0.58f, 0.38f), pathX(v) - w, pathY(v));
        vtx(rgb(0.70f, 0.58f, 0.38f), pathX(v) + w, pathY(v));
    }
    glEnd();
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= PN; i++)
    {
        const float v = (float)i / PN, w = pathW(v);
        vtx(rgb(0.93f, 0.84f, 0.60f), pathX(v) - w, pathY(v));
        vtx(rgb(0.86f, 0.74f, 0.50f), pathX(v) + w, pathY(v));
    }
    glEnd();

    for (const Pine& p : valleyPinesBack)
        if (visible(p.x, p.y + p.h * 0.5f, p.h * 0.5f))
            drawPine(p.x, p.y, p.h, 0.012f * std::sin(t * 1.2f + p.phase * 6.0f));
    drawVillage(s.time);
    for (const Pine& p : valleyPinesFront)
        if (visible(p.x, p.y + p.h * 0.5f, p.h * 0.5f))
            drawPine(p.x, p.y, p.h, 0.012f * std::sin(t * 1.2f + p.phase * 6.0f));
}

void drawMassif(const Massif& m, const AnimState& s, float t)
{
    const float slide = m.dir * m.slide * s.open;     // world units, scaled with the layer
    beginLayer(m.f, s);

    // cheap whole-mountain culling
    const float sx0 = toScreenX(m.cx + slide - m.span), sx1 = toScreenX(m.cx + slide + m.span);
    if (sx1 < -WORLD_W - 0.3f || sx0 > WORLD_W + 0.3f) { ++statCulled; popCamera(); return; }
    ++statDrawn;

    glPushMatrix();
    glTranslatef(m.cx + slide, 0.0f, 0.0f);

    // distance haze: far mountains melt toward the sky colour
    const Color haze = P(rgb(0.84f, 0.88f, 0.90f), rgb(0.80f, 0.89f, 0.96f));
    const float hz   = clampf((0.85f - m.f) * 0.70f, 0.0f, 0.6f);
    Color cTop, cMid, cBot;
    if (m.kind == 0)
    {
        cTop = P(rgb(0.52f, 0.58f, 0.66f), rgb(0.54f, 0.62f, 0.84f));
        cMid = P(rgb(0.38f, 0.46f, 0.50f), rgb(0.40f, 0.52f, 0.62f));
        cBot = P(rgb(0.16f, 0.32f, 0.26f), rgb(0.20f, 0.46f, 0.38f));
    }
    else if (m.kind == 1)
    {
        cTop = P(rgb(0.42f, 0.48f, 0.46f), rgb(0.52f, 0.58f, 0.54f));
        cMid = P(rgb(0.20f, 0.36f, 0.26f), rgb(0.28f, 0.54f, 0.36f));
        cBot = P(rgb(0.08f, 0.22f, 0.16f), rgb(0.10f, 0.32f, 0.22f));
    }
    else
    {
        cTop = P(rgb(0.32f, 0.40f, 0.34f), rgb(0.44f, 0.54f, 0.42f));
        cMid = P(rgb(0.14f, 0.30f, 0.18f), rgb(0.20f, 0.46f, 0.26f));
        cBot = P(rgb(0.04f, 0.14f, 0.08f), rgb(0.06f, 0.20f, 0.12f));
    }
    cTop = mix(cTop, haze, hz); cMid = mix(cMid, haze, hz * 0.8f); cBot = mix(cBot, haze, hz * 0.5f);

    // body: rock on top, olive slope, dark forest at the foot
    glBegin(GL_TRIANGLE_STRIP);
    for (size_t i = 0; i < m.r.x.size(); i++)
    {
        const float yt = m.r.y[i];
        vtx(cTop, m.r.x[i], yt);
        vtx(cMid, m.r.x[i], lerpf(yt, -1.3f, 0.40f));
    }
    glEnd();
    glBegin(GL_TRIANGLE_STRIP);
    for (size_t i = 0; i < m.r.x.size(); i++)
    {
        const float yt = m.r.y[i];
        vtx(cMid, m.r.x[i], lerpf(yt, -1.3f, 0.40f));
        vtx(cBot, m.r.x[i], -1.3f);
    }
    glEnd();

    // sunlit and shaded faces give every peak volume (profile clipped at the base so no giant wedges)
    Ridge clipped = m.r;
    for (float& y : clipped.y) y = std::max(y, m.base - 0.02f);
    drawRidgeLights(clipped, mix(P(rgb(0.90f, 0.93f, 0.95f, 0.30f), rgb(1.00f, 0.95f, 0.78f, 0.38f)), withAlpha(haze, 0.25f), hz));
    drawRidgeShadows(clipped, mix(P(rgb(0.08f, 0.14f, 0.20f, 0.55f), rgb(0.10f, 0.16f, 0.34f, 0.55f)), withAlpha(haze, 0.35f), hz * 0.7f));
    if (m.snowY > 0.0f)
        drawSnow(m.r, m.snowY, rgb(1.0f, 1.0f, 1.0f), P(rgb(0.86f, 0.91f, 0.96f), rgb(0.88f, 0.92f, 1.0f)));
    if (m.kind != 2)
        drawRockStreaks(m.r, mix(P(rgb(0.08f, 0.12f, 0.18f, 0.35f), rgb(0.14f, 0.20f, 0.36f, 0.32f)), withAlpha(haze, 0.2f), hz), m.base + 0.25f);

    // forest: scattered pines, bigger and darker toward the foot
    const float pd = 0.60f + 0.35f * hz;
    for (const Pine& p : m.pines)
        if (visible(p.x + m.cx + slide, p.y + p.h * 0.5f, p.h * 0.5f))
            drawPine(p.x, p.y, p.h, 0.006f * std::sin(t * 1.1f + p.phase * 6.0f), pd + 0.3f * p.phase);

    if (m.fall) drawFall(m.fallX, ridgeAt(m.r, m.fallX) - 0.10f, ridgeAt(m.r, m.fallX) - 0.62f, 0.05f, t, m.fallX * 7.0f);

    glPopMatrix();
    popCamera();
}


}   // namespace

// ---------------------------------------------------------------------------
void initMountain()
{
    far0 = makePeakRidge(-3.0f, 3.0f, 0.02f, 0.10f,
                         {{-2.3f, 0.52f, 0.65f}, {-1.45f, 0.68f, 0.60f}, {-0.55f, 0.56f, 0.55f},
                          {0.35f, 0.74f, 0.65f}, {1.30f, 0.60f, 0.60f}, {2.25f, 0.54f, 0.65f}},
                         2.1f, 0.05f, false);
    farA = makeRidge(-3.0f, 3.0f, 0.02f, 0.10f, 0.52f, 1.3f, 2.4f);
    farB = makePeakRidge(-3.0f, 3.0f, 0.02f, 0.04f,
                         {{-2.5f, 0.40f, 0.42f}, {-1.7f, 0.50f, 0.40f}, {-0.9f, 0.34f, 0.45f}, {-0.1f, 0.46f, 0.38f},
                          {0.7f, 0.52f, 0.42f}, {1.6f, 0.38f, 0.45f}, {2.4f, 0.48f, 0.42f}},
                         5.7f, 0.07f, false);
    midR = makePeakRidge(-3.0f, 3.0f, 0.02f, 0.00f,
                         {{-2.3f, 0.40f, 0.75f}, {-1.1f, 0.33f, 0.65f}, {0.0f, 0.28f, 0.60f},
                          {1.2f, 0.42f, 0.75f}, {2.4f, 0.35f, 0.70f}},
                         3.3f, 0.04f, true);
    hillsR    = makeRidge(-3.0f, 3.0f, 0.02f, -0.02f, 0.24f, 2.2f, 5.5f);
    valleyTop = makeRolling(-3.0f, 3.0f, 0.02f, 0.00f, 0.07f, 1.1f, 2.8f);

    auto scatter = [](std::vector<Pine>& out, const Ridge& r, float step, float hMin, float hVar, float dy, float seed)
    {
        for (float x = -2.9f; x < 2.9f; x += step * (0.6f + 0.8f * hash1(x * 31.7f + seed)))
            out.push_back({x, ridgeAt(r, x) - dy - 0.02f * hash1(x * 5.1f), hMin + hVar * hash1(x * 7.3f + seed), hash1(x * 3.1f)});
    };
    midPines.clear(); hillPines.clear();
    scatter(midPines, midR, 0.05f, 0.035f, 0.035f, 0.010f, 1.0f);
    scatter(hillPines, hillsR, 0.04f, 0.055f, 0.055f, 0.015f, 2.0f);

    valleyPinesBack = {{-1.00f, -0.10f, 0.12f, 0.1f}, {-0.84f, -0.08f, 0.10f, 0.4f}, {0.90f, -0.10f, 0.12f, 0.7f},
                       {0.76f, -0.07f, 0.10f, 0.2f},  {-0.70f, -0.06f, 0.11f, 0.9f}, {0.72f, -0.05f, 0.11f, 0.5f},
                       {-1.15f, -0.06f, 0.10f, 0.3f}, {1.10f, -0.06f, 0.11f, 0.8f}};
    valleyPinesFront = {{-0.95f, -0.22f, 0.20f, 0.3f}, {-0.82f, -0.30f, 0.16f, 0.6f}, {-1.02f, -0.38f, 0.22f, 0.8f},
                        {-0.72f, -0.20f, 0.12f, 0.1f}, {-0.90f, -0.50f, 0.24f, 0.4f}, {-0.62f, -0.16f, 0.10f, 0.7f},
                        {-1.15f, -0.25f, 0.17f, 0.2f}, {-1.20f, -0.55f, 0.26f, 0.5f},
                        {0.92f, -0.24f, 0.20f, 0.5f},  {0.80f, -0.31f, 0.15f, 0.2f},  {1.02f, -0.40f, 0.22f, 0.9f},
                        {0.70f, -0.20f, 0.11f, 0.6f},  {0.88f, -0.52f, 0.25f, 0.3f},  {0.64f, -0.18f, 0.10f, 0.1f},
                        {1.15f, -0.27f, 0.17f, 0.4f},  {1.22f, -0.58f, 0.27f, 0.7f}};

    // Three depths of mountain pairs. Far pairs move little, near pairs a lot.
    massifs.clear();
    // far: tall snowy alpine peaks
    massifs.push_back(makeMassif(-0.5f, -1.0f, 0.35f, 2.2f, -0.05f, 0.55f, 0,
        {{-0.95f, 0.88f, 0.42f}, {-0.30f, 0.98f, 0.50f}, {0.35f, 0.80f, 0.40f}, {0.95f, 0.66f, 0.45f}, {-0.55f, 0.60f, 0.35f}},
        2.4f, 0.07f, 0.20f, 70));
    massifs.push_back(makeMassif(0.5f, 1.0f, 0.35f, 2.2f, -0.05f, 0.55f, 0,
        {{-0.95f, 0.70f, 0.45f}, {-0.25f, 0.90f, 0.50f}, {0.45f, 0.98f, 0.42f}, {1.00f, 0.78f, 0.40f}, {0.10f, 0.55f, 0.35f}},
        5.1f, 0.07f, 0.20f, 70));
    // mid: rocky green mountains
    massifs.push_back(makeMassif(-0.5f, -1.0f, 0.55f, 2.7f, -0.25f, -1.0f, 1,
        {{-0.90f, 0.62f, 0.50f}, {-0.20f, 0.78f, 0.52f}, {0.45f, 0.70f, 0.48f}, {0.95f, 0.52f, 0.40f}},
        7.7f, 0.06f, 0.30f, 110));
    massifs.back().fall = true; massifs.back().fallX = -0.18f;
    massifs.push_back(makeMassif(0.5f, 1.0f, 0.55f, 2.7f, -0.25f, -1.0f, 1,
        {{-0.95f, 0.54f, 0.42f}, {-0.30f, 0.72f, 0.50f}, {0.35f, 0.82f, 0.52f}, {0.95f, 0.64f, 0.48f}},
        3.9f, 0.06f, 0.30f, 110));
    // near: forest-covered mountains
    massifs.push_back(makeMassif(-0.5f, -1.0f, 0.80f, 3.3f, -0.52f, -1.0f, 2,
        {{-0.85f, 0.68f, 0.55f}, {-0.15f, 0.82f, 0.60f}, {0.50f, 0.74f, 0.52f}, {0.95f, 0.62f, 0.50f}},
        8.9f, 0.05f, 0.05f, 150));
    massifs.push_back(makeMassif(0.5f, 1.0f, 0.80f, 3.3f, -0.52f, -1.0f, 2,
        {{-0.95f, 0.66f, 0.50f}, {-0.35f, 0.76f, 0.58f}, {0.25f, 0.84f, 0.60f}, {0.90f, 0.70f, 0.55f}},
        6.3f, 0.05f, 0.05f, 150));

    initVillage();
}

void drawMountainScene(const AnimState& s)
{
    const float t = (float)s.time;
    gMood = smoothstep((t - 4.0f) / 9.0f);               // grey & misty -> warm & sunny

    beginLayer(F_SKY, s);
    drawSky(t);
    popCamera();

    beginLayer(F_FAR0, s);
    drawRidge(far0, -0.6f, P(rgb(0.60f, 0.67f, 0.76f), rgb(0.60f, 0.66f, 0.90f)), P(rgb(0.86f, 0.90f, 0.93f), rgb(0.86f, 0.91f, 0.99f)));
    drawRidgeLights(far0, rgb(0.95f, 0.97f, 1.00f, 0.35f));
    drawSnow(far0, 0.46f, rgb(1.00f, 1.00f, 1.00f), P(rgb(0.88f, 0.92f, 0.96f), rgb(0.86f, 0.90f, 1.00f)));
    drawRidgeShadows(far0, P(rgb(0.42f, 0.50f, 0.62f, 0.50f), rgb(0.46f, 0.52f, 0.82f, 0.50f)));
    drawMist(0.30f, 0.10f, 0.30f, t, 0.012f, 1.0f);
    popCamera();

    beginLayer(F_FAR_A, s);
    drawRidge(farA, -0.6f, P(rgb(0.44f, 0.56f, 0.64f), rgb(0.46f, 0.66f, 0.88f)), P(rgb(0.80f, 0.86f, 0.89f), rgb(0.80f, 0.89f, 0.96f)));
    drawRidgeLights(farA, rgb(0.86f, 0.93f, 1.00f, 0.30f));
    drawRidgeShadows(farA, P(rgb(0.30f, 0.42f, 0.54f, 0.55f), rgb(0.34f, 0.54f, 0.80f, 0.55f)));
    drawMist(0.20f, 0.09f, 0.30f, t, -0.015f, 2.0f);
    popCamera();

    beginLayer(F_FAR_B, s);
    drawRidge(farB, -0.6f, P(rgb(0.34f, 0.44f, 0.50f), rgb(0.42f, 0.50f, 0.72f)), P(rgb(0.70f, 0.77f, 0.80f), rgb(0.72f, 0.80f, 0.92f)));
    drawRidgeLights(farB, rgb(0.84f, 0.88f, 0.98f, 0.30f));
    drawRidgeShadows(farB, P(rgb(0.14f, 0.22f, 0.30f, 0.55f), rgb(0.20f, 0.28f, 0.58f, 0.55f)));
    drawRockStreaks(farB, P(rgb(0.10f, 0.16f, 0.22f, 0.35f), rgb(0.18f, 0.24f, 0.50f, 0.30f)), 0.22f);
    drawMist(0.12f, 0.09f, 0.34f, t, 0.02f, 3.0f);
    popCamera();

    beginLayer(F_MID, s);
    drawRidge(midR, -0.7f, P(rgb(0.12f, 0.30f, 0.24f), rgb(0.18f, 0.50f, 0.38f)), P(rgb(0.50f, 0.62f, 0.58f), rgb(0.52f, 0.76f, 0.66f)));
    drawRidgeLights(midR, P(rgb(0.62f, 0.72f, 0.34f, 0.35f), rgb(0.62f, 0.88f, 0.52f, 0.40f)));
    drawRidgeShadows(midR, rgb(0.03f, 0.20f, 0.22f, 0.55f));
    drawPines(midPines, t, 0.70f);
    drawMist(0.06f, 0.08f, 0.38f, t, -0.02f, 4.0f);
    popCamera();

    beginLayer(F_HILLS, s);
    drawRidge(hillsR, -0.8f, P(rgb(0.10f, 0.28f, 0.17f), rgb(0.16f, 0.48f, 0.36f)), P(rgb(0.30f, 0.46f, 0.28f), rgb(0.38f, 0.68f, 0.46f)));
    drawRidgeLights(hillsR, P(rgb(0.66f, 0.72f, 0.28f, 0.32f), rgb(0.62f, 0.86f, 0.50f, 0.30f)));
    drawRidgeShadows(hillsR, rgb(0.03f, 0.20f, 0.20f, 0.55f));
    drawPines(hillPines, t, 0.75f);
    popCamera();

    beginLayer(F_VALLEY, s);
    drawValley(t, s);
    popCamera();

    // mountain pairs, far -> near; drifting mist between the depths
    for (size_t i = 0; i < massifs.size(); i++)
    {
        drawMassif(massifs[i], s, t);
        if (i % 2 == 1 && s.open < 0.98f)
        {
            beginLayer(massifs[i].f, s);
            drawMist(massifs[i].base + 0.10f, 0.07f, 0.20f * (1.0f - s.open), t, 0.02f * (i % 4 ? 1 : -1), 6.0f + (float)i);
            popCamera();
        }
    }
}
