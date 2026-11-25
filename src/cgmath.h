// cgmath.h - computer-graphics mathematics used across the project (header only)
//
//   Mat3                 2D homogeneous transformation matrices (translate / scale / rotate,
//                        composition, inverse) - camera, picking, reflections
//   Bezier / Hermite     quadratic + cubic Bezier curves, cubic Hermite and
//   Kochanek-Bartels     TCB splines (tension) for camera paths
//   ArcLength            arc-length re-parameterisation of a parametric curve (constant speed)
//   midpointDisplacement 1D fractal terrain (Fournier-Fussell-Carpenter)
//   valueNoise / fbm     smooth pseudo-random noise and fractal Brownian motion
//   clipLineCS           Cohen-Sutherland line clipping against a rectangle
//   clipPolygonSH        Sutherland-Hodgman polygon clipping against a convex polygon
//   pointInPolygon       ray-casting (even-odd) point-in-polygon test
//   collisions           circle-circle, circle-AABB, segment-point distance, swept circle vs line

#ifndef CGMATH_H
#define CGMATH_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cg
{

struct V2
{
    float x = 0, y = 0;
    V2() = default;
    V2(float x_, float y_) : x(x_), y(y_) {}
    V2 operator+(const V2& o) const { return V2(x + o.x, y + o.y); }
    V2 operator-(const V2& o) const { return V2(x - o.x, y - o.y); }
    V2 operator*(float k) const { return V2(x * k, y * k); }
};
inline float dot(const V2& a, const V2& b) { return a.x * b.x + a.y * b.y; }
inline float cross(const V2& a, const V2& b) { return a.x * b.y - a.y * b.x; }
inline float len(const V2& a) { return std::sqrt(a.x * a.x + a.y * a.y); }
inline V2 lerp(const V2& a, const V2& b, float t) { return a + (b - a) * t; }

// ----------------------------------------------------------------------------
// 2D homogeneous transformation matrix (row-major 3x3)
//   | a b tx |       p' = M * [x y 1]^T
//   | c d ty |
//   | 0 0 1  |
// ----------------------------------------------------------------------------
struct Mat3
{
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    static Mat3 identity() { return Mat3(); }
    static Mat3 translate(float tx, float ty) { Mat3 r; r.m[2] = tx; r.m[5] = ty; return r; }
    static Mat3 scale(float sx, float sy) { Mat3 r; r.m[0] = sx; r.m[4] = sy; return r; }
    static Mat3 rotate(float rad) { Mat3 r; const float c = std::cos(rad), s = std::sin(rad); r.m[0] = c; r.m[1] = -s; r.m[3] = s; r.m[4] = c; return r; }
    static Mat3 reflectY(float lineY)            // mirror about the horizontal line y = lineY
    { return translate(0, lineY) * scale(1, -1) * translate(0, -lineY); }

    Mat3 operator*(const Mat3& b) const          // composition: (A*B) p = A (B p)
    {
        Mat3 r;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                r.m[i * 3 + j] = m[i * 3] * b.m[j] + m[i * 3 + 1] * b.m[3 + j] + m[i * 3 + 2] * b.m[6 + j];
        return r;
    }
    V2 apply(const V2& p) const { return V2(m[0] * p.x + m[1] * p.y + m[2], m[3] * p.x + m[4] * p.y + m[5]); }
    Mat3 inverse() const                          // affine inverse
    {
        const float det = m[0] * m[4] - m[1] * m[3];
        const float id = std::fabs(det) > 1e-12f ? 1.0f / det : 0.0f;
        Mat3 r;
        r.m[0] = m[4] * id;  r.m[1] = -m[1] * id;
        r.m[3] = -m[3] * id; r.m[4] = m[0] * id;
        r.m[2] = -(r.m[0] * m[2] + r.m[1] * m[5]);
        r.m[5] = -(r.m[3] * m[2] + r.m[4] * m[5]);
        return r;
    }
    void toGL(float out[16]) const               // column-major 4x4 for glMultMatrixf
    {
        const float g[16] = {m[0], m[3], 0, 0,  m[1], m[4], 0, 0,  0, 0, 1, 0,  m[2], m[5], 0, 1};
        for (int i = 0; i < 16; i++) out[i] = g[i];
    }
};

// ----------------------------------------------------------------------------
// Curves
// ----------------------------------------------------------------------------
inline V2 quadBezier(const V2& p0, const V2& c, const V2& p1, float t)
{
    const float u = 1.0f - t;
    return p0 * (u * u) + c * (2 * u * t) + p1 * (t * t);
}
inline V2 cubicBezier(const V2& p0, const V2& p1, const V2& p2, const V2& p3, float t)
{
    const float u = 1.0f - t;
    return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
}
inline V2 cubicBezierTangent(const V2& p0, const V2& p1, const V2& p2, const V2& p3, float t)
{
    const float u = 1.0f - t;
    return (p1 - p0) * (3 * u * u) + (p2 - p1) * (6 * u * t) + (p3 - p2) * (3 * t * t);
}
// cubic Hermite basis: position p0->p1 with tangents m0, m1 (t in 0..1)
inline float hermite(float p0, float m0, float p1, float m1, float t)
{
    const float t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * p0 + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * p1 + (t3 - t2) * m1;
}
// Kochanek-Bartels (TCB) tangent with tension only (continuity = bias = 0).
// tension 0 = Catmull-Rom; tension -> 1 shortens the tangents so the curve lingers at the keys.
inline float kbTangent(float pPrev, float /*p*/, float pNext, float tension)
{
    return (1.0f - tension) * 0.5f * (pNext - pPrev);
}

// Arc-length table for any parametric curve f(t), t in [0,1]: maps distance -> parameter
struct ArcLength
{
    std::vector<float> s;            // cumulative length at t = i / (n - 1)
    template <class F> void build(F f, int n = 128)
    {
        s.assign(n, 0.0f);
        V2 prev = f(0.0f);
        for (int i = 1; i < n; i++) { const V2 p = f((float)i / (n - 1)); s[i] = s[i - 1] + len(p - prev); prev = p; }
    }
    float total() const { return s.empty() ? 0.0f : s.back(); }
    float paramAt(float dist) const  // binary search + linear interpolation
    {
        if (s.size() < 2) return 0.0f;
        dist = std::max(0.0f, std::min(dist, total()));
        const size_t i = (size_t)(std::upper_bound(s.begin(), s.end(), dist) - s.begin());
        if (i == 0) return 0.0f;
        if (i >= s.size()) return 1.0f;
        const float seg = s[i] - s[i - 1];
        const float k = seg > 1e-9f ? (dist - s[i - 1]) / seg : 0.0f;
        return ((float)(i - 1) + k) / (float)(s.size() - 1);
    }
};

// ----------------------------------------------------------------------------
// Procedural noise / fractal terrain
// ----------------------------------------------------------------------------
inline float hashU(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (x & 0xFFFFFF) / 16777216.0f;
}
// 1D value noise: random values at integers, smooth (quintic) interpolation between them
inline float valueNoise(float x, uint32_t seed)
{
    const float fl = std::floor(x);
    const int   i = (int)fl;
    const float f = x - fl, u = f * f * f * (f * (f * 6 - 15) + 10);
    const float a = hashU((uint32_t)i * 747796405u + seed), b = hashU((uint32_t)(i + 1) * 747796405u + seed);
    return a + (b - a) * u;
}
// fractal Brownian motion: octaves of noise, each twice the frequency, `gain` the amplitude
inline float fbm(float x, uint32_t seed, int octaves = 5, float gain = 0.5f)
{
    float sum = 0, amp = 0.5f, norm = 0, f = 1;
    for (int o = 0; o < octaves; o++) { sum += amp * valueNoise(x * f, seed + 131u * o); norm += amp; amp *= gain; f *= 2.0f; }
    return sum / norm;
}
// Midpoint displacement: 2^levels + 1 heights. Every pass halves the segments and moves each new
// midpoint by a random amount whose range shrinks by 2^-roughness  (fractal mountain profile)
inline std::vector<float> midpointDisplacement(int levels, float roughness, uint32_t seed)
{
    const int n = (1 << levels) + 1;
    std::vector<float> h(n, 0.0f);
    float range = 1.0f;
    uint32_t k = seed;
    for (int step = n - 1; step > 1; step /= 2)
    {
        for (int i = step / 2; i < n; i += step)
        {
            const float r = hashU(k++ * 2654435761u) * 2.0f - 1.0f;
            h[i] = 0.5f * (h[i - step / 2] + h[i + step / 2]) + r * range;
        }
        range *= std::pow(2.0f, -roughness);
    }
    return h;
}

// ----------------------------------------------------------------------------
// Clipping
// ----------------------------------------------------------------------------
// Cohen-Sutherland: clip segment a-b to [x0,x1]x[y0,y1]. Returns false if fully outside.
inline int csCode(float x, float y, float x0, float y0, float x1, float y1)
{
    return (x < x0 ? 1 : 0) | (x > x1 ? 2 : 0) | (y < y0 ? 4 : 0) | (y > y1 ? 8 : 0);
}
inline bool clipLineCS(V2& a, V2& b, float x0, float y0, float x1, float y1)
{
    int ca = csCode(a.x, a.y, x0, y0, x1, y1), cb = csCode(b.x, b.y, x0, y0, x1, y1);
    for (int guard = 0; guard < 8; guard++)
    {
        if (!(ca | cb)) return true;           // trivially inside
        if (ca & cb) return false;             // trivially outside (same outer half-plane)
        const int c = ca ? ca : cb;
        float x = 0, y = 0;
        if (c & 8)      { x = a.x + (b.x - a.x) * (y1 - a.y) / (b.y - a.y); y = y1; }
        else if (c & 4) { x = a.x + (b.x - a.x) * (y0 - a.y) / (b.y - a.y); y = y0; }
        else if (c & 2) { y = a.y + (b.y - a.y) * (x1 - a.x) / (b.x - a.x); x = x1; }
        else            { y = a.y + (b.y - a.y) * (x0 - a.x) / (b.x - a.x); x = x0; }
        if (c == ca) { a = V2(x, y); ca = csCode(x, y, x0, y0, x1, y1); }
        else         { b = V2(x, y); cb = csCode(x, y, x0, y0, x1, y1); }
    }
    return !(ca | cb);
}

// Sutherland-Hodgman: clip `poly` against a CONVEX counter-clockwise polygon `clip`
inline std::vector<V2> clipPolygonSH(const std::vector<V2>& poly, const std::vector<V2>& clip)
{
    std::vector<V2> out = poly;
    for (size_t e = 0; e < clip.size() && !out.empty(); e++)
    {
        const V2 A = clip[e], B = clip[(e + 1) % clip.size()];
        auto inside = [&](const V2& p) { return cross(B - A, p - A) >= 0.0f; };
        auto hit = [&](const V2& p, const V2& q)      // segment p-q meets the infinite line A-B
        {
            const V2 d = q - p, ab = B - A;
            const float den = cross(ab, d);
            const float t = std::fabs(den) > 1e-12f ? cross(ab, A - p) / den : 0.0f;
            return p + d * t;
        };
        std::vector<V2> in = out;
        out.clear();
        for (size_t i = 0; i < in.size(); i++)
        {
            const V2 P = in[i], Q = in[(i + 1) % in.size()];
            const bool pi = inside(P), qi = inside(Q);
            if (pi && qi)       out.push_back(Q);
            else if (pi && !qi) out.push_back(hit(P, Q));
            else if (!pi && qi) { out.push_back(hit(P, Q)); out.push_back(Q); }
        }
    }
    return out;
}

// Ray casting (even-odd rule)
inline bool pointInPolygon(const V2& p, const std::vector<V2>& poly)
{
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
    {
        const V2& a = poly[i]; const V2& b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

// ----------------------------------------------------------------------------
// Collision detection
// ----------------------------------------------------------------------------
inline bool circleCircle(const V2& a, float ra, const V2& b, float rb)
{
    const V2 d = b - a;
    return dot(d, d) <= (ra + rb) * (ra + rb);
}
// closest point on an axis-aligned box, then a circle test
inline bool circleAABB(const V2& c, float r, float x0, float y0, float x1, float y1, V2* normal = nullptr)
{
    const float qx = std::max(x0, std::min(c.x, x1)), qy = std::max(y0, std::min(c.y, y1));
    const float dx = c.x - qx, dy = c.y - qy, d2 = dx * dx + dy * dy;
    if (d2 > r * r) return false;
    if (normal)
    {
        const float d = std::sqrt(d2);
        if (d > 1e-6f) *normal = V2(dx / d, dy / d);
        else
        {   // centre inside the box: push out along the smallest overlap
            const float l = c.x - x0, rr = x1 - c.x, b = c.y - y0, t = y1 - c.y;
            const float m = std::min(std::min(l, rr), std::min(b, t));
            *normal = m == l ? V2(-1, 0) : m == rr ? V2(1, 0) : m == b ? V2(0, -1) : V2(0, 1);
        }
    }
    return true;
}
inline float distPointSegment(const V2& p, const V2& a, const V2& b)
{
    const V2 ab = b - a;
    const float l2 = dot(ab, ab);
    const float t = l2 > 1e-12f ? std::max(0.0f, std::min(1.0f, dot(p - a, ab) / l2)) : 0.0f;
    return len(p - (a + ab * t));
}
// capsule (segment a-b with radius r) vs circle: used for "ball hits a child's body"
inline bool circleCapsule(const V2& c, float rc, const V2& a, const V2& b, float r)
{
    return distPointSegment(c, a, b) <= rc + r;
}

}   // namespace cg

#endif
