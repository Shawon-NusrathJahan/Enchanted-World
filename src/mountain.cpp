// mountain.cpp - the opening film: title screen, mountain peaks, the parting mountains,
//                the village on its plateau with the lake and waterfalls, the journey in.
//
// Computer-graphics techniques used here (see CG_ALGORITHMS.md):
//   * fractal terrain: peak envelopes + 1D midpoint displacement + fBm value noise
//   * painterly lighting: every summit gets a light/shadow "spine" (lit west faces, shadowed
//     east faces), snow caps above a noisy snow line, warm rim light on sun-facing flanks
//   * aerial perspective: colours blend toward the sky haze colour with distance
//   * layered parallax camera: layer f is drawn with  screen = Z^f * (p - f*C)
//   * camera path: Kochanek-Bartels (TCB) Hermite spline through key shots (log-zoom space)
//   * zoom-to-point: the camera keeps a target point sliding smoothly to the screen centre
//   * display lists (retained mode) cache every static mountain range
//   * waterfalls/streams: Bezier ribbons, arc-length parameterised flow streaks (texture-like
//     UV scrolling), stateless analytic particle system for spray and foam

#include "mountain.h"

#include "cgmath.h"
#include "font.h"
#include "gfx.h"
#include "theme.h"
#include "ui.h"
#include "village.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace gfx;

namespace
{

// ----------------------------------------------------------------------------
// Mountain ranges
// ----------------------------------------------------------------------------
struct MPeak { float x, h, w; };
struct Summit  { float x, y; int i; float valley; int valleyI; float lean; uint32_t seed; };

struct Range
{
    float f = 1.0f;                 // parallax depth (0 = infinitely far, 1 = nearest layer)
    float x0 = -2.6f, step = 0.005f, base = -1.2f;
    std::vector<float> y;           // silhouette
    std::vector<Summit> summits;
    Color lit, shadow, bottom, snow, rim, haze;
    float hazeK = 0.0f;             // aerial perspective amount
    float snowLine = 1.0f;
    float slide = 0.0f;             // -1 / +1: parts to the left / right when the film opens up
    bool  forest = false;
    GLuint list = 0;
    Color listTint{-1, -1, -1, -1};

    float at(float x) const
    {
        float fi = (x - x0) / step;
        fi = clampf(fi, 0.0f, (float)y.size() - 1.001f);
        const int i = (int)fi;
        return y[i] + (y[i + 1] - y[i]) * (fi - i);
    }
};

std::vector<Range> ranges;           // far -> near
int idxPlateauAfter = 0;             // the plateau is drawn after ranges[0 .. idxPlateauAfter-1]

Color hazed(const Color& c, const Range& r) { return mix(c, r.haze, r.hazeK); }

Range makeRange(float f, float x0, float x1, float base, const std::vector<MPeak>& peaks, float detail, uint32_t seed, float floorY = -9.0f)
{
    Range r;
    r.f = f; r.x0 = x0; r.base = base;
    r.step = f >= 0.8f ? 0.004f : 0.006f;
    const int n = (int)((x1 - x0) / r.step) + 2;
    // midpoint-displacement detail, resampled over the whole range
    const std::vector<float> mpd = cg::midpointDisplacement(11, 0.80f, seed);
    // every peak gets two lower "shoulder" peaks, so the outline is not a simple cone
    std::vector<MPeak> all = peaks;
    for (const MPeak& p : peaks)
    {
        const float k = cg::hashU(seed + (uint32_t)(p.x * 1000.0f + 5000.0f));
        all.push_back(MPeak{p.x - p.w * (0.28f + 0.12f * k), p.h * (0.70f + 0.10f * k), p.w * 0.45f});
        all.push_back(MPeak{p.x + p.w * (0.30f + 0.10f * k), p.h * (0.62f + 0.12f * k), p.w * 0.40f});
    }
    float maxH = 0.0f;
    for (const MPeak& p : peaks) maxH = std::max(maxH, p.h);
    r.y.resize(n);
    for (int i = 0; i < n; i++)
    {
        const float x = x0 + i * r.step;
        float env = 0.0f;
        for (const MPeak& p : all)
        {
            const float d = std::fabs(x - p.x) / p.w;
            if (d < 1.0f) env = std::max(env, p.h * std::pow(1.0f - d, 1.45f));
        }
        const float u = (x - x0) / (x1 - x0) * (mpd.size() - 1);
        const int   k = std::min((int)u, (int)mpd.size() - 2);
        const float m = mpd[k] + (mpd[k + 1] - mpd[k]) * (u - k);
        const float rough = detail * (0.25f + 0.75f * env / std::max(0.01f, maxH));   // rockier toward the tops
        r.y[i] = base + 0.20f + env + m * rough + 0.03f * (cg::fbm(x * 3.0f, seed + 7u, 4) - 0.5f);
        r.y[i] = std::max(r.y[i], floorY);                       // no deep gaps: nothing behind shows through
    }
    // summits: the highest sample around every peak definition
    for (const MPeak& p : peaks)
    {
        int best = -1;
        for (int i = 0; i < n; i++)
        {
            const float x = x0 + i * r.step;
            if (std::fabs(x - p.x) > p.w * 0.25f) continue;
            if (best < 0 || r.y[i] > r.y[best]) best = i;
        }
        if (best < 0) continue;
        Summit s;
        s.i = best; s.x = x0 + best * r.step; s.y = r.y[best];
        s.lean = 0.10f + 0.25f * cg::hashU(seed + best);
        s.seed = seed + 97u * best;
        r.summits.push_back(s);
    }
    std::sort(r.summits.begin(), r.summits.end(), [](const Summit& a, const Summit& b) { return a.i < b.i; });
    for (size_t k = 0; k < r.summits.size(); k++)      // lowest point between this summit and the next
    {
        const int a = r.summits[k].i, b = k + 1 < r.summits.size() ? r.summits[k + 1].i : n - 1;
        int lo = a;
        for (int i = a; i <= b; i++) if (r.y[i] < r.y[lo]) lo = i;
        r.summits[k].valley = r.y[lo];
        r.summits[k].valleyI = lo;
    }
    return r;
}

// the geometry of one range (compiled once into a display list)
void buildRangeGeometry(const Range& r)
{
    const int n = (int)r.y.size();
    // 1. body: vertical gradient from the lit colour at the crest to hazy colour at the foot
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i < n; i++)
    {
        const float x = r.x0 + i * r.step;
        const float h = clampf((r.y[i] - r.base) / 1.0f, 0.0f, 1.0f);
        vtx(hazed(mix(r.bottom, r.lit, 0.35f + 0.65f * h), r), x, r.y[i]);
        vtx(hazed(r.bottom, r), x, r.base);
    }
    glEnd();

    // 2. forest belt on the near ranges: pine silhouettes on the lower slopes
    if (r.forest)
    {
        const Color dark = hazed(rgb(0.10f, 0.26f, 0.30f), r), mid = hazed(rgb(0.15f, 0.36f, 0.36f), r);
        for (int k = 0; k < 420; k++)
        {
            const float x = r.x0 + (n - 1) * r.step * cg::hashU(k * 7u + 3u);
            const float top = r.at(x);
            const float y = -0.62f - 0.45f * std::pow(cg::hashU(k * 13u + 5u), 0.6f);   // a band at the foot
            if (y > top - 0.10f || y < r.base + 0.05f) continue;
            const float h = 0.04f + 0.03f * cg::hashU(k * 17u + 1u) + 0.06f * (-0.62f - y);
            setColor(k & 1 ? dark : mid);
            tri(x - h * 0.32f, y, x + h * 0.32f, y, x, y + h);
            tri(x - h * 0.24f, y + h * 0.35f, x + h * 0.24f, y + h * 0.35f, x, y + h * 1.25f);
        }
    }

    // 2b. rock texture: short strata strokes on the faces, more of them near the crest
    if (r.f > 0.3f)
    {
        glLineWidth(1.2f);
        glBegin(GL_LINES);
        const int strokes = r.f > 0.8f ? 900 : 400;
        for (int k = 0; k < strokes; k++)
        {
            const float x = r.x0 + (n - 1) * r.step * cg::hashU(k * 31u + 9u);
            const float top = r.at(x);
            const float depth = 0.02f + 0.55f * std::pow(cg::hashU(k * 19u + 4u), 1.5f);
            const float y = top - depth;
            if (y < -0.55f) continue;
            const float L = 0.012f + 0.02f * cg::hashU(k * 23u + 2u), slope = 0.15f + 0.6f * cg::hashU(k * 29u);
            const bool lightSide = cg::hashU(k * 37u) > 0.5f;
            Color c = hazed(lightSide ? mix(r.lit, rgb(1, 1, 1), 0.3f) : mix(r.shadow, r.lit, 0.4f), r);
            c.a = 0.20f;
            setColor(c);
            glVertex2f(x, y); glVertex2f(x + L, y - L * slope);
        }
        glEnd();
        glLineWidth(1.0f);
    }

    // 3. snow above a noisy snow line, with thin drips down the gullies
    glBegin(GL_QUAD_STRIP);
    bool open = false;
    for (int i = 0; i < n; i++)
    {
        const float x = r.x0 + i * r.step;
        const float drip = 0.035f * std::pow(cg::fbm(x * 30.0f, 77u, 3), 4.0f) * 4.0f;
        const float line = r.snowLine + 0.07f * (cg::fbm(x * 5.0f, 31u, 4) - 0.5f) + 0.02f * (cg::fbm(x * 22.0f, 41u, 3) - 0.5f) - drip;
        if (r.y[i] > line)
        {
            vtx(hazed(r.snow, r), x, r.y[i]);
            vtx(hazed(mix(r.snow, r.lit, 0.25f), r), x, std::min(line, r.y[i]));
            open = true;
        }
        else if (open)
        {
            vtx(hazed(r.snow, r), x, r.y[i]); vtx(hazed(r.snow, r), x, r.y[i]);
            glEnd(); glBegin(GL_QUAD_STRIP); open = false;
        }
    }
    glEnd();

    // 4. light / shadow spines: east side of every summit is in shadow (sun is in the west)
    for (const Summit& s : r.summits)
    {
        const float stopY = std::max(s.valley, s.y - (s.y - r.base) * 0.85f);
        const float dy = 0.006f;
        glBegin(GL_QUAD_STRIP);
        int ri = s.i;
        for (float y = s.y - 0.002f; y > stopY; y -= dy)
        {
            while (ri < s.valleyI && r.y[ri] > y) ri++;             // right flank at this height
            const float xr = r.x0 + ri * r.step;
            const float depth = s.y - y;
            const float xs = s.x + s.lean * depth + 0.06f * depth * (cg::fbm(depth * 9.0f, s.seed, 3) - 0.5f) * 2.0f;
            if (xs >= xr) continue;
            const float a = 0.62f * (1.0f - 0.55f * smoothstep(depth / std::max(0.05f, s.y - stopY)));
            Color c = hazed(r.shadow, r); c.a = a;
            vtx(c, xs, y); vtx(c, xr, y);
        }
        glEnd();
        // secondary ridges: thin lighter lines running down-left on the lit face
        for (int k = 0; k < 3; k++)
        {
            const float len = std::min(0.10f, (s.y - stopY) * (0.20f + 0.15f * cg::hashU(s.seed + k)));
            Color c = hazed(mix(r.lit, rgb(1, 1, 1), 0.45f), r); c.a = 0.35f;
            setColor(c);
            glLineWidth(1.5f);
            glBegin(GL_LINE_STRIP);
            for (int j = 0; j <= 12; j++)
            {
                const float d = len * j / 12.0f;
                const float x = s.x - (0.10f + 0.45f * cg::hashU(s.seed + 11u * k)) * d * (0.8f + 0.3f * k) +
                                0.02f * (cg::fbm(d * 14.0f, s.seed + k, 2) - 0.5f);
                const float y = s.y - d - 0.01f * k;
                if (y > r.at(x) - 0.003f) continue;
                glVertex2f(x, y);
            }
            glEnd();
            glLineWidth(1.0f);
        }
    }

    // 5. warm rim light along the sun-facing (rising) flanks of the crest
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    for (int i = 1; i < n; i++)
    {
        if (r.y[i] <= r.y[i - 1] || r.y[i] < r.snowLine - 0.25f) continue;
        Color c = hazed(r.rim, r); c.a = 0.55f;
        setColor(c);
        glVertex2f(r.x0 + (i - 1) * r.step, r.y[i - 1] - 0.002f);
        glVertex2f(r.x0 + i * r.step, r.y[i] - 0.002f);
    }
    glEnd();
    glLineWidth(1.0f);
}

void drawRangeCached(Range& r)
{
    const Color t = gfx::tintColor;
    const bool same = std::fabs(t.r - r.listTint.r) + std::fabs(t.g - r.listTint.g) + std::fabs(t.b - r.listTint.b) < 0.004f;
    if (!r.list || !same)
    {
        if (!r.list) r.list = glGenLists(1);
        glNewList(r.list, GL_COMPILE);
        buildRangeGeometry(r);
        glEndList();
        r.listTint = t;
    }
    glCallList(r.list);
}

// ----------------------------------------------------------------------------
// Camera: key shots joined by a Kochanek-Bartels spline, then zoom-to-point
// ----------------------------------------------------------------------------
struct Cam { float cx, cy, lz; };                       // centre (layer f = 1 units) + log zoom
struct Key { float t; Cam c; };
std::vector<Key> keys;
const float TENSION = 0.55f;
const cg::V2 TARGET(-0.21f, -0.255f);                  // between the lake and the cottage (plateau layer)
const float  PLATEAU_F = 0.55f;
const float  END_LAYER_ZOOM = 9.5f;

Key keyFor(float t, float f, float px, float py, float zoom)
{   // centre the point (px,py) of the layer with depth f:  f*C = P
    return Key{t, Cam{px / f, py / f, std::log(zoom)}};
}

void buildKeys()
{
    keys.clear();
    keys.push_back(keyFor(0.0f, 1.0f, 0.0f, 0.0f, 1.0f));              // the title view
    keys.push_back(keyFor(3.1f, 1.0f, -0.80f, 0.33f, 2.6f));           // peak 1: left front summit
    keys.push_back(keyFor(6.2f, 0.85f, -0.30f, 0.58f, 3.0f));          // peak 2: left giant
    keys.push_back(keyFor(9.3f, 0.85f, 0.43f, 0.62f, 3.1f));           // peak 3: right giant
    keys.push_back(keyFor(12.4f, 1.0f, 1.04f, 0.34f, 2.6f));           // peak 4: right front summit
    keys.push_back(keyFor(15.6f, 1.0f, 0.0f, 0.02f, 1.0f));            // back to the whole range
    keys.push_back(keyFor(19.2f, PLATEAU_F, 0.0f, -0.06f, 1.10f));     // framing the revealed village
}

Cam splineAt(float t)
{
    if (t <= keys.front().t) return keys.front().c;
    if (t >= keys.back().t)  return keys.back().c;
    size_t i = 0;
    while (i + 1 < keys.size() && keys[i + 1].t < t) i++;
    const Key& a = keys[i];
    const Key& b = keys[i + 1];
    const Key& p = keys[i > 0 ? i - 1 : i];
    const Key& q = keys[i + 2 < keys.size() ? i + 2 : i + 1];
    const float u = (t - a.t) / (b.t - a.t);
    auto comp = [&](float Cam::*m)
    {
        const float m0 = cg::kbTangent(p.c.*m, a.c.*m, b.c.*m, TENSION);
        const float m1 = cg::kbTangent(a.c.*m, b.c.*m, q.c.*m, TENSION);
        return cg::hermite(a.c.*m, m0, b.c.*m, m1, u);
    };
    return Cam{comp(&Cam::cx), comp(&Cam::cy), comp(&Cam::lz)};
}

Cam cameraAt(float t)
{
    const float j0 = (float)timeline::JOURNEY_START;
    if (t < j0) return splineAt(t);
    // zoom-to-point: log-zoom eases from the framing shot to the close-up, while the target's
    // screen position slides to the centre:  S(s) = S0 (1 - e(s)),  C = (T - S / Z^f) / f
    const Cam c0 = keys.back().c;
    const float s = clampf((t - j0) / (float)(timeline::GARDEN_START + 1.2 - timeline::JOURNEY_START), 0.0f, 1.0f);
    const float zEnd = std::log(END_LAYER_ZOOM) / PLATEAU_F;
    const float lz = lerpf(c0.lz, zEnd, smootherstep(s) * 0.85f + 0.15f * s * s);
    const float zf0 = std::exp(PLATEAU_F * c0.lz), zf = std::exp(PLATEAU_F * lz);
    const float S0x = (TARGET.x - PLATEAU_F * c0.cx) * zf0, S0y = (TARGET.y - PLATEAU_F * c0.cy) * zf0;
    const float k = 1.0f - smootherstep(std::min(1.0f, s * 1.5f));
    return Cam{(TARGET.x - S0x * k / zf) / PLATEAU_F, (TARGET.y - S0y * k / zf) / PLATEAU_F, lz};
}

Cam cam_;                                               // camera of the current frame

void pushLayer(float f, float extraX = 0.0f)
{
    const float zf = std::exp(f * cam_.lz);
    pushCamera(zf, 0.0f, 0.0f, zf * (extraX - f * cam_.cx), -zf * f * cam_.cy);
}

// ----------------------------------------------------------------------------
// Sky, sun, light shafts, cloud banks, birds
// ----------------------------------------------------------------------------
const float SUN_X = -1.05f, SUN_Y = 0.52f;

void drawSky(float t, bool still)
{
    const bool old = gfx::useTint;
    gfx::useTint = false;
    const float shiftY = -0.04f * cam_.cy;                  // the sky moves very little
    Stop st[] = {{-1.2f, rgb(0.98f, 0.88f, 0.78f)}, {-0.1f, rgb(0.93f, 0.88f, 0.92f)}, {0.35f, rgb(0.62f, 0.76f, 0.97f)},
                 {1.2f, rgb(0.20f, 0.40f, 0.86f)}};
    for (Stop& s : st) { s.c = theme::skyColor(s.c, s.y); s.y += shiftY; }
    vGradient(-WORLD_W - 0.1f, WORLD_W + 0.1f, st, 4);
    const float sx = SUN_X - 0.06f * cam_.cx, sy = SUN_Y + shiftY;
    if (theme::cur().sunAlpha > 0.01f)
    {
        glow(sx, sy, 1.25f, theme::sun(rgb(1.0f, 0.86f, 0.62f, 0.45f)), 48);
        glow(sx, sy, 0.38f, theme::sun(rgb(1.0f, 0.95f, 0.80f, 0.85f)), 40);
        setColor(theme::sun(rgb(1.0f, 0.98f, 0.88f)));
        circle(sx, sy, 0.075f, 40);
        // light shafts (additive wedges); they breathe slowly once the film runs
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        for (int k = 0; k < 9; k++)
        {
            const float a = -0.15f - 0.13f * k + (still ? 0.0f : 0.02f * std::sin(t * 0.3f + k));
            const float w = 0.025f + 0.02f * cg::hashU(k * 3u);
            const float L = 2.6f, al = 0.07f * theme::cur().sunAlpha;
            // the wedge is much larger than the screen: Sutherland-Hodgman clips it to the
            // (convex) view rectangle first, then each kept vertex gets its alpha from its
            // distance to the sun, so the fade is identical to the unclipped triangle
            const std::vector<cg::V2> wedge = {cg::V2(sx, sy), cg::V2(sx + std::cos(a - w) * L, sy + std::sin(a - w) * L), cg::V2(sx + std::cos(a + w) * L, sy + std::sin(a + w) * L)};
            static const std::vector<cg::V2> view = {cg::V2(-WORLD_W - 0.2f, -1.2f), cg::V2(WORLD_W + 0.2f, -1.2f), cg::V2(WORLD_W + 0.2f, 1.2f), cg::V2(-WORLD_W - 0.2f, 1.2f)};
            const std::vector<cg::V2> poly = cg::clipPolygonSH(wedge, view);
            if (poly.size() < 3) continue;
            glBegin(GL_TRIANGLE_FAN);
            for (const cg::V2& v : poly)
            {
                const float d = std::hypot(v.x - sx, v.y - sy) / L;
                glColor4f(1.0f, 0.93f, 0.75f, al * std::max(0.0f, 1.0f - d));
                glVertex2f(v.x, v.y);
            }
            glEnd();
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    theme::drawStarsAndMoon(sx, sy, 0.075f, still ? 0.0f : fract(t / 60.0f));
    gfx::useTint = old;
}

// a soft cloud bank: noise-shaped top edge, dense core, transparent bottom
void cloudBank(float x0, float x1, float y, float h, float drift, uint32_t seed, float alpha, const Color& c)
{
    const int n = 90;
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= n; i++)
    {
        const float x = lerpf(x0, x1, (float)i / n);
        const float top = y + h * (0.35f + 0.65f * cg::fbm((x - drift) * 2.2f, seed, 4));
        const float edge = smoothstep(0.0f, 0.12f, (x - x0)) * smoothstep(0.0f, 0.12f, (x1 - x));
        const float patch = smoothstep(0.35f, 0.75f, cg::fbm((x - drift * 0.5f) * 0.9f, seed + 3u, 3));
        Color a = c; a.a = alpha * edge * patch;
        Color b = c; b.a = 0.0f;
        vtx(a, x, top); vtx(b, x, y - h * 0.6f);
    }
    glEnd();
    glBegin(GL_QUAD_STRIP);                                 // feathered top edge
    for (int i = 0; i <= n; i++)
    {
        const float x = lerpf(x0, x1, (float)i / n);
        const float top = y + h * (0.35f + 0.65f * cg::fbm((x - drift) * 2.2f, seed, 4));
        const float edge = smoothstep(0.0f, 0.12f, (x - x0)) * smoothstep(0.0f, 0.12f, (x1 - x));
        const float patch = smoothstep(0.35f, 0.75f, cg::fbm((x - drift * 0.5f) * 0.9f, seed + 3u, 3));
        Color a = c; a.a = alpha * edge * patch; Color b = c; b.a = 0.0f;
        vtx(a, x, top); vtx(b, x, top + 0.035f);
    }
    glEnd();
}

void mistBand(float y, float h, float alpha, const Color& c)
{
    Color a = c; a.a = 0.0f;
    Color m = c; m.a = alpha;
    rectV(-6.0f, y - h, 6.0f, y, a, m);
    rectV(-6.0f, y, 6.0f, y + h, m, a);
}

// ----------------------------------------------------------------------------
// Water: Bezier ribbons with flowing streaks, analytic spray particles
// ----------------------------------------------------------------------------
struct Fall { cg::V2 p0, p1, p2, p3; float w0, w1; cg::ArcLength arc; };
Fall upperFall, cliffFall;
struct Stream { cg::V2 p0, p1, p2, p3; float w0, w1; cg::ArcLength arc; };
Stream outflow, mountainCascade;

template <class T> cg::V2 curveAt(const T& c, float t) { return cg::cubicBezier(c.p0, c.p1, c.p2, c.p3, t); }
template <class T> cg::V2 tangentAt(const T& c, float t) { return cg::cubicBezierTangent(c.p0, c.p1, c.p2, c.p3, t); }

template <class T>
void waterRibbon(const T& c, float t, float speed, const Color& body, const Color& streak, bool still)
{
    const int N = 48;
    // body
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= N; i++)
    {
        const float u = (float)i / N;
        const cg::V2 p = curveAt(c, u), d = tangentAt(c, u);
        const float l = std::max(1e-5f, cg::len(d)), w = lerpf(c.w0, c.w1, u);
        Color b = body; b.a *= 0.75f + 0.25f * u;
        vtx(b, p.x - d.y / l * w, p.y + d.x / l * w);
        vtx(b, p.x + d.y / l * w, p.y - d.x / l * w);
    }
    glEnd();
    // flowing streaks: dashes travel along the ARC LENGTH, so the speed is constant
    const float L = c.arc.total();
    glLineWidth(1.6f);
    glBegin(GL_LINES);
    for (int lane = 0; lane < 7; lane++)
    {
        const float off = (lane / 6.0f) * 1.6f - 0.8f;
        const float ph = cg::hashU(lane * 31u + 7u);
        const float dash = 0.035f + 0.03f * cg::hashU(lane * 5u);
        for (int k = 0; k < 10; k++)
        {
            const float s0 = fract(ph + k * 0.1f + (still ? 0.0f : t * speed / std::max(0.01f, L))) * L;
            const float s1 = std::min(L, s0 + dash);
            const float u0 = c.arc.paramAt(s0), u1 = c.arc.paramAt(s1);
            const cg::V2 a = curveAt(c, u0), b = curveAt(c, u1), d = tangentAt(c, u0);
            const float l = std::max(1e-5f, cg::len(d)), w = lerpf(c.w0, c.w1, u0) * off;
            Color s = streak; s.a *= 0.5f + 0.5f * cg::hashU(lane * 13u + k);
            setColor(s);
            glVertex2f(a.x - d.y / l * w, a.y + d.x / l * w);
            glVertex2f(b.x - d.y / l * w, b.y + d.x / l * w);
        }
    }
    glEnd();
    glLineWidth(1.0f);
}

// stateless particle system: particle k's position is a closed-form function of its age,
// so the spray never needs updating and can be evaluated at any time (scrubbing the film).
void spray(float x, float y, float spread, float t, int count, float scale, bool still)
{
    const bool old = gfx::useTint;
    for (int k = 0; k < count; k++)
    {
        const float life = 0.9f + 0.6f * cg::hashU(k * 7u + 1u);
        const float age = fract((still ? 0.37f : t) / life + cg::hashU(k * 3u + 2u)) * life;
        const float vx = (cg::hashU(k * 11u + 3u) - 0.5f) * spread, vy = 0.15f + 0.25f * cg::hashU(k * 5u + 4u);
        const float px = x + vx * age * scale, py = y + (vy * age - 0.25f * age * age) * scale;
        const float a = (1.0f - age / life) * 0.55f;
        setColor(rgb(1.0f, 1.0f, 1.0f, a));
        circle(px, py, (0.006f + 0.010f * age) * scale, 8);
    }
    gfx::useTint = old;
}

// ----------------------------------------------------------------------------
// Plateau with the village, the lake, the waterfalls and the cliff (layer f = 0.55)
// ----------------------------------------------------------------------------
const float LAKE_X = -0.47f, LAKE_Y = -0.27f, LAKE_RX = 0.33f, LAKE_RY = 0.08f;

float backHill(float x) { return -0.005f + 0.055f * cg::fbm(x * 1.4f + 3.0f, 501u, 4); }
float cliffEdge(float x) { return -0.465f + 0.025f * (cg::fbm(x * 3.0f, 909u, 4) - 0.5f); }

GLuint plateauList = 0;
Color  plateauTint{-1, -1, -1, -1};

void buildPlateauStatic()
{
    // back hills hide the feet of the mountains behind the village
    glBegin(GL_TRIANGLE_STRIP);
    for (float x = -2.6f; x <= 2.6f; x += 0.02f)
    {
        vtx(rgb(0.40f, 0.62f, 0.52f), x, backHill(x));
        vtx(rgb(0.36f, 0.58f, 0.44f), x, -0.30f);
    }
    glEnd();
    for (int k = 0; k < 160; k++)                                    // forest on the hill tops
    {
        const float x = -2.5f + 5.0f * cg::hashU(k * 9u + 1u);
        const float y = backHill(x) - 0.03f * cg::hashU(k * 5u + 2u);
        const float h = 0.03f + 0.02f * cg::hashU(k * 3u + 7u);
        setColor(k & 1 ? rgb(0.16f, 0.38f, 0.34f) : rgb(0.20f, 0.44f, 0.36f));
        tri(x - h * 0.3f, y - 0.01f, x + h * 0.3f, y - 0.01f, x, y + h);
    }
    // rocky spur on the left: the mountain stream falls over it into the lake
    {
        const float P[][2] = {{-1.55f, -0.32f}, {-1.48f, 0.00f}, {-1.30f, 0.05f}, {-1.10f, 0.065f}, {-0.94f, 0.07f},
                              {-0.80f, 0.11f}, {-0.70f, 0.04f}, {-0.63f, -0.12f}, {-0.60f, -0.25f}};
        const int n = 9;
        glBegin(GL_TRIANGLE_FAN);
        vtx(rgb(0.42f, 0.45f, 0.58f), -1.05f, -0.10f);
        for (int i = 0; i < n; i++) vtx(i < 3 ? rgb(0.55f, 0.58f, 0.74f) : rgb(0.48f, 0.52f, 0.68f), P[i][0], P[i][1]);
        glEnd();
        setColor(rgb(0.32f, 0.34f, 0.48f, 0.55f));                       // shadowed east face
        quad(-0.94f, 0.07f, -0.80f, 0.11f, -0.70f, -0.25f, -0.90f, -0.25f);
        setColor(rgb(0.36f, 0.58f, 0.40f));                              // grassy top
        glBegin(GL_TRIANGLE_STRIP);
        for (int i = 1; i < 7; i++) { vtx(rgb(0.42f, 0.66f, 0.40f), P[i][0], P[i][1] + 0.012f); vtx(rgb(0.30f, 0.52f, 0.36f), P[i][0], P[i][1] - 0.03f); }
        glEnd();
        for (int k = 0; k < 10; k++)                                   // boulders on the spur
        {
            const float x = -1.42f + 0.06f * k, y = 0.0f + 0.04f * cg::hashU(k * 7u);
            setColor(rgb(0.50f, 0.52f, 0.64f)); ellipse(x, y, 0.035f, 0.02f, 14);
        }
    }
    // meadow of the plateau
    glBegin(GL_TRIANGLE_STRIP);
    for (float x = -2.6f; x <= 2.6f; x += 0.02f)
    {
        vtx(rgb(0.60f, 0.84f, 0.42f), x, -0.03f + 0.02f * cg::fbm(x * 2.0f, 77u, 3));
        vtx(rgb(0.46f, 0.76f, 0.34f), x, cliffEdge(x));
    }
    glEnd();
    setColor(rgb(0.72f, 0.90f, 0.50f, 0.35f));                            // sunlit patches
    ellipse(0.35f, -0.20f, 0.55f, 0.09f, 40);
    ellipse(-0.95f, -0.33f, 0.40f, 0.06f, 40);
    // dirt path from the village to the lake shore
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= 30; i++)
    {
        const float u = i / 30.0f;
        const cg::V2 p = cg::cubicBezier(cg::V2(0.95f, -0.42f), cg::V2(0.55f, -0.38f), cg::V2(0.15f, -0.36f), cg::V2(-0.15f, -0.33f), u);
        const float w = 0.016f - 0.008f * u;
        vtx(rgb(0.86f, 0.74f, 0.52f), p.x, p.y + w);
        vtx(rgb(0.80f, 0.66f, 0.46f), p.x, p.y - w);
    }
    glEnd();
    // the cliff below the plateau: the village is HIGH above the valley
    glBegin(GL_TRIANGLE_STRIP);
    for (float x = -2.6f; x <= 2.6f; x += 0.02f)
    {
        vtx(rgb(0.52f, 0.50f, 0.60f), x, cliffEdge(x));
        vtx(rgb(0.36f, 0.38f, 0.52f), x, -1.40f);
    }
    glEnd();
    glLineWidth(1.5f);
    for (int k = 0; k < 9; k++)                                        // rock strata
    {
        setColor(rgb(0.30f, 0.30f, 0.44f, 0.45f));
        glBegin(GL_LINE_STRIP);
        for (float x = -2.6f; x <= 2.6f; x += 0.04f)
            glVertex2f(x, cliffEdge(x) - 0.06f - 0.075f * k + 0.02f * (cg::fbm(x * 2.0f + k, 300u + k, 3) - 0.5f));
        glEnd();
    }
    glLineWidth(1.0f);
    for (float x = -2.5f; x <= 2.5f; x += 0.05f)                       // grass lip over the cliff edge
    {
        setColor(rgb(0.34f, 0.62f, 0.32f));
        tri(x - 0.03f, cliffEdge(x) + 0.005f, x + 0.03f, cliffEdge(x) + 0.005f, x + 0.005f, cliffEdge(x) - 0.025f - 0.02f * cg::hashU((uint32_t)(x * 100 + 500)));
    }
    // trees around the meadow
    const float TX[] = {-1.25f, -0.98f, -0.92f, 1.05f, 1.22f, 1.40f, 1.58f, -1.60f, 0.98f};
    const float TY[] = {-0.36f, -0.40f, -0.30f, -0.30f, -0.40f, -0.33f, -0.42f, -0.40f, -0.12f};
    for (int k = 0; k < 9; k++)
    {
        if (k % 3 == 0) drawPine(TX[k], TY[k], 0.20f, 0.0f, 1.0f);
        else drawRoundTree(TX[k], TY[k], 0.15f, 0.0f, rgb(0.18f, 0.48f, 0.26f), rgb(0.36f, 0.68f, 0.32f), k == 4);
    }
}

void drawLake(float t, bool still)
{
    // shore, water body, sky reflection band
    setColor(rgb(0.66f, 0.62f, 0.50f));
    ellipse(LAKE_X, LAKE_Y - 0.004f, LAKE_RX + 0.025f, LAKE_RY + 0.016f, 72);
    glBegin(GL_TRIANGLE_FAN);
    vtx(rgb(0.30f, 0.60f, 0.86f), LAKE_X, LAKE_Y + 0.01f);
    for (int i = 0; i <= 72; i++)
    {
        const float a = TAU * i / 72.0f, back = std::sin(a);
        vtx(mix(rgb(0.20f, 0.46f, 0.74f), rgb(0.62f, 0.84f, 0.98f), 0.5f + 0.5f * back),
            LAKE_X + std::cos(a) * LAKE_RX, LAKE_Y + std::sin(a) * LAKE_RY);
    }
    glEnd();
    setColor(rgb(0.85f, 0.95f, 1.0f, 0.35f));
    ellipse(LAKE_X + 0.06f, LAKE_Y + 0.025f, LAKE_RX * 0.55f, LAKE_RY * 0.18f, 40);
    // rings spreading from the plunge pool (periodic, kept inside the lake ellipse)
    const float px = -0.635f, py = -0.215f;
    glLineWidth(1.5f);
    for (int k = 0; k < 4; k++)
    {
        const float p = fract((still ? 0.3f : t) * 0.45f + k * 0.25f);
        const float rr = 0.02f + 0.16f * p;
        setColor(rgb(1, 1, 1, 0.55f * (1.0f - p)));
        glBegin(GL_LINE_STRIP);
        for (int i = 0; i <= 40; i++)
        {
            const float a = TAU * i / 40.0f;
            const float x = px + std::cos(a) * rr, y = py + std::sin(a) * rr * 0.28f;
            const float dx = (x - LAKE_X) / LAKE_RX, dy = (y - LAKE_Y) / LAKE_RY;
            if (dx * dx + dy * dy > 0.92f) { glEnd(); glBegin(GL_LINE_STRIP); continue; }
            glVertex2f(x, y);
        }
        glEnd();
    }
    glLineWidth(1.0f);
    // glints that twinkle on the water
    for (int k = 0; k < 14; k++)
    {
        const float a = TAU * cg::hashU(k * 5u + 1u), r = std::sqrt(cg::hashU(k * 9u + 2u)) * 0.85f;
        const float x = LAKE_X + std::cos(a) * LAKE_RX * r, y = LAKE_Y + std::sin(a) * LAKE_RY * r;
        const float tw = still ? 0.6f : 0.5f + 0.5f * std::sin(t * 3.0f + k * 1.9f);
        setColor(rgb(1.0f, 1.0f, 1.0f, 0.75f * tw));
        rect(x - 0.012f * tw, y - 0.0015f, x + 0.012f * tw, y + 0.0015f);
    }
    // lily pads
    for (int k = 0; k < 5; k++)
    {
        const float x = LAKE_X - 0.18f + 0.09f * k, y = LAKE_Y - 0.035f + 0.02f * (k & 1);
        setColor(rgb(0.26f, 0.62f, 0.32f));
        ellipse(x, y, 0.025f, 0.008f, 14);
        if (k == 2) { setColor(rgb(1.0f, 0.62f, 0.78f)); circle(x, y + 0.006f, 0.007f, 10); }
    }
}

void drawPlateau(float t, bool still)
{
    pushLayer(PLATEAU_F);
    const Color tnow = gfx::tintColor;
    const bool same = std::fabs(tnow.r - plateauTint.r) + std::fabs(tnow.g - plateauTint.g) + std::fabs(tnow.b - plateauTint.b) < 0.004f;
    if (!plateauList || !same)
    {
        if (!plateauList) plateauList = glGenLists(1);
        glNewList(plateauList, GL_COMPILE);
        buildPlateauStatic();
        glEndList();
        plateauTint = tnow;
    }
    glCallList(plateauList);
    mistBand(-0.92f, 0.16f, 0.75f, rgb(0.93f, 0.95f, 1.0f));

    // water flowing out of the lake and over the cliff edge into the misty valley
    waterRibbon(outflow, t, 0.18f, rgb(0.55f, 0.80f, 0.98f, 0.95f), rgb(1, 1, 1, 0.8f), still);
    drawLake(t, still);
    waterRibbon(cliffFall, t, 0.55f, rgb(0.82f, 0.92f, 1.0f, 0.88f), rgb(1, 1, 1, 0.9f), still);
    spray(cliffFall.p3.x, cliffFall.p3.y, 0.9f, t, 26, 0.6f, still);
    // the mountain stream: falls from the rocky spur into the lake
    waterRibbon(upperFall, t, 0.60f, rgb(0.84f, 0.94f, 1.0f, 0.92f), rgb(1, 1, 1, 0.95f), still);
    spray(upperFall.p3.x, upperFall.p3.y + 0.005f, 0.7f, t, 30, 0.45f, still);
    glow(upperFall.p3.x, upperFall.p3.y + 0.02f, 0.07f, rgb(1, 1, 1, 0.35f), 24);

    // the cottage beside the lake (the zoom ends here and becomes the garden cottage)
    drawHouse(-0.05f, -0.29f, 0.105f, 1, true, still ? 0.0 : (double)t);
    drawRoundTree(-0.68f, -0.34f, 0.13f, still ? 0.0f : 0.5f * std::sin(t * 1.3f), rgb(0.16f, 0.46f, 0.24f), rgb(0.34f, 0.66f, 0.30f));
    popCamera();

    // the village itself: the original village items, placed on the plateau
    {
        const float zf = std::exp(PLATEAU_F * cam_.lz);
        const float ox = 0.46f, oy = -0.165f, k = 0.88f;
        pushCamera(zf * k, 0.0f, 0.0f, zf * (ox - PLATEAU_F * cam_.cx), zf * (oy - PLATEAU_F * cam_.cy));
        drawVillage(still ? 0.0 : (double)t);
        popCamera();
    }
}

// ----------------------------------------------------------------------------
// Birds (a small flock crossing during the peak shots)
// ----------------------------------------------------------------------------
void drawFlock(float t)
{
    if (t < 2.0f || t > 16.0f) return;
    const float u = (t - 2.0f) / 14.0f;
    for (int k = 0; k < 7; k++)
    {
        const float x = -2.2f + 4.6f * u + 0.07f * (k % 4) - 0.03f * k;
        const float y = 0.42f + 0.05f * std::sin(u * 6.0f + k) + 0.035f * (k % 3) - 0.02f * k;
        drawBird(x, y, 0.022f, std::sin(t * 9.0f + k * 1.3f), rgb(0.18f, 0.20f, 0.32f));
    }
}

}   // namespace

// ----------------------------------------------------------------------------
// public
// ----------------------------------------------------------------------------
void initMountain()
{
    initVillage();
    buildKeys();
    ranges.clear();
    auto mk = [](float f, float x0, float x1, float base, std::vector<MPeak> pk, float detail, uint32_t seed, float floorY = -9.0f)
    {
        for (MPeak& p : pk) p.h = p.h - (base + 0.20f);              // MPeak.h is given as the summit height
        return makeRange(f, x0, x1, base, pk, detail, seed, floorY);
    };
    const Color sky = rgb(0.86f, 0.90f, 0.99f);

    Range r0 = mk(0.10f, -2.6f, 2.6f, -0.30f, {{-1.6f, 0.36f, 0.6f}, {-0.9f, 0.44f, 0.55f}, {-0.2f, 0.40f, 0.5f}, {0.5f, 0.47f, 0.55f},
                                               {1.2f, 0.39f, 0.5f}, {1.9f, 0.45f, 0.6f}}, 0.07f, 11u);
    r0.lit = rgb(0.74f, 0.79f, 0.97f); r0.shadow = rgb(0.56f, 0.60f, 0.88f); r0.bottom = rgb(0.82f, 0.86f, 0.98f);
    r0.snow = rgb(0.97f, 0.97f, 1.0f); r0.rim = rgb(1.0f, 0.92f, 0.85f); r0.haze = sky; r0.hazeK = 0.40f; r0.snowLine = 0.34f;
    Range r1 = mk(0.22f, -2.6f, 2.6f, -0.35f, {{-1.9f, 0.34f, 0.6f}, {-1.25f, 0.40f, 0.55f}, {-0.55f, 0.33f, 0.5f}, {0.15f, 0.42f, 0.55f},
                                                {0.85f, 0.34f, 0.5f}, {1.55f, 0.41f, 0.6f}}, 0.08f, 23u);
    r1.lit = rgb(0.58f, 0.68f, 0.95f); r1.shadow = rgb(0.38f, 0.44f, 0.80f); r1.bottom = rgb(0.66f, 0.76f, 0.94f);
    r1.snow = rgb(0.96f, 0.97f, 1.0f); r1.rim = rgb(1.0f, 0.90f, 0.80f); r1.haze = sky; r1.hazeK = 0.25f; r1.snowLine = 0.29f;
    Range r2 = mk(0.40f, -2.6f, 2.6f, -0.45f, {{-1.7f, 0.30f, 0.7f}, {-0.95f, 0.44f, 0.65f}, {-0.05f, 0.58f, 0.75f}, {0.80f, 0.46f, 0.65f},
                                                {1.6f, 0.36f, 0.7f}}, 0.11f, 37u);
    r2.lit = rgb(0.46f, 0.58f, 0.92f); r2.shadow = rgb(0.22f, 0.28f, 0.62f); r2.bottom = rgb(0.38f, 0.58f, 0.70f);
    r2.snow = rgb(0.97f, 0.97f, 1.0f); r2.rim = rgb(1.0f, 0.88f, 0.74f); r2.haze = sky; r2.hazeK = 0.12f; r2.snowLine = 0.27f;
    idxPlateauAfter = 3;
    // the great mountains that part: back giants (f = 0.85) and front massifs (f = 1)
    Range r3 = mk(0.85f, -3.2f, 0.9f, -1.30f, {{-1.95f, 0.40f, 1.5f}, {-1.05f, 0.46f, 1.4f}, {-0.30f, 0.58f, 1.5f}, {0.25f, 0.18f, 1.1f}}, 0.14f, 51u);
    r3.slide = -1.0f;
    Range r4 = mk(0.85f, -0.9f, 3.2f, -1.30f, {{-0.25f, 0.20f, 1.1f}, {0.43f, 0.62f, 1.5f}, {1.25f, 0.48f, 1.4f}, {2.0f, 0.42f, 1.5f}}, 0.14f, 63u);
    r4.slide = 1.0f;
    for (Range* r : {&r3, &r4})
    {
        r->lit = rgb(0.50f, 0.60f, 0.94f); r->shadow = rgb(0.16f, 0.18f, 0.50f); r->bottom = rgb(0.30f, 0.52f, 0.58f);
        r->snow = rgb(1.0f, 0.98f, 0.96f); r->rim = rgb(1.0f, 0.86f, 0.70f); r->haze = sky; r->hazeK = 0.05f; r->snowLine = 0.24f;
    }
    Range r5 = mk(1.0f, -3.4f, 0.75f, -1.30f, {{-1.55f, 0.22f, 1.4f}, {-0.80f, 0.33f, 1.5f}, {-0.22f, 0.16f, 1.2f}, {0.05f, 0.07f, 1.0f}}, 0.13f, 71u);
    r5.slide = -1.0f;
    Range r6 = mk(1.0f, -0.75f, 3.4f, -1.30f, {{-0.05f, 0.07f, 1.0f}, {0.28f, 0.15f, 1.2f}, {1.04f, 0.34f, 1.5f}, {1.72f, 0.23f, 1.4f}}, 0.13f, 83u);
    r6.slide = 1.0f;
    for (Range* r : {&r5, &r6})
    {
        r->lit = rgb(0.40f, 0.54f, 0.86f); r->shadow = rgb(0.10f, 0.14f, 0.40f); r->bottom = rgb(0.18f, 0.46f, 0.36f);
        r->snow = rgb(1.0f, 0.98f, 0.95f); r->rim = rgb(1.0f, 0.86f, 0.66f); r->haze = sky; r->hazeK = 0.0f; r->snowLine = 0.14f;
        r->forest = true;
    }
    for (Range* r : {&r0, &r1, &r2, &r3, &r4, &r5, &r6}) ranges.push_back(*r);

    // water curves (plateau layer)
    upperFall = Fall{cg::V2(-0.795f, 0.105f), cg::V2(-0.758f, 0.09f), cg::V2(-0.665f, -0.05f), cg::V2(-0.635f, -0.212f), 0.016f, 0.042f, {}};
    upperFall.arc.build([](float u) { return cg::cubicBezier(upperFall.p0, upperFall.p1, upperFall.p2, upperFall.p3, u); });
    cliffFall = Fall{cg::V2(-0.335f, -0.455f), cg::V2(-0.33f, -0.52f), cg::V2(-0.31f, -0.70f), cg::V2(-0.30f, -0.90f), 0.016f, 0.05f, {}};
    cliffFall.arc.build([](float u) { return cg::cubicBezier(cliffFall.p0, cliffFall.p1, cliffFall.p2, cliffFall.p3, u); });
    outflow = Stream{cg::V2(-0.40f, -0.335f), cg::V2(-0.37f, -0.37f), cg::V2(-0.35f, -0.41f), cg::V2(-0.335f, -0.458f), 0.011f, 0.016f, {}};
    outflow.arc.build([](float u) { return cg::cubicBezier(outflow.p0, outflow.p1, outflow.p2, outflow.p3, u); });
    // a cascade high up on the mountain behind the lake (range r2, f = 0.40)
    mountainCascade = Stream{cg::V2(-0.98f, 0.30f), cg::V2(-0.95f, 0.22f), cg::V2(-0.99f, 0.12f), cg::V2(-0.94f, 0.0f), 0.006f, 0.012f, {}};
    mountainCascade.arc.build([](float u) { return cg::cubicBezier(mountainCascade.p0, mountainCascade.p1, mountainCascade.p2, mountainCascade.p3, u); });
}

void drawMountainScene(const AnimState& s)
{
    const float t = (float)s.time;
    const bool still = s.waiting;                       // title screen: nothing moves
    cam_ = cameraAt(t);

    drawSky(t, still);
    const float drift = still ? 0.0f : t * 0.02f;

    for (size_t i = 0; i < ranges.size(); i++)
    {
        Range& r = ranges[i];
        if ((int)i == idxPlateauAfter)
        {
            // mist where the far ranges meet the plateau, then the plateau itself
            pushLayer(0.45f);
            mistBand(-0.05f, 0.12f, 0.30f, rgb(0.92f, 0.94f, 1.0f));
            popCamera();
            // During the peak shots the camera looks OVER the giants, so with parallax the plateau
            // would peek through; the village must only appear when the mountains part, so it is
            // drawn from the wide shot on (the closed mountains still cover it completely there).
            if (s.open > 0.0f) drawPlateau(t, still);
        }
        const float part = r.slide * s.open * (r.f >= 0.99f ? 1.80f : 1.55f);
        pushLayer(r.f, part);
        drawRangeCached(r);
        if (i == 2)                                     // stream high on the far mountain + its cloud belt
        {
            waterRibbon(mountainCascade, t, 0.25f, rgb(0.90f, 0.95f, 1.0f, 0.85f), rgb(1, 1, 1, 0.9f), still);
            cloudBank(-2.4f, 2.4f, 0.08f, 0.08f, drift * 1.5f, 5u, 0.75f, rgb(1.0f, 1.0f, 1.0f));
        }
        if (i == 0 || i == 1) mistBand(-0.12f + 0.05f * i, 0.12f, 0.18f, rgb(0.90f, 0.93f, 1.0f));
        if (r.f > 0.8f && r.f < 0.9f && r.slide < 0)  // clouds wrapping the giants
            cloudBank(-3.0f, 0.8f, 0.20f, 0.07f, drift * 2.0f, 9u, 0.50f, rgb(1.0f, 1.0f, 1.0f));
        if (r.f > 0.8f && r.f < 0.9f && r.slide > 0)
            cloudBank(-0.8f, 3.0f, 0.24f, 0.07f, drift * 2.0f + 1.3f, 13u, 0.50f, rgb(1.0f, 1.0f, 1.0f));
        popCamera();
        if (i == 4)
        {
            pushLayer(0.9f);
            drawFlock(t);
            popCamera();
        }
    }
    // foreground wisps drifting across the front massifs
    pushLayer(1.15f);
    cloudBank(-3.0f, 3.0f, -0.62f, 0.10f, drift * 3.0f + 2.0f, 17u, 0.55f, rgb(1.0f, 1.0f, 1.0f));
    popCamera();

    // ---- cinematic grade: warm light from the sun side + soft vignette (screen space) ----
    {
        const bool old = gfx::useTint;
        gfx::useTint = false;
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glow(SUN_X, SUN_Y, 1.6f, theme::sun(rgb(1.0f, 0.80f, 0.55f, 0.07f)), 48);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        const float W = WORLD_W + 0.05f;
        for (int k = 0; k < 2; k++)
        {
            const float sx = k ? 1.0f : -1.0f;
            glBegin(GL_QUADS);
            glColor4f(0.02f, 0.03f, 0.10f, 0.0f);  glVertex2f(sx * (W - 0.55f), -1.0f); glVertex2f(sx * (W - 0.55f), 1.0f);
            glColor4f(0.02f, 0.03f, 0.10f, 0.28f); glVertex2f(sx * W, 1.0f); glVertex2f(sx * W, -1.0f);
            glEnd();
        }
        glBegin(GL_QUADS);
        glColor4f(0.02f, 0.03f, 0.10f, 0.30f); glVertex2f(-W, -1.0f); glVertex2f(W, -1.0f);
        glColor4f(0.02f, 0.03f, 0.10f, 0.0f);  glVertex2f(W, -0.55f); glVertex2f(-W, -0.55f);
        glEnd();
        gfx::useTint = old;
    }

    // ---- title: the logo, then the prompt (both still while waiting) ----
    if (s.logoFade > 0.001f)
    {
        const bool old = gfx::useTint;
        gfx::useTint = false;
        const float out = 1.0f - s.logoFade;
        // a soft dark vignette behind the logo for contrast
        glow(0.0f, 0.22f, 1.5f, rgb(0.05f, 0.06f, 0.18f, 0.32f * s.logoFade), 48);
        font::drawLogo(0.0f, 0.28f, s.titleT, s.logoFade, 0.22f * out * out, out);
        const float pa = smoothstep((s.titleT - 3.0f) / 0.6f) * s.logoFade * (s.waiting ? 1.0f : 0.0f);
        if (pa > 0.0f)
        {
            font::draw(0.0f, -0.50f, 0.038f, rgb(1.0f, 1.0f, 1.0f, pa), "Click anywhere to begin", 1, true);
            font::draw(0.0f, -0.585f, 0.022f, rgb(0.85f, 0.90f, 1.0f, 0.8f * pa), "A journey through the mountains", 1, true);
        }
        gfx::useTint = old;
    }
}
