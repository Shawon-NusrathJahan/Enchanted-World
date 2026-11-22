// gfx.h  -  shared drawing helpers (header only)
//
// Everything here is plain OpenGL 2.1 immediate mode, matching your existing
// project. The world is 2D:  x = -WORLD_W .. +WORLD_W  (16:9),  y = -1 .. +1.

#ifndef GFX_H
#define GFX_H

#include <GL/glew.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace gfx
{

constexpr float PI      = 3.14159265358979f;
constexpr float TAU     = 6.28318530717958f;
constexpr float WORLD_W = 16.0f / 9.0f;

// ----------------------------------------------------------------------------
// Colours and small math helpers
// ----------------------------------------------------------------------------
struct Color { float r, g, b, a; };

inline Color rgb(float r, float g, float b, float a = 1.0f) { return Color{r, g, b, a}; }

// Time-of-day tint. Every scene colour goes through paint(), which multiplies
// it by tintColor (set once per frame from theme.cpp). Sky and UI switch the
// tint off with useTint = false.
inline bool  useTint   = true;
inline Color tintColor = Color{1.0f, 1.0f, 1.0f, 1.0f};
inline void  paint(float r, float g, float b, float a)
{
    if (useTint) glColor4f(r * tintColor.r, g * tintColor.g, b * tintColor.b, a);
    else         glColor4f(r, g, b, a);
}

inline Color mix(const Color& a, const Color& b, float t)
{
    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                 a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
inline Color shade(const Color& c, float k)      { return Color{c.r * k, c.g * k, c.b * k, c.a}; }
inline Color withAlpha(const Color& c, float a)  { return Color{c.r, c.g, c.b, a}; }
inline void  setColor(const Color& c)            { paint(c.r, c.g, c.b, c.a); }
inline void  vtx(const Color& c, float x, float y) { paint(c.r, c.g, c.b, c.a); glVertex2f(x, y); }

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t)    { return a + (b - a) * t; }
inline float fract(float x)                      { return x - std::floor(x); }

// Smoothstep  E(u) = 3u^2 - 2u^3   (roadmap: implement once, reuse everywhere)
inline float smoothstep(float t)
{
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
inline float smoothstep(float a, float b, float x) { return smoothstep((x - a) / (b - a)); }

// Deterministic pseudo random number in [0,1)
inline float hash1(float n)
{
    float s = std::sin(n * 127.1f) * 43758.5453f;
    return s - std::floor(s);
}

// Periodic wave: sin(2*pi*(cycles*u + phase)).  With integer `cycles` and u in
// [0,1) the value at u = 1 equals the value at u = 0, so loops have no jump.
inline float wave(float u, float cycles, float phase = 0.0f)
{
    return std::sin(TAU * (cycles * u + phase));
}

// ----------------------------------------------------------------------------
// Camera / layer transform  (translation + scaling about a pivot point)
//   screen = pivot + pan + scale * (p - pivot)
// ----------------------------------------------------------------------------
struct LayerXform { float s = 1, px = 0, py = 0, panx = 0, pany = 0; };

inline LayerXform layer;              // the transform currently active
inline bool       cullingEnabled = true;
inline int        statDrawn  = 0;     // objects drawn this frame
inline int        statCulled = 0;     // objects skipped this frame

inline void pushCamera(float scale, float px, float py, float panx, float pany)
{
    glPushMatrix();
    glTranslatef(px + panx, py + pany, 0.0f);
    glScalef(scale, scale, 1.0f);
    glTranslatef(-px, -py, 0.0f);
    layer = LayerXform{scale, px, py, panx, pany};
}
inline void popCamera()
{
    glPopMatrix();
    layer = LayerXform{};
}
inline float toScreenX(float x) { return layer.px + layer.panx + layer.s * (x - layer.px); }
inline float toScreenY(float y) { return layer.py + layer.pany + layer.s * (y - layer.py); }

// View-frustum style test: is a circle (x, y, r) in the current layer on screen?
inline bool visible(float x, float y, float r)
{
    const float sx = toScreenX(x), sy = toScreenY(y), sr = r * layer.s;
    const bool  v  = !cullingEnabled ||
                     (std::fabs(sx) - sr < WORLD_W + 0.30f && std::fabs(sy) - sr < 1.30f);
    if (v) ++statDrawn; else ++statCulled;
    return v;
}

// ----------------------------------------------------------------------------
// Primitive shapes
// ----------------------------------------------------------------------------
inline void rect(float x0, float y0, float x1, float y1)
{
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
    glEnd();
}

inline void rectV(float x0, float y0, float x1, float y1, const Color& bottom, const Color& top)
{
    glBegin(GL_QUADS);
    vtx(bottom, x0, y0); vtx(bottom, x1, y0); vtx(top, x1, y1); vtx(top, x0, y1);
    glEnd();
}

inline void tri(float x0, float y0, float x1, float y1, float x2, float y2)
{
    glBegin(GL_TRIANGLES);
    glVertex2f(x0, y0); glVertex2f(x1, y1); glVertex2f(x2, y2);
    glEnd();
}

inline void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3)
{
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y1); glVertex2f(x2, y2); glVertex2f(x3, y3);
    glEnd();
}

inline void ellipse(float x, float y, float rx, float ry, int seg = 28)
{
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x, y);
    for (int i = 0; i <= seg; i++)
    {
        const float a = TAU * i / seg;
        glVertex2f(x + std::cos(a) * rx, y + std::sin(a) * ry);
    }
    glEnd();
}
inline void circle(float x, float y, float r, int seg = 28) { ellipse(x, y, r, r, seg); }

// Soft radial glow: opaque centre colour fading to transparent at the edge.
inline void glow(float x, float y, float r, const Color& c, int seg = 40)
{
    glBegin(GL_TRIANGLE_FAN);
    setColor(c);
    glVertex2f(x, y);
    paint(c.r, c.g, c.b, 0.0f);
    for (int i = 0; i <= seg; i++)
    {
        const float a = TAU * i / seg;
        glVertex2f(x + std::cos(a) * r, y + std::sin(a) * r);
    }
    glEnd();
}

inline void ring(float x, float y, float rx, float ry, int seg = 40)
{
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < seg; i++)
    {
        const float a = TAU * i / seg;
        glVertex2f(x + std::cos(a) * rx, y + std::sin(a) * ry);
    }
    glEnd();
}

inline void thickLine(float x0, float y0, float x1, float y1, float w)
{
    float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f) return;
    const float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
    quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny);
}

// Vertical multi-stop gradient across x0..x1 (used for skies)
struct Stop { float y; Color c; };
inline void vGradient(float x0, float x1, const Stop* stops, int n)
{
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i < n; i++)
    {
        vtx(stops[i].c, x0, stops[i].y);
        vtx(stops[i].c, x1, stops[i].y);
    }
    glEnd();
}

// ----------------------------------------------------------------------------
// Ridges: a height profile sampled at fixed x steps. Used for mountains/hills.
// ----------------------------------------------------------------------------
struct Ridge
{
    std::vector<float> x, y;
    float x0 = 0, step = 0.02f;
};

// Sharp-peaked mountain profile
inline Ridge makeRidge(float x0, float x1, float step, float base, float amp, float seed, float freq)
{
    Ridge r; r.x0 = x0; r.step = step;
    for (float x = x0; x <= x1 + 1e-4f; x += step)
    {
        const float n = 0.55f * (1.0f - std::fabs(std::sin(freq * x + seed))) +
                        0.30f * (1.0f - std::fabs(std::sin(2.3f * freq * x + seed * 1.7f))) +
                        0.15f * (1.0f - std::fabs(std::sin(4.1f * freq * x + seed * 2.9f)));
        r.x.push_back(x);
        r.y.push_back(base + amp * n);
    }
    return r;
}

// Soft rolling-hill profile
inline Ridge makeRolling(float x0, float x1, float step, float base, float amp, float seed, float freq)
{
    Ridge r; r.x0 = x0; r.step = step;
    for (float x = x0; x <= x1 + 1e-4f; x += step)
    {
        const float n = 0.6f * (0.5f + 0.5f * std::sin(freq * x + seed)) +
                        0.4f * (0.5f + 0.5f * std::sin(2.1f * freq * x + seed * 1.3f));
        r.x.push_back(x);
        r.y.push_back(base + amp * n);
    }
    return r;
}

// Ridge built from explicit peaks (x, height above base, half-width).
// rounded = false: sharp cone peaks (alpine / rocky).  true: soft gaussian domes (green mountains).
struct PeakDef { float x, h, w; };
inline Ridge makePeakRidge(float x0, float x1, float step, float base, const std::vector<PeakDef>& peaks,
                           float seed, float rough, bool rounded)
{
    Ridge r; r.x0 = x0; r.step = step;
    for (float x = x0; x <= x1 + 1e-4f; x += step)
    {
        float m = 0.0f;
        for (const PeakDef& p : peaks)
        {
            const float dx = std::fabs(x - p.x) / p.w;
            const float v  = rounded ? p.h * std::exp(-dx * dx * 1.6f) : p.h * std::max(0.0f, 1.0f - dx);
            m = std::max(m, v);
        }
        // roughness grows with elevation so the foot of the mountain stays smooth
        const float n = rough * (0.6f * (1.0f - std::fabs(std::sin(9.0f * x + seed))) +
                                 0.4f * (1.0f - std::fabs(std::sin(23.0f * x + seed * 1.9f)))) *
                        std::min(1.0f, m * 2.5f);
        r.x.push_back(x);
        r.y.push_back(base + m + n);
    }
    return r;
}

inline float ridgeAt(const Ridge& r, float x)
{
    float f = (x - r.x0) / r.step;
    f = clampf(f, 0.0f, (float)(r.x.size() - 1) - 0.001f);
    const int   i = (int)f;
    const float t = f - i;
    return r.y[i] + (r.y[i + 1] - r.y[i]) * t;
}

inline void drawRidge(const Ridge& r, float bottom, const Color& top, const Color& bot)
{
    glBegin(GL_TRIANGLE_STRIP);
    for (size_t i = 0; i < r.x.size(); i++)
    {
        vtx(top, r.x[i], r.y[i]);
        vtx(bot, r.x[i], bottom);
    }
    glEnd();
}

// Dark triangle on the right-hand side of every peak => lit / shadow faces
inline void drawRidgeShadows(const Ridge& r, const Color& c)
{
    setColor(c);
    const int n = (int)r.x.size();
    for (int i = 1; i < n - 1; i++)
    {
        if (!(r.y[i] >= r.y[i - 1] && r.y[i] > r.y[i + 1])) continue;   // not a peak
        int j = i;
        while (j < n - 1 && r.y[j + 1] <= r.y[j]) j++;                  // walk down to valley
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(r.x[i] - 0.03f, r.y[j] - 0.02f);
        for (int k = i; k <= j; k++) glVertex2f(r.x[k], r.y[k]);
        glEnd();
    }
}

// Snow cap: everything above a jagged snow line is painted white
inline void drawSnow(const Ridge& r, float snowY, const Color& top, const Color& bottom)
{
    glBegin(GL_TRIANGLE_STRIP);
    for (size_t i = 0; i < r.x.size(); i++)
    {
        const float line = snowY + 0.035f * std::sin(r.x[i] * 37.0f) + 0.030f * hash1(i * 1.7f);
        const float yb   = r.y[i] > line ? line : r.y[i];
        vtx(top, r.x[i], r.y[i]);
        vtx(bottom, r.x[i], yb);
    }
    glEnd();
}

// Thin dark gullies running down from the crest (rocky mountains)
inline void drawRockStreaks(const Ridge& r, const Color& c, float minY)
{
    glLineWidth(1.5f);
    setColor(c);
    glBegin(GL_LINES);
    for (size_t i = 2; i < r.x.size(); i += 2)
    {
        if (r.y[i] < minY) continue;
        const float h = hash1(i * 2.3f);
        glVertex2f(r.x[i], r.y[i] - 0.01f);
        glVertex2f(r.x[i] + 0.012f * (h - 0.5f), r.y[i] - 0.05f - 0.14f * h);
    }
    glEnd();
    glLineWidth(1.0f);
}

// Sunlit face: lighter triangle on the left-hand side of every peak
inline void drawRidgeLights(const Ridge& r, const Color& c)
{
    setColor(c);
    const int n = (int)r.x.size();
    for (int i = 1; i < n - 1; i++)
    {
        if (!(r.y[i] >= r.y[i - 1] && r.y[i] > r.y[i + 1])) continue;   // not a peak
        int j = i;
        while (j > 0 && r.y[j - 1] <= r.y[j]) j--;                      // walk down to valley on the left
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(r.x[i] + 0.02f, r.y[j] - 0.03f);
        for (int k = j; k <= i; k++) glVertex2f(r.x[k], r.y[k]);
        glEnd();
    }
}

// ----------------------------------------------------------------------------
// Small reusable objects (object reuse: one function, many placements)
// ----------------------------------------------------------------------------
inline void drawPine(float x, float y, float h, float sway = 0.0f, float dark = 1.0f)
{
    setColor(rgb(0.25f, 0.15f, 0.10f));
    rect(x - 0.04f * h, y, x + 0.04f * h, y + 0.18f * h);
    const Color lit = shade(rgb(0.12f, 0.44f, 0.28f), dark);
    const Color shd = shade(rgb(0.04f, 0.26f, 0.20f), dark);
    for (int t = 0; t < 3; t++)
    {
        const float hw = (0.30f - 0.06f * t) * h;
        const float yb = y + h * (0.10f + 0.25f * t);
        const float yt = yb + h * (0.46f - 0.03f * t);
        const float tx = x + sway * h * (0.35f + 0.3f * t);
        setColor(lit); tri(x - hw, yb, x, yb, tx, yt);
        setColor(shd); tri(x, yb, x + hw, yb, tx, yt);
    }
}

inline void drawRoundTree(float x, float y, float h, float sway,
                          const Color& leafDark, const Color& leafLight, bool fruit = false)
{
    setColor(rgb(0.36f, 0.22f, 0.12f));
    quad(x - 0.035f * h, y, x + 0.035f * h, y, x + 0.020f * h, y + 0.50f * h, x - 0.020f * h, y + 0.50f * h);
    const float cx = x + sway * 0.03f * h, cy = y + 0.62f * h;
    static const float C[][3] = {{-0.22f, -0.04f, 0.24f}, {0.22f, -0.04f, 0.24f}, {0.0f, 0.14f, 0.28f},
                                 {-0.15f, 0.22f, 0.20f},  {0.16f, 0.22f, 0.20f},  {0.0f, -0.12f, 0.22f}};
    setColor(leafDark);
    for (const auto& c : C) circle(cx + c[0] * h, cy + c[1] * h, c[2] * h);
    setColor(leafLight);
    for (const auto& c : C) circle(cx + (c[0] - 0.03f) * h, cy + (c[1] + 0.04f) * h, c[2] * 0.78f * h);
    if (fruit)
    {
        setColor(rgb(0.86f, 0.15f, 0.12f));
        static const float F[][2] = {{-0.2f, 0.0f}, {0.12f, 0.05f}, {0.22f, -0.1f}, {-0.05f, 0.22f}, {0.0f, -0.12f}};
        for (const auto& f : F) circle(cx + f[0] * h, cy + f[1] * h, 0.035f * h, 12);
    }
}

inline void drawBush(float x, float y, float r, const Color& dark, const Color& light)
{
    setColor(dark);
    circle(x - r * 0.8f, y + r * 0.55f, r * 0.75f);
    circle(x + r * 0.8f, y + r * 0.55f, r * 0.75f);
    circle(x, y + r * 0.85f, r);
    setColor(light);
    circle(x - r * 0.85f, y + r * 0.65f, r * 0.50f);
    circle(x + r * 0.55f, y + r * 0.78f, r * 0.55f);
    circle(x - r * 0.1f, y + r * 1.05f, r * 0.60f);
}

inline void drawFence(float x0, float x1, float y, float h, const Color& c)
{
    const float sp = h * 0.42f, pw = h * 0.22f;
    const int   n  = (int)((x1 - x0) / sp);
    setColor(shade(c, 0.82f));
    rect(x0, y + h * 0.22f, x1, y + h * 0.32f);
    rect(x0, y + h * 0.60f, x1, y + h * 0.70f);
    for (int i = 0; i <= n; i++)
    {
        const float px = x0 + i * sp;
        setColor(c);
        rect(px - pw * 0.5f, y, px + pw * 0.5f, y + h * 0.88f);
        tri(px - pw * 0.5f, y + h * 0.88f, px + pw * 0.5f, y + h * 0.88f, px, y + h);
    }
}

// Fluffy cloud. pink = 0 (white) .. 1 (sunrise pink)
inline void drawCloud(float x, float y, float s, float pink = 0.5f, float alpha = 1.0f)
{
    static const float B[][3] = {{-0.30f, -0.02f, 0.16f}, {-0.10f, 0.06f, 0.22f}, {0.14f, 0.10f, 0.26f},
                                 {0.40f, 0.04f, 0.22f},   {0.62f, -0.02f, 0.16f}, {0.16f, -0.02f, 0.20f},
                                 {0.0f, -0.04f, 0.18f}};
    const Color shadow = mix(rgb(0.72f, 0.76f, 0.92f, alpha), rgb(0.86f, 0.68f, 0.84f, alpha), pink);
    const Color body   = mix(rgb(0.98f, 0.98f, 1.00f, alpha), rgb(1.00f, 0.86f, 0.90f, alpha), pink * 0.75f);
    const Color top    = rgb(1.0f, 1.0f, 1.0f, alpha);
    setColor(shadow);
    for (const auto& b : B) ellipse(x + b[0] * s, y + (b[1] - 0.04f) * s, b[2] * s * 1.05f, b[2] * s * 0.80f);
    setColor(body);
    for (const auto& b : B) ellipse(x + b[0] * s, y + b[1] * s, b[2] * s, b[2] * s * 0.80f);
    setColor(top);
    for (const auto& b : B) ellipse(x + (b[0] - 0.02f) * s, y + (b[1] + 0.045f) * s, b[2] * s * 0.78f, b[2] * s * 0.58f);
}

// Simple flapping bird silhouette. flap is -1..1
inline void drawBird(float x, float y, float size, float flap, const Color& c)
{
    setColor(c);
    const float lift = size * (0.10f + 0.50f * flap);
    tri(x, y, x - size, y + lift, x - size * 0.45f, y - size * 0.10f);
    tri(x, y, x + size, y + lift, x + size * 0.45f, y - size * 0.10f);
    ellipse(x, y - size * 0.05f, size * 0.16f, size * 0.10f, 10);
}

}   // namespace gfx

#endif
