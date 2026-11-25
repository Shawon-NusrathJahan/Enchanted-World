#include "animation.h"
#include "gfx.h"

#include <cmath>

using gfx::clampf;

static double    elapsed  = 0.0;      // film clock
static double    titleClk = 0.0;      // title screen clock (only for the logo entrance)
static double    lastTime = 0.0;
static bool      playing  = true;
static bool      waiting  = true;
static AnimState state;

float smootherstep(float u)
{
    u = clampf(u, 0.0f, 1.0f);
    return u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f);
}

static Phase phaseAt(double t)
{
    if (waiting) return PHASE_TITLE;
    if (t < timeline::REVEAL_START)  return PHASE_OPENING;
    if (t < timeline::JOURNEY_START) return PHASE_REVEAL;
    if (t < timeline::GARDEN_START)  return PHASE_JOURNEY;
    if (t < timeline::LIVING_START)  return PHASE_GARDEN_REVEAL;
    return PHASE_LIVING_GARDEN;
}

static double phaseStart(Phase p)
{
    switch (p)
    {
    case PHASE_TITLE:
    case PHASE_OPENING:       return timeline::OPENING_START;
    case PHASE_REVEAL:        return timeline::REVEAL_START;
    case PHASE_JOURNEY:       return timeline::JOURNEY_START;
    case PHASE_GARDEN_REVEAL: return timeline::GARDEN_START;
    default:                  return timeline::LIVING_START;
    }
}

static void recompute()
{
    using namespace timeline;
    const float t = (float)elapsed;

    state.time    = elapsed;
    state.waiting = waiting;
    state.phase   = phaseAt(elapsed);
    state.paused  = !playing;
    state.titleT  = (float)std::min(titleClk, LOGO_IN);
    state.logoFade = waiting ? 1.0f : 1.0f - smootherstep(t / (float)TITLE_OUT);

    state.open = smootherstep((t - (float)REVEAL_START) / (float)(JOURNEY_START - REVEAL_START - 0.6));
    state.gardenBlend = smootherstep((t - (float)GARDEN_START) / (float)(LIVING_START - GARDEN_START));
    state.gardenTime  = elapsed > GARDEN_START ? elapsed - GARDEN_START : 0.0;
}

void initializeAnimation(double currentTime)
{
    elapsed  = 0.0;
    titleClk = 0.0;
    lastTime = currentTime;
    playing  = true;
    waiting  = true;
    recompute();
}

void updateAnimation(double currentTime)
{
    double dt = currentTime - lastTime;
    lastTime  = currentTime;
    if (dt > 0.1) dt = 0.1;            // avoid jumps after window drag / breakpoints
    if (dt < 0.0) dt = 0.0;
    if (waiting) titleClk += dt;       // the logo entrance plays once, then the title screen is still
    else if (playing) elapsed += dt;
    recompute();
}

void startFilm(double currentTime)
{
    if (!waiting) return;
    waiting  = false;
    elapsed  = 0.0;
    titleClk = timeline::LOGO_IN;      // the logo is complete when it starts to leave
    lastTime = currentTime;
    recompute();
}

void toggleAnimation() { playing = !playing; recompute(); }

void resetAnimation(double currentTime) { initializeAnimation(currentTime); }

void jumpToPhase(Phase phase, double currentTime)
{
    if (phase == PHASE_TITLE) { initializeAnimation(currentTime); return; }
    waiting  = false;
    titleClk = timeline::LOGO_IN;
    elapsed  = phaseStart(phase);
    lastTime = currentTime;
    recompute();
}

void seekAnimation(double deltaSeconds, double currentTime)
{
    if (waiting) { waiting = false; titleClk = timeline::LOGO_IN; }
    elapsed += deltaSeconds;
    if (elapsed < 0.0) elapsed = 0.0;
    lastTime = currentTime;
    recompute();
}

const AnimState& getAnimState() { return state; }

const char* phaseName(Phase phase)
{
    switch (phase)
    {
    case PHASE_TITLE:         return "0 Title (click to start)";
    case PHASE_OPENING:       return "1 Mountain Peaks";
    case PHASE_REVEAL:        return "2 Village Reveal";
    case PHASE_JOURNEY:       return "3 Forward Journey";
    case PHASE_GARDEN_REVEAL: return "4 Garden Reveal";
    default:                  return "5 Living Garden (loop)";
    }
}
