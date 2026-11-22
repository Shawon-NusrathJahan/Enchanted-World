#include "village.h"
#include "gfx.h"
#include "theme.h"

#include <algorithm>
#include <vector>

using namespace gfx;

namespace
{

struct Style { Color wall, roof, shutter; };

const Style STYLES[4] = {
    {rgb(0.96f, 0.90f, 0.76f), rgb(0.74f, 0.22f, 0.18f), rgb(0.25f, 0.45f, 0.35f)},   // cream / red / green
    {rgb(0.97f, 0.80f, 0.66f), rgb(0.82f, 0.32f, 0.20f), rgb(0.25f, 0.40f, 0.62f)},   // peach / orange / blue
    {rgb(0.93f, 0.93f, 0.88f), rgb(0.62f, 0.18f, 0.20f), rgb(0.45f, 0.28f, 0.18f)},   // white / dark red / brown
    {rgb(0.82f, 0.88f, 0.92f), rgb(0.80f, 0.38f, 0.24f), rgb(0.65f, 0.22f, 0.22f)},   // blue-grey / terracotta / red
};

enum ItemType { HOUSE, TOWER, ROUND_TREE, PINE, FENCE };

struct Item
{
    ItemType type;
    float    x, y, s;       // position, size (house width / tree height / fence height)
    int      style;
    bool     smoke;
    float    x2;            // fence end x
};

std::vector<Item> items;

// ---------------------------------------------------------------------------
// Local drawing in "house units": house is 1.0 wide, walls 0..0.7, roof to 1.3
// ---------------------------------------------------------------------------
void drawWindow(float cx, float cy, float w, float h, bool lit, const Color& shutter, bool flowerBox)
{
    // shutters
    setColor(shutter);
    rect(cx - w * 0.5f - w * 0.5f, cy - h * 0.5f, cx - w * 0.5f - 0.008f, cy + h * 0.5f);
    rect(cx + w * 0.5f + 0.008f, cy - h * 0.5f, cx + w * 0.5f + w * 0.5f, cy + h * 0.5f);
    setColor(shade(shutter, 0.75f));
    for (int i = 1; i < 4; i++)                       // louvres
    {
        const float yy = cy - h * 0.5f + h * i / 4.0f;
        rect(cx - w - 0.0f, yy - 0.004f, cx - w * 0.5f - 0.008f, yy + 0.004f);
        rect(cx + w * 0.5f + 0.008f, yy - 0.004f, cx + w, yy + 0.004f);
    }
    // frame + glass
    setColor(rgb(0.95f, 0.92f, 0.85f));
    rect(cx - w * 0.5f - 0.014f, cy - h * 0.5f - 0.014f, cx + w * 0.5f + 0.014f, cy + h * 0.5f + 0.014f);
    if (lit || theme::cur().windowsLit > 0.5f)
    {
        const bool oldTint = gfx::useTint;          // lit windows glow: no night tint
        gfx::useTint = false;
        rectV(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f, rgb(0.98f, 0.70f, 0.32f), rgb(1.0f, 0.90f, 0.58f));
        gfx::useTint = oldTint;
    }
    else     rectV(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f, rgb(0.42f, 0.66f, 0.86f), rgb(0.74f, 0.89f, 0.97f));
    // cross bars
    setColor(rgb(0.95f, 0.92f, 0.85f));
    rect(cx - 0.006f, cy - h * 0.5f, cx + 0.006f, cy + h * 0.5f);
    rect(cx - w * 0.5f, cy - 0.006f, cx + w * 0.5f, cy + 0.006f);
    // sill
    setColor(rgb(0.72f, 0.70f, 0.66f));
    rect(cx - w * 0.5f - 0.03f, cy - h * 0.5f - 0.035f, cx + w * 0.5f + 0.03f, cy - h * 0.5f - 0.012f);
    if (flowerBox)
    {
        setColor(rgb(0.45f, 0.28f, 0.16f));
        rect(cx - w * 0.5f - 0.01f, cy - h * 0.5f - 0.07f, cx + w * 0.5f + 0.01f, cy - h * 0.5f - 0.03f);
        static const Color FC[3] = {rgb(0.90f, 0.20f, 0.30f), rgb(1.0f, 0.80f, 0.20f), rgb(0.95f, 0.45f, 0.70f)};
        for (int i = 0; i < 3; i++)
        {
            setColor(rgb(0.20f, 0.55f, 0.25f));
            circle(cx - w * 0.33f + i * w * 0.33f, cy - h * 0.5f - 0.025f, 0.022f, 10);
            setColor(FC[i]);
            circle(cx - w * 0.33f + i * w * 0.33f, cy - h * 0.5f - 0.012f, 0.017f, 10);
        }
    }
}

void drawSmoke(float x, float y, double time)
{
    for (int i = 0; i < 4; i++)
    {
        const float p = fract((float)(time * 0.35) + i * 0.25f);
        const float a = 0.55f * (1.0f - p) * smoothstep(0.0f, 0.12f, p);
        setColor(rgb(0.93f, 0.93f, 0.96f, a));
        circle(x + 0.30f * p + 0.06f * std::sin(p * 6.0f + i), y + 0.9f * p, 0.05f + 0.13f * p, 16);
    }
}

void drawTower(float x, float y, float w, double time)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(w, w, 1.0f);

    setColor(rgb(0.0f, 0.0f, 0.0f, 0.16f));
    ellipse(0.0f, 0.0f, 0.75f, 0.09f);

    rectV(-0.5f, 0.0f, 0.5f, 2.2f, rgb(0.66f, 0.64f, 0.62f), rgb(0.90f, 0.87f, 0.82f));
    setColor(rgb(0.0f, 0.0f, 0.0f, 0.10f));
    rect(0.15f, 0.0f, 0.5f, 2.2f);                                   // shaded side
    setColor(rgb(0.30f, 0.30f, 0.38f));
    for (int i = 0; i < 2; i++)                                      // arched slit windows
    {
        const float yy = 0.9f + i * 0.65f;
        rect(-0.07f, yy, 0.07f, yy + 0.30f);
        circle(0.0f, yy + 0.30f, 0.07f, 12);
    }
    setColor(rgb(0.55f, 0.52f, 0.50f));
    rect(-0.56f, 2.14f, 0.56f, 2.26f);                               // ledge
    setColor(rgb(0.78f, 0.22f, 0.18f));
    tri(-0.68f, 2.24f, 0.68f, 2.24f, 0.0f, 3.5f);                    // cone roof
    setColor(rgb(0.0f, 0.0f, 0.0f, 0.18f));
    tri(0.0f, 2.24f, 0.68f, 2.24f, 0.0f, 3.5f);
    setColor(rgb(0.3f, 0.2f, 0.15f));
    rect(-0.01f, 3.45f, 0.01f, 3.95f);                               // flag pole
    setColor(rgb(0.95f, 0.75f, 0.15f));
    const float fl = 0.04f * std::sin((float)time * 4.0f);
    tri(0.01f, 3.92f, 0.01f, 3.72f, 0.30f + fl, 3.82f);              // waving flag
    glPopMatrix();
}

}   // namespace

// ---------------------------------------------------------------------------
void drawHouse(float x, float y, float w, int style, bool smoke, double time)
{
    const Style& st = STYLES[style & 3];
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(w, w, 1.0f);

    // ground shadow
    setColor(rgb(0.0f, 0.0f, 0.0f, 0.18f));
    ellipse(0.0f, 0.0f, 0.72f, 0.08f);

    // chimney (behind the roof)
    setColor(rgb(0.62f, 0.30f, 0.24f));
    rect(0.20f, 0.85f, 0.34f, 1.20f);
    setColor(rgb(0.45f, 0.20f, 0.16f));
    for (int i = 0; i < 4; i++) rect(0.20f, 0.92f + i * 0.08f, 0.34f, 0.935f + i * 0.08f);
    setColor(rgb(0.50f, 0.48f, 0.46f));
    rect(0.18f, 1.20f, 0.36f, 1.25f);

    // walls + stone base + corner posts
    rectV(-0.48f, 0.08f, 0.48f, 0.70f, shade(st.wall, 0.88f), st.wall);
    setColor(rgb(0.0f, 0.0f, 0.0f, 0.07f));
    rect(0.30f, 0.08f, 0.48f, 0.70f);                                 // soft side shade
    setColor(rgb(0.56f, 0.53f, 0.50f));
    rect(-0.50f, 0.0f, 0.50f, 0.10f);
    setColor(rgb(0.66f, 0.63f, 0.60f));
    rect(-0.50f, 0.075f, 0.50f, 0.10f);
    setColor(rgb(0.40f, 0.27f, 0.18f));
    rect(-0.48f, 0.10f, -0.45f, 0.70f);
    rect(0.45f, 0.10f, 0.48f, 0.70f);

    // roof: alternating tile bands
    for (int k = 0; k < 6; k++)
    {
        const float k0 = k / 6.0f, k1 = (k + 1) / 6.0f;
        const float y0 = 0.68f + 0.62f * k0, y1 = 0.68f + 0.62f * k1;
        const float h0 = 0.62f * (1.0f - k0), h1 = 0.62f * (1.0f - k1);
        setColor(shade(st.roof, (k & 1) ? 0.86f : 1.0f));
        quad(-h0 - 0.0f, y0, h0, y0, h1, y1, -h1, y1);
    }
    setColor(rgb(0.0f, 0.0f, 0.0f, 0.13f));
    tri(0.0f, 0.68f, 0.62f, 0.68f, 0.0f, 1.30f);                      // roof shade (right slope)
    setColor(shade(st.roof, 0.55f));
    rect(-0.64f, 0.66f, 0.64f, 0.71f);                                // eave

    // attic window
    setColor(rgb(0.95f, 0.92f, 0.85f));
    circle(0.0f, 0.90f, 0.085f, 20);
    setColor(rgb(0.45f, 0.70f, 0.88f));
    circle(0.0f, 0.90f, 0.065f, 20);
    setColor(rgb(0.95f, 0.92f, 0.85f));
    rect(-0.005f, 0.835f, 0.005f, 0.965f);
    rect(-0.065f, 0.895f, 0.065f, 0.905f);

    // windows
    drawWindow(-0.30f, 0.42f, 0.16f, 0.22f, false, st.shutter, true);
    drawWindow(0.30f, 0.42f, 0.16f, 0.22f, (style & 1) == 1, st.shutter, true);

    // door with arch, planks, knob, step
    setColor(rgb(0.95f, 0.92f, 0.85f));
    rect(-0.115f, 0.08f, 0.115f, 0.40f);
    circle(0.0f, 0.40f, 0.115f, 20);
    setColor(rgb(0.36f, 0.22f, 0.12f));
    rect(-0.09f, 0.08f, 0.09f, 0.40f);
    circle(0.0f, 0.40f, 0.09f, 20);
    setColor(rgb(0.26f, 0.15f, 0.08f));
    rect(-0.005f, 0.08f, 0.005f, 0.49f);
    rect(-0.05f, 0.08f, -0.045f, 0.42f);
    rect(0.045f, 0.08f, 0.05f, 0.42f);
    setColor(rgb(0.95f, 0.78f, 0.25f));
    circle(0.055f, 0.26f, 0.013f, 10);
    setColor(rgb(0.62f, 0.60f, 0.57f));
    rect(-0.15f, 0.045f, 0.15f, 0.085f);

    if (smoke) drawSmoke(0.27f, 1.26f, time);

    glPopMatrix();
}

void initVillage()
{
    items.clear();
    auto house = [](float x, float y, float w, int st, bool sm)
    { return Item{HOUSE, x, y, w, st, sm, 0.0f}; };

    // back row (small, far)
    items.push_back(house(-0.40f, -0.012f, 0.085f, 0, false));
    items.push_back(house(-0.24f, -0.005f, 0.070f, 1, true));
    items.push_back(house(0.08f, 0.000f, 0.080f, 3, false));
    items.push_back(house(0.22f, -0.012f, 0.085f, 2, false));
    items.push_back(house(0.39f, -0.020f, 0.080f, 0, true));
    items.push_back(Item{TOWER, -0.07f, 0.010f, 0.050f, 0, false, 0.0f});
    items.push_back(Item{TOWER, 0.31f, 0.006f, 0.042f, 0, false, 0.0f});
    // middle row
    items.push_back(house(-0.33f, -0.070f, 0.110f, 2, false));
    items.push_back(house(-0.10f, -0.085f, 0.120f, 0, true));
    items.push_back(house(0.15f, -0.075f, 0.115f, 1, false));
    items.push_back(house(0.40f, -0.090f, 0.110f, 3, true));
    // front row (large, near)
    items.push_back(house(-0.26f, -0.150f, 0.140f, 1, false));
    items.push_back(house(0.10f, -0.165f, 0.150f, 0, true));
    items.push_back(house(0.36f, -0.155f, 0.140f, 2, false));
    // trees
    items.push_back(Item{ROUND_TREE, -0.50f, -0.070f, 0.13f, 0, false, 0});
    items.push_back(Item{ROUND_TREE, 0.53f, -0.100f, 0.13f, 1, false, 0});
    items.push_back(Item{ROUND_TREE, -0.04f, -0.045f, 0.085f, 0, false, 0});
    items.push_back(Item{ROUND_TREE, 0.27f, -0.050f, 0.100f, 1, false, 0});
    items.push_back(Item{ROUND_TREE, -0.04f, -0.185f, 0.10f, 0, false, 0});
    items.push_back(Item{PINE, -0.62f, -0.040f, 0.17f, 0, false, 0});
    items.push_back(Item{PINE, 0.64f, -0.040f, 0.17f, 0, false, 0});
    items.push_back(Item{PINE, -0.58f, -0.130f, 0.15f, 0, false, 0});
    items.push_back(Item{PINE, 0.60f, -0.150f, 0.16f, 0, false, 0});
    // fences
    items.push_back(Item{FENCE, -0.46f, -0.205f, 0.045f, 0, false, -0.32f});
    items.push_back(Item{FENCE, 0.22f, -0.215f, 0.045f, 0, false, 0.34f});
    items.push_back(Item{FENCE, 0.44f, -0.200f, 0.045f, 0, false, 0.60f});

    // far -> near (painter's algorithm)
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.y > b.y; });
}

void drawVillage(double time)
{
    const float t = (float)time;

    for (const Item& it : items)
    {
        switch (it.type)
        {
        case HOUSE:
            if (visible(it.x, it.y + it.s * 0.7f, it.s * 1.0f))
                drawHouse(it.x, it.y, it.s, it.style, it.smoke, time);
            break;
        case TOWER:
            if (visible(it.x, it.y + it.s * 1.8f, it.s * 2.2f))
                drawTower(it.x, it.y, it.s, time);
            break;
        case ROUND_TREE:
            if (visible(it.x, it.y + it.s * 0.6f, it.s * 0.6f))
                drawRoundTree(it.x, it.y, it.s, std::sin(t * 1.4f + it.x * 9.0f),
                              rgb(0.16f, 0.46f, 0.22f), rgb(0.34f, 0.64f, 0.30f), it.style == 1);
            break;
        case PINE:
            if (visible(it.x, it.y + it.s * 0.5f, it.s * 0.5f))
                drawPine(it.x, it.y, it.s, 0.015f * std::sin(t * 1.2f + it.x * 7.0f));
            break;
        case FENCE:
            if (visible(0.5f * (it.x + it.x2), it.y, std::fabs(it.x2 - it.x)))
                drawFence(std::min(it.x, it.x2), std::max(it.x, it.x2), it.y, it.s, rgb(0.97f, 0.94f, 0.86f));
            break;
        }
    }
}
