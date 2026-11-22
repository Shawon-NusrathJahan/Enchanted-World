// theme.h - real-time (device clock) day / afternoon / evening / night themes
//
// The mode is chosen from the computer's LOCAL time and changes by itself
// while the program runs. Change the four start hours below to retune it.
// Every scene reads the same Params, so all scenes always agree.

#ifndef THEME_H
#define THEME_H

#include "gfx.h"

namespace theme
{

enum Mode { MODE_DAY = 0, MODE_AFTERNOON, MODE_EVENING, MODE_NIGHT };

// ---- configurable boundaries (local hour, 0..24) -------------------------
constexpr float DAY_START       = 6.0f;     // 06:00 - 15:00  day
constexpr float AFTERNOON_START = 15.0f;    // 15:00 - 18:00  afternoon
constexpr float EVENING_START   = 18.0f;    // 18:00 - 20:00  evening
constexpr float NIGHT_START     = 20.0f;    // 20:00 - 06:00  night

struct Params
{
    gfx::Color tint;         // multiplied into every scene colour
    gfx::Color skyTop;       // sky gradient (used when skyBlend > 0)
    gfx::Color skyHorizon;
    float skyBlend;          // 0 = original day sky, 1 = themed sky
    float starAlpha;
    float moonAlpha;
    float sunAlpha;
    float sunWarm;           // 0 white sun .. 1 orange sun
    float windowsLit;        // 0..1, house windows glow when > 0.5
};

void        init();
void        update(double dt);       // call every frame (re-reads the clock once a second)
void        snap();                  // jump straight to the target look (screenshots)
Mode        mode();
const Params& cur();
const char* modeName(Mode m);
float       localHour();             // 0..24 (real clock unless overridden)
void        setOverrideHour(float h);   // h < 0 = use the real clock
void        cycleOverride();            // test aid: T key
bool        isOverridden();

gfx::Color  skyColor(const gfx::Color& original, float y);   // themed sky colour at height y
gfx::Color  sun(const gfx::Color& c);                        // fade / warm a sun colour
void        drawStarsAndMoon(float moonX, float moonY, float moonR, float u);   // u = 0..1 loop phase

}   // namespace theme

#endif
