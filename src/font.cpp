// font.cpp - turns the glyph outlines of fontdata.h into anti-aliased textures and draws text
//
// Computer-graphics algorithms used here (see CG_ALGORITHMS.md, section "Text and logo"):
//   * adaptive de Casteljau subdivision to flatten quadratic Bezier curves into line edges
//   * scan-line polygon filling with an Edge Table (ET) and an Active Edge Table (AET),
//     non-zero winding rule, 4x vertical super-sampling + exact horizontal span coverage
//   * shelf packing of the glyph bitmaps into one texture atlas
//   * a software mip-map pyramid (2x2 box filter) for clean minification
//   * logo post-processing: morphological dilation (outline), separable box blur x3
//     (approximates a Gaussian glow), offset difference mask (bevel highlight)

#include "font.h"
#include "fontdata.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace gfx;

namespace font
{
namespace
{

struct GlyphSrc { const short* ops; int count; int advance; };

GlyphSrc findGlyph(const fontdata::GlyphRec* table, const short* ops, char c)
{
    const int code = (unsigned char)c;
    const int idx = (code >= 32 && code < 127) ? code - 32 : '?' - 32;
    const fontdata::GlyphRec& g = table[idx];
    return GlyphSrc{ops + g.start, g.count, g.advance};
}

// ----------------------------------------------------------------------------
// 1. Bezier flattening (adaptive de Casteljau subdivision)
// ----------------------------------------------------------------------------
struct Pt { float x, y; };

void addEdge(std::vector<Edge>& e, Pt a, Pt b)
{
    if (std::fabs(a.y - b.y) < 1e-6f) return;               // horizontal edges never cross a scan-line
    e.push_back(Edge{a.x, a.y, b.x, b.y, a.y < b.y ? 1 : -1});
}

void flattenQuad(std::vector<Edge>& e, Pt p0, Pt c, Pt p1, int depth)
{
    // flatness = distance of the control point from the chord midpoint
    const float mx = (p0.x + p1.x) * 0.5f, my = (p0.y + p1.y) * 0.5f;
    const float fx = c.x - mx, fy = c.y - my;
    if (depth > 10 || fx * fx + fy * fy < 0.04f) { addEdge(e, p0, p1); return; }
    // de Casteljau split at t = 0.5
    const Pt a{(p0.x + c.x) * 0.5f, (p0.y + c.y) * 0.5f};
    const Pt b{(c.x + p1.x) * 0.5f, (c.y + p1.y) * 0.5f};
    const Pt m{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    flattenQuad(e, p0, a, m, depth + 1);
    flattenQuad(e, m, b, p1, depth + 1);
}

// glyph outline -> edges in PIXEL space (y down). scale = pixels per font unit
std::vector<Edge> glyphEdges(const GlyphSrc& g, float scale, float ox, float oy)
{
    std::vector<Edge> e;
    Pt start{0, 0}, cur{0, 0};
    auto P = [&](int x, int y) { return Pt{ox + x * scale, oy - y * scale}; };
    int i = 0;
    while (i < g.count)
    {
        const int op = g.ops[i++];
        if (op == 0)      { cur = start = P(g.ops[i], g.ops[i + 1]); i += 2; }
        else if (op == 1) { const Pt p = P(g.ops[i], g.ops[i + 1]); i += 2; addEdge(e, cur, p); cur = p; }
        else if (op == 2) { const Pt c = P(g.ops[i], g.ops[i + 1]), p = P(g.ops[i + 2], g.ops[i + 3]); i += 4; flattenQuad(e, cur, c, p, 0); cur = p; }
        else              { addEdge(e, cur, start); cur = start; }
    }
    return e;
}

void glyphBounds(const GlyphSrc& g, int& x0, int& y0, int& x1, int& y1)
{
    x0 = y0 = 1 << 20; x1 = y1 = -(1 << 20);
    int i = 0;
    auto take = [&](int x, int y) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); };
    while (i < g.count)
    {
        const int op = g.ops[i++];
        if (op == 0 || op == 1) { take(g.ops[i], g.ops[i + 1]); i += 2; }
        else if (op == 2) { take(g.ops[i], g.ops[i + 1]); take(g.ops[i + 2], g.ops[i + 3]); i += 4; }
    }
    if (x0 > x1) { x0 = y0 = x1 = y1 = 0; }
}

// ----------------------------------------------------------------------------
// 4. software mip-maps (2x2 box filter) and texture upload
// ----------------------------------------------------------------------------
GLuint upload(const std::vector<unsigned char>& base, int w, int h, int maxLevel)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    std::vector<unsigned char> cur = base, next;
    int lw = w, lh = h, level = 0;
    for (;;)
    {
        glTexImage2D(GL_TEXTURE_2D, level, GL_ALPHA, lw, lh, 0, GL_ALPHA, GL_UNSIGNED_BYTE, cur.data());
        if (level >= maxLevel || (lw == 1 && lh == 1)) break;
        const int nw = std::max(1, lw / 2), nh = std::max(1, lh / 2);
        next.assign((size_t)nw * nh, 0);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++)
            {
                const int sx = std::min(lw - 1, x * 2), sy = std::min(lh - 1, y * 2);
                const int sx1 = std::min(lw - 1, sx + 1), sy1 = std::min(lh - 1, sy + 1);
                const int s = cur[(size_t)sy * lw + sx] + cur[(size_t)sy * lw + sx1] + cur[(size_t)sy1 * lw + sx] + cur[(size_t)sy1 * lw + sx1];
                next[(size_t)y * nw + x] = (unsigned char)((s + 2) / 4);
            }
        cur.swap(next);
        lw = nw; lh = nh; level++;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, level > 0 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

// ----------------------------------------------------------------------------
// Atlas (shelf packing)
// ----------------------------------------------------------------------------
struct AGlyph
{
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;      // texture rectangle
    float bx = 0, by = 0, bw = 0, bh = 0;      // bitmap rectangle in EM units (bx,by = left/bottom relative to pen)
    float adv = 0;                             // advance in em units
};

struct Atlas
{
    GLuint tex = 0;
    AGlyph g[95];
    float  capEm = 0.7f;                       // cap height / em
};

Atlas ui_;
bool  ready_ = false;

void buildUiAtlas()
{
    const float EM = 60.0f;                                   // pixels per em in the atlas
    const float scale = EM / fontdata::UIF_UPM;
    const int   W = 1024, PAD = 4;
    std::vector<unsigned char> img((size_t)W * 1024, 0);
    int penX = PAD, penY = PAD, shelf = 0, usedH = 0;
    ui_.capEm = 700.0f / fontdata::UIF_UPM;
    for (int c = 32; c < 127; c++)
    {
        const GlyphSrc gs = findGlyph(fontdata::UIF_GLYPHS, fontdata::UIF_OPS, (char)c);
        AGlyph& ag = ui_.g[c - 32];
        ag.adv = gs.advance / (float)fontdata::UIF_UPM;
        int x0, y0, x1, y1;
        glyphBounds(gs, x0, y0, x1, y1);
        if (gs.count == 0 || x1 <= x0) continue;
        const int bw = (int)std::ceil((x1 - x0) * scale) + 2, bh = (int)std::ceil((y1 - y0) * scale) + 2;
        if (penX + bw + PAD > W) { penX = PAD; penY += shelf + PAD; shelf = 0; }
        std::vector<Edge> e = glyphEdges(gs, scale, 1.0f - x0 * scale, 1.0f + y1 * scale);
        std::vector<unsigned char> bmp((size_t)bw * bh);
        rasterize(e.data(), (int)e.size(), bw, bh, bmp.data());
        for (int y = 0; y < bh; y++) std::memcpy(&img[(size_t)(penY + y) * W + penX], &bmp[(size_t)y * bw], (size_t)bw);
        ag.u0 = (float)penX / W; ag.u1 = (float)(penX + bw) / W;
        ag.v0 = (float)penY;     ag.v1 = (float)(penY + bh);             // normalised after packing
        ag.bx = (x0 * scale - 1.0f) / EM; ag.bw = bw / EM;
        ag.by = (y1 * scale + 1.0f) / EM - bh / EM; ag.bh = bh / EM;
        penX += bw + PAD; shelf = std::max(shelf, bh); usedH = penY + shelf;
    }
    int H = 64; while (H < usedH + PAD) H *= 2;
    img.resize((size_t)W * H);
    for (AGlyph& ag : ui_.g) { ag.v0 /= H; ag.v1 /= H; }
    ui_.tex = upload(img, W, H, 3);
}

// ----------------------------------------------------------------------------
// Logo: four masks per letter (glow, outline, fill, highlight)
// ----------------------------------------------------------------------------
struct LogoLetter { char c; float x, y, em; float adv; AGlyph g; };
GLuint logoTex_[4] = {0, 0, 0, 0};          // 0 glow, 1 outline, 2 fill, 3 bevel highlight
std::vector<LogoLetter> letters_;
float logoPadEm_ = 0.0f;

void dilate(const std::vector<unsigned char>& src, std::vector<unsigned char>& dst, int w, int h, int r)
{
    dst.assign(src.size(), 0);
    std::vector<std::pair<int, int>> disk;                  // circular structuring element
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) if (dx * dx + dy * dy <= r * r) disk.push_back({dx, dy});
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            int m = 0;
            for (const auto& d : disk)
            {
                const int xx = x + d.first, yy = y + d.second;
                if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                m = std::max(m, (int)src[(size_t)yy * w + xx]);
                if (m == 255) break;
            }
            dst[(size_t)y * w + x] = (unsigned char)m;
        }
}

void boxBlur(std::vector<unsigned char>& img, int w, int h, int r)      // separable: rows, then columns
{
    std::vector<unsigned char> tmp(img.size());
    const int n = 2 * r + 1;
    for (int y = 0; y < h; y++)
    {
        int sum = 0;
        for (int x = -r; x <= r; x++) sum += img[(size_t)y * w + std::clamp(x, 0, w - 1)];
        for (int x = 0; x < w; x++)
        {
            tmp[(size_t)y * w + x] = (unsigned char)(sum / n);
            sum += img[(size_t)y * w + std::min(w - 1, x + r + 1)] - img[(size_t)y * w + std::max(0, x - r)];
        }
    }
    for (int x = 0; x < w; x++)
    {
        int sum = 0;
        for (int y = -r; y <= r; y++) sum += tmp[(size_t)std::clamp(y, 0, h - 1) * w + x];
        for (int y = 0; y < h; y++)
        {
            img[(size_t)y * w + x] = (unsigned char)(sum / n);
            sum += tmp[(size_t)std::min(h - 1, y + r + 1) * w + x] - tmp[(size_t)std::max(0, y - r) * w + x];
        }
    }
}

void buildLogo()
{
    const char* lines[2] = {"Enchanted", "World"};
    const float ems[2] = {0.34f, 0.25f};                    // world-units per em of each line
    const float EM = 200.0f;                                // pixels per em in the logo atlas
    const float scale = EM / fontdata::DECO_UPM;
    const int   PAD = 26, W = 2048;
    logoPadEm_ = PAD / EM;

    struct Bmp { char c; int w, h; float bx, by; std::vector<unsigned char> m[4]; };
    std::vector<Bmp> bmps;                                  // one per distinct character
    auto bmpFor = [&](char c) -> int {
        for (size_t i = 0; i < bmps.size(); i++) if (bmps[i].c == c) return (int)i;
        const GlyphSrc gs = findGlyph(fontdata::DECO_GLYPHS, fontdata::DECO_OPS, c);
        int x0, y0, x1, y1;
        glyphBounds(gs, x0, y0, x1, y1);
        Bmp b; b.c = c;
        b.w = (int)std::ceil((x1 - x0) * scale) + 2 * PAD;
        b.h = (int)std::ceil((y1 - y0) * scale) + 2 * PAD;
        b.bx = (x0 * scale - PAD) / EM;
        b.by = (y1 * scale + PAD) / EM - b.h / EM;
        std::vector<Edge> e = glyphEdges(gs, scale, PAD - x0 * scale, PAD + y1 * scale);
        std::vector<unsigned char>& fill = b.m[2];
        fill.assign((size_t)b.w * b.h, 0);
        rasterize(e.data(), (int)e.size(), b.w, b.h, fill.data());
        dilate(fill, b.m[1], b.w, b.h, 6);                  // dark outline
        dilate(fill, b.m[0], b.w, b.h, 4);                  // glow = blurred, dilated shape
        for (int k = 0; k < 3; k++) boxBlur(b.m[0], b.w, b.h, 7);
        b.m[3].assign(fill.size(), 0);                      // bevel: shape minus itself shifted down
        for (int y = 0; y < b.h; y++)
            for (int x = 0; x < b.w; x++)
            {
                const int a = fill[(size_t)y * b.w + x], above = y >= 5 ? fill[(size_t)(y - 5) * b.w + x] : 0;
                b.m[3][(size_t)y * b.w + x] = (unsigned char)std::max(0, a - above);
            }
        bmps.push_back(std::move(b));
        return (int)bmps.size() - 1;
    };

    // layout: both lines centred on x = 0 (world units), line 0 above line 1
    letters_.clear();
    const float lineY[2] = {0.03f, -0.27f};
    std::vector<int> which;
    for (int l = 0; l < 2; l++)
    {
        float total = 0.0f;
        for (const char* p = lines[l]; *p; ++p) total += findGlyph(fontdata::DECO_GLYPHS, fontdata::DECO_OPS, *p).advance;
        float x = -0.5f * total / fontdata::DECO_UPM * ems[l];
        for (const char* p = lines[l]; *p; ++p)
        {
            const GlyphSrc gs = findGlyph(fontdata::DECO_GLYPHS, fontdata::DECO_OPS, *p);
            LogoLetter L; L.c = *p; L.x = x; L.y = lineY[l]; L.em = ems[l]; L.adv = gs.advance / (float)fontdata::DECO_UPM;
            if (*p != ' ') which.push_back(bmpFor(*p)); else which.push_back(-1);
            letters_.push_back(L);
            x += L.adv * ems[l];
        }
    }

    // shelf-pack the distinct bitmaps
    int penX = 0, penY = 0, shelf = 0;
    std::vector<int> bx(bmps.size()), by(bmps.size());
    for (size_t i = 0; i < bmps.size(); i++)
    {
        if (penX + bmps[i].w > W) { penX = 0; penY += shelf; shelf = 0; }
        bx[i] = penX; by[i] = penY; penX += bmps[i].w; shelf = std::max(shelf, bmps[i].h);
    }
    int H = 64; while (H < penY + shelf) H *= 2;
    for (int k = 0; k < 4; k++)
    {
        std::vector<unsigned char> img((size_t)W * H, 0);
        for (size_t i = 0; i < bmps.size(); i++)
            for (int y = 0; y < bmps[i].h; y++)
                std::memcpy(&img[(size_t)(by[i] + y) * W + bx[i]], &bmps[i].m[k][(size_t)y * bmps[i].w], (size_t)bmps[i].w);
        logoTex_[k] = upload(img, W, H, 4);
    }
    for (size_t i = 0; i < letters_.size(); i++)
    {
        if (which[i] < 0) continue;
        const Bmp& b = bmps[which[i]];
        AGlyph& g = letters_[i].g;
        g.u0 = (float)bx[which[i]] / W; g.u1 = (float)(bx[which[i]] + b.w) / W;
        g.v0 = (float)by[which[i]] / H; g.v1 = (float)(by[which[i]] + b.h) / H;
        g.bx = b.bx; g.by = b.by; g.bw = b.w / EM; g.bh = b.h / EM;
    }
}

float easeOutBack(float t)
{
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

// thick cubic Bezier ribbon (used for the logo flourishes), drawn up to parameter `upto`
void bezierRibbon(Pt p0, Pt p1, Pt p2, Pt p3, float w0, float w1, float upto, const Color& c)
{
    const int N = 40;
    const int n = std::max(1, (int)(N * upto));
    glColor4f(c.r, c.g, c.b, c.a);
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= n; i++)
    {
        const float t = upto * i / n, u = 1.0f - t;
        const float x = u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x;
        const float y = u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y;
        const float dx = 3 * u * u * (p1.x - p0.x) + 6 * u * t * (p2.x - p1.x) + 3 * t * t * (p3.x - p2.x);
        const float dy = 3 * u * u * (p1.y - p0.y) + 6 * u * t * (p2.y - p1.y) + 3 * t * t * (p3.y - p2.y);
        const float l = std::max(1e-5f, std::sqrt(dx * dx + dy * dy));
        const float w = lerpf(w0, w1, t);
        glVertex2f(x - dy / l * w, y + dx / l * w);
        glVertex2f(x + dy / l * w, y - dx / l * w);
    }
    glEnd();
}

}   // namespace

// ----------------------------------------------------------------------------
// 2+3. Scan-line polygon fill: edge table + active edge table, non-zero winding,
//      4 sub-scanlines per pixel, exact horizontal coverage of every span.
// ----------------------------------------------------------------------------
void rasterize(const Edge* edgesIn, int n, int w, int h, unsigned char* out)
{
    struct E { float ymin, ymax, xAtYmin, dxdy; int dir; };
    std::vector<E> et;                                          // edge table, sorted by ymin
    et.reserve((size_t)n);
    for (int i = 0; i < n; i++)
    {
        const Edge& e = edgesIn[i];
        const bool down = e.y0 < e.y1;
        const float ya = down ? e.y0 : e.y1, yb = down ? e.y1 : e.y0;
        const float xa = down ? e.x0 : e.x1, xb = down ? e.x1 : e.x0;
        et.push_back(E{ya, yb, xa, (xb - xa) / (yb - ya), e.dir});
    }
    std::sort(et.begin(), et.end(), [](const E& a, const E& b) { return a.ymin < b.ymin; });

    std::vector<float> acc((size_t)w + 2);
    std::vector<int> aet;                                       // indices into et
    std::vector<std::pair<float, int>> xs;
    size_t next = 0;
    const int SUB = 4;
    for (int row = 0; row < h; row++)
    {
        std::fill(acc.begin(), acc.end(), 0.0f);
        for (int s = 0; s < SUB; s++)
        {
            const float sy = row + (s + 0.5f) / SUB;
            while (next < et.size() && et[next].ymin <= sy) aet.push_back((int)next++);     // ET -> AET
            aet.erase(std::remove_if(aet.begin(), aet.end(), [&](int i) { return et[i].ymax <= sy; }), aet.end());
            xs.clear();
            for (int i : aet)
                if (et[i].ymin <= sy) xs.push_back({et[i].xAtYmin + (sy - et[i].ymin) * et[i].dxdy, et[i].dir});
            std::sort(xs.begin(), xs.end());
            int wind = 0;                                       // non-zero winding rule
            for (size_t k = 0; k < xs.size(); k++)
            {
                wind += xs[k].second;
                if (wind == 0 || k + 1 >= xs.size()) continue;      // span [x_k, x_k+1) is inside
                float xa = std::max(0.0f, xs[k].first), xb = std::min((float)w, xs[k + 1].first);
                if (xb <= xa) continue;
                const float wgt = 1.0f / SUB;
                const int ia = (int)xa, ib = (int)xb;
                if (ia == ib) { acc[ia] += (xb - xa) * wgt; continue; }
                acc[ia] += (ia + 1 - xa) * wgt;                 // partial first pixel
                for (int x = ia + 1; x < ib; x++) acc[x] += wgt; // full pixels
                if (ib < w) acc[ib] += (xb - ib) * wgt;         // partial last pixel
            }
        }
        for (int x = 0; x < w; x++) out[(size_t)row * w + x] = (unsigned char)std::min(255.0f, acc[x] * 255.0f + 0.5f);
    }
}

void init()
{
    if (ready_) return;
    buildUiAtlas();
    buildLogo();
    ready_ = true;
}

float width(const char* s, float cap)
{
    const float em = cap / ui_.capEm;
    float w = 0.0f;
    for (const char* p = s; *p; ++p)
    {
        const int c = (unsigned char)*p;
        w += ui_.g[(c >= 32 && c < 127) ? c - 32 : '?' - 32].adv * em;
    }
    return w;
}

void draw(float x, float y, float cap, const Color& c, const char* s, int align, bool shadow)
{
    if (!ready_ || !s || !*s) return;
    const float em = cap / ui_.capEm;
    if (align == 1) x -= width(s, cap) * 0.5f;
    else if (align == 2) x -= width(s, cap);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, ui_.tex);
    for (int pass = shadow ? 0 : 1; pass < 2; pass++)
    {
        const float ox = pass == 0 ? cap * 0.07f : 0.0f, oy = pass == 0 ? -cap * 0.09f : 0.0f;
        if (pass == 0) glColor4f(0.0f, 0.0f, 0.0f, 0.45f * c.a);
        else           glColor4f(c.r, c.g, c.b, c.a);
        glBegin(GL_QUADS);
        float pen = x + ox;
        for (const char* p = s; *p; ++p)
        {
            const int ch = (unsigned char)*p;
            const AGlyph& g = ui_.g[(ch >= 32 && ch < 127) ? ch - 32 : '?' - 32];
            if (g.bw > 0.0f)
            {
                const float x0 = pen + g.bx * em, x1 = x0 + g.bw * em, y0 = y + oy + g.by * em, y1 = y0 + g.bh * em;
                glTexCoord2f(g.u0, g.v1); glVertex2f(x0, y0);
                glTexCoord2f(g.u1, g.v1); glVertex2f(x1, y0);
                glTexCoord2f(g.u1, g.v0); glVertex2f(x1, y1);
                glTexCoord2f(g.u0, g.v0); glVertex2f(x0, y1);
            }
            pen += g.adv * em;
        }
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}

void drawLogo(float cx, float cy, float t, float fade, float grow, float glowBoost)
{
    if (!ready_ || fade <= 0.001f) return;
    glPushMatrix();
    glTranslatef(cx, cy, 0.0f);
    glScalef(1.0f + grow, 1.0f + grow, 1.0f);

    // flourishes either side of "World": drawn on with a growing Bezier parameter
    {
        const float draw = smoothstep((t - 1.3f) / 1.1f);
        const Color gold = rgb(0.98f, 0.80f, 0.38f, 0.95f * fade);
        const Color dark = rgb(0.10f, 0.07f, 0.20f, 0.75f * fade);
        for (int side = -1; side <= 1; side += 2)
        {
            const float s = (float)side;
            const Pt p0{s * 0.36f, -0.185f}, p1{s * 0.60f, -0.24f}, p2{s * 0.70f, -0.12f}, p3{s * 0.98f, -0.19f};
            bezierRibbon(Pt{p0.x, p0.y - 0.006f}, Pt{p1.x, p1.y - 0.006f}, Pt{p2.x, p2.y - 0.006f}, Pt{p3.x, p3.y - 0.006f}, 0.010f, 0.002f, draw, dark);
            bezierRibbon(p0, p1, p2, p3, 0.0075f, 0.0012f, draw, gold);
            if (draw > 0.98f)                                  // diamond at the tip
            {
                glColor4f(gold.r, gold.g, gold.b, gold.a);
                glBegin(GL_QUADS);
                glVertex2f(p3.x - 0.018f * s, p3.y); glVertex2f(p3.x, p3.y + 0.016f);
                glVertex2f(p3.x + 0.018f * s, p3.y); glVertex2f(p3.x, p3.y - 0.016f);
                glEnd();
            }
        }
    }

    glEnable(GL_TEXTURE_2D);
    const int N = (int)letters_.size();
    for (int pass = 0; pass < 5; pass++)
    {
        const int tex = pass == 4 ? 2 : pass;                    // pass 4 = moving shine (fill mask, additive)
        glBindTexture(GL_TEXTURE_2D, logoTex_[tex]);
        if (pass == 0 || pass >= 3) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        else                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        for (int i = 0; i < N; i++)
        {
            const LogoLetter& L = letters_[i];
            if (L.c == ' ' || L.g.bw <= 0.0f) continue;
            // staggered entrance: rise + overshoot scale + fade
            const float p  = clampf((t - 0.10f * i) / 0.85f, 0.0f, 1.0f);
            if (p <= 0.0f) continue;
            const float sc = 0.55f + 0.45f * easeOutBack(p);
            const float a  = smoothstep(p * 1.6f) * fade;
            const float rise = 0.07f * (1.0f - smoothstep(p));
            const float cxl = L.x + L.adv * L.em * 0.5f;            // scale about the letter centre
            auto X = [&](float ex) { return cxl + (L.x + ex * L.em - cxl) * sc; };
            auto Y = [&](float ey) { return L.y + rise + ey * L.em * sc; };
            const float x0 = X(L.g.bx), x1 = X(L.g.bx + L.g.bw), y0 = Y(L.g.by), y1 = Y(L.g.by + L.g.bh);
            const int STRIPS = pass == 4 ? 12 : 1;
            glBegin(GL_QUADS);
            for (int k = 0; k < STRIPS; k++)
            {
                const float s0 = (float)k / STRIPS, s1 = (float)(k + 1) / STRIPS;
                const float xa = lerpf(x0, x1, s0), xb = lerpf(x0, x1, s1);
                const float ua = lerpf(L.g.u0, L.g.u1, s0), ub = lerpf(L.g.u0, L.g.u1, s1);
                Color top, bot;
                if (pass == 0)      { const float pulse = 0.55f + 0.25f * glowBoost + 0.25f * std::sin(PI * p);
                                      top = bot = rgb(1.0f, 0.78f, 0.45f, 0.55f * pulse * a); }
                else if (pass == 1) { top = bot = rgb(0.10f, 0.06f, 0.20f, 0.92f * a); }
                else if (pass == 2) { top = rgb(1.0f, 0.97f, 0.78f, a); bot = rgb(0.96f, 0.58f, 0.18f, a); }
                else if (pass == 3) { top = bot = rgb(1.0f, 1.0f, 1.0f, 0.55f * a); }
                else
                {   // a bright band sweeps once from left to right after the letters have landed
                    const float bc = lerpf(-1.5f, 1.5f, clampf((t - 2.1f) / 1.1f, 0.0f, 1.0f));
                    auto shine = [&](float xw) { const float d = (xw - bc) / 0.16f; return std::exp(-d * d); };
                    const float ka = 0.9f * shine(xa) * a, kb = 0.9f * shine(xb) * a;
                    glColor4f(1.0f, 1.0f, 0.95f, ka); glTexCoord2f(ua, L.g.v1); glVertex2f(xa, y0);
                    glColor4f(1.0f, 1.0f, 0.95f, kb); glTexCoord2f(ub, L.g.v1); glVertex2f(xb, y0);
                    glColor4f(1.0f, 1.0f, 0.95f, kb); glTexCoord2f(ub, L.g.v0); glVertex2f(xb, y1);
                    glColor4f(1.0f, 1.0f, 0.95f, ka); glTexCoord2f(ua, L.g.v0); glVertex2f(xa, y1);
                    continue;
                }
                glColor4f(bot.r, bot.g, bot.b, bot.a); glTexCoord2f(ua, L.g.v1); glVertex2f(xa, y0);
                glColor4f(bot.r, bot.g, bot.b, bot.a); glTexCoord2f(ub, L.g.v1); glVertex2f(xb, y0);
                glColor4f(top.r, top.g, top.b, top.a); glTexCoord2f(ub, L.g.v0); glVertex2f(xb, y1);
                glColor4f(top.r, top.g, top.b, top.a); glTexCoord2f(ua, L.g.v0); glVertex2f(xa, y1);
            }
            glEnd();
        }
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);
    glPopMatrix();
}

}   // namespace font
