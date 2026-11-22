#include "theme.h"

#include <cmath>
#include <ctime>

using namespace gfx;

namespace theme
{
namespace
{

Params current{};
Params target{};
Mode   curMode      = MODE_DAY;
float  overrideHour = -1.0f;
float  cachedHour   = 12.0f;
double sinceRead    = 100.0;
bool   started      = false;

Mode modeForHour(float h)
{
    if (h >= DAY_START && h < AFTERNOON_START)       return MODE_DAY;
    if (h >= AFTERNOON_START && h < EVENING_START)   return MODE_AFTERNOON;
    if (h >= EVENING_START && h < NIGHT_START)       return MODE_EVENING;
    return MODE_NIGHT;
}

Params paramsFor(Mode m)
{
    switch (m)
    {
    case MODE_DAY:
        return Params{rgb(1.00f, 1.00f, 1.00f), rgb(0.30f, 0.55f, 0.90f), rgb(0.80f, 0.90f, 0.97f),
                      0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    case MODE_AFTERNOON:
        return Params{rgb(1.00f, 0.93f, 0.80f), rgb(0.35f, 0.58f, 0.90f), rgb(1.00f, 0.85f, 0.58f),
                      0.80f, 0.0f, 0.0f, 1.0f, 0.5f, 0.0f};
    case MODE_EVENING:
        return Params{rgb(0.96f, 0.68f, 0.62f), rgb(0.22f, 0.20f, 0.48f), rgb(1.00f, 0.46f, 0.28f),
                      1.0f, 0.25f, 0.0f, 0.85f, 1.0f, 1.0f};
    default:
        return Params{rgb(0.33f, 0.40f, 0.70f), rgb(0.015f, 0.03f, 0.12f), rgb(0.09f, 0.14f, 0.30f),
                      1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f};
    }
}

void blend(Params& a, const Params& b, float k)
{
    a.tint       = mix(a.tint, b.tint, k);
    a.skyTop     = mix(a.skyTop, b.skyTop, k);
    a.skyHorizon = mix(a.skyHorizon, b.skyHorizon, k);
    a.skyBlend   = lerpf(a.skyBlend, b.skyBlend, k);
    a.starAlpha  = lerpf(a.starAlpha, b.starAlpha, k);
    a.moonAlpha  = lerpf(a.moonAlpha, b.moonAlpha, k);
    a.sunAlpha   = lerpf(a.sunAlpha, b.sunAlpha, k);
    a.sunWarm    = lerpf(a.sunWarm, b.sunWarm, k);
    a.windowsLit = lerpf(a.windowsLit, b.windowsLit, k);
}

void readClock()
{
    if (overrideHour >= 0.0f) { cachedHour = overrideHour; return; }
    const std::time_t t  = std::time(nullptr);
    const std::tm*    lt = std::localtime(&t);
    if (lt) cachedHour = lt->tm_hour + lt->tm_min / 60.0f + lt->tm_sec / 3600.0f;
}

void retarget()
{
    readClock();
    curMode = modeForHour(cachedHour);
    target  = paramsFor(curMode);
}

}   // namespace

void init()
{
    retarget();
    current = target;
    started = true;
}

void update(double dt)
{
    if (!started) init();
    sinceRead += dt;
    if (sinceRead >= 1.0) { sinceRead = 0.0; retarget(); }       // follows the real clock while running
    const float k = 1.0f - std::exp(-(float)dt / 1.2f);          // ~1.2 s smooth cross-fade between modes
    blend(current, target, k);
}

void snap() { retarget(); current = target; }

Mode          mode()    { return curMode; }
const Params& cur()     { return current; }
float         localHour() { return cachedHour; }
bool          isOverridden() { return overrideHour >= 0.0f; }

void setOverrideHour(float h) { overrideHour = h; sinceRead = 100.0; retarget(); }

void cycleOverride()      // none -> day -> afternoon -> evening -> night -> none
{
    if (overrideHour < 0.0f)               overrideHour = 12.0f;
    else if (overrideHour < AFTERNOON_START) overrideHour = 16.5f;
    else if (overrideHour < EVENING_START)   overrideHour = 19.0f;
    else if (overrideHour < NIGHT_START)     overrideHour = 22.0f;
    else                                     overrideHour = -1.0f;
    sinceRead = 100.0;
    retarget();
}

const char* modeName(Mode m)
{
    switch (m)
    {
    case MODE_DAY:       return "DAY";
    case MODE_AFTERNOON: return "AFTERNOON";
    case MODE_EVENING:   return "EVENING";
    default:             return "NIGHT";
    }
}

Color skyColor(const Color& original, float y)
{
    const float t    = clampf(y / 1.3f, 0.0f, 1.0f);
    const Color grad = mix(current.skyHorizon, current.skyTop, std::pow(t, 0.8f));
    return mix(original, Color{grad.r, grad.g, grad.b, original.a}, current.skyBlend);
}

Color sun(const Color& c)
{
    const Color warm = rgb(1.0f, 0.62f, 0.30f, c.a);
    Color r = mix(c, warm, current.sunWarm * 0.8f);
    r.a = c.a * current.sunAlpha;
    return r;
}

void drawStarsAndMoon(float mx, float my, float mr, float u)
{
    const bool oldTint = useTint;
    useTint = false;

    if (current.starAlpha > 0.01f)
    {
        glBegin(GL_QUADS);
        for (int i = 0; i < 80; i++)
        {
            const float x  = lerpf(-2.2f, 2.2f, hash1(i * 3.1f + 0.5f));
            const float y  = lerpf(0.12f, 1.25f, hash1(i * 7.7f + 1.5f));
            const float sz = 0.0035f + 0.0055f * hash1(i * 5.3f);
            const float tw = 0.65f + 0.35f * wave(u, 20.0f + (float)(i % 9) * 3.0f, hash1(i * 2.9f));   // whole cycles per loop
            glColor4f(1.0f, 0.98f, 0.90f, current.starAlpha * tw);
            glVertex2f(x - sz, y); glVertex2f(x, y - sz); glVertex2f(x + sz, y); glVertex2f(x, y + sz);
        }
        glEnd();
    }

    if (current.moonAlpha > 0.01f)
    {
        const float a = current.moonAlpha;
        glow(mx, my, mr * 5.0f, rgb(0.70f, 0.78f, 1.0f, 0.30f * a), 40);
        glColor4f(0.96f, 0.96f, 0.88f, a);
        circle(mx, my, mr, 36);
        glColor4f(0.80f, 0.80f, 0.74f, 0.55f * a);                      // craters
        circle(mx - 0.30f * mr, my + 0.25f * mr, 0.20f * mr, 14);
        circle(mx + 0.28f * mr, my - 0.20f * mr, 0.28f * mr, 14);
        circle(mx + 0.10f * mr, my + 0.50f * mr, 0.12f * mr, 12);
    }
    useTint = oldTint;
}

}   // namespace theme
