// animation.h  -  central, time based animation controller
//
// One clock drives everything. The controller turns elapsed seconds into a
// small AnimState that the scenes read (they never keep their own timers).
//
// The film does NOT start by itself: the program opens on the title screen
// ("Enchanted World"), which waits - completely still once the logo has
// appeared - until the user clicks anywhere. Only then does the clock run.

#ifndef ANIMATION_H
#define ANIMATION_H

enum Phase
{
    PHASE_TITLE = 0,        // title screen, waiting for the first click (film clock stopped)
    PHASE_OPENING,          // title fades, camera flies to the mountain peaks (several close views)
    PHASE_REVEAL,           // the great mountains part left/right, the village on its plateau appears
    PHASE_JOURNEY,          // zoom toward the village
    PHASE_GARDEN_REVEAL,    // village dissolves into the garden
    PHASE_LIVING_GARDEN     // looping garden (runs forever)
};

// ---- Timeline (film seconds after the click). Change these to retime the film. ----
namespace timeline
{
constexpr double OPENING_START = 0.0;
constexpr double TITLE_OUT     = 1.8;    // logo has faded away
constexpr double REVEAL_START  = 14.6;   // mountains start parting sideways
constexpr double JOURNEY_START = 20.4;   // village fully revealed: the film WAITS here for a click, then the zoom begins
constexpr double GARDEN_START  = 27.4;   // garden starts fading in + animating
constexpr double LIVING_START  = 30.4;   // garden fully visible, loops forever
constexpr double LOGO_IN       = 3.6;    // seconds the logo needs to appear on the title screen
}

struct AnimState
{
    double time;          // film seconds since the click (0 on the title screen, frozen while paused)
    Phase  phase;
    bool   waiting;       // title screen: waiting for the first click
    float  titleT;        // seconds since the title screen appeared (stops at LOGO_IN -> static)
    float  logoFade;      // 1 = logo fully visible .. 0 = gone
    float  open;          // 0 = great mountains closed .. 1 = parted (smootherstep)
    float  gardenBlend;   // 0 = village only .. 1 = garden only
    double gardenTime;    // seconds since the garden started animating
    bool   paused;
    bool   villageWait;   // the village hub: revealed, the film waits while you pick a place
    float  holdT;         // seconds spent waiting at the village (for the small hint)
    double ambient;       // always-running seconds (water, people, smoke keep moving while waiting)
};

void initializeAnimation(double currentTime);
void updateAnimation(double currentTime);
void startFilm(double currentTime);                             // the first click on the title screen
void continueFilm();                                            // village hub: the Garden was clicked (zoom into it)
void goToVillageFilm(double currentTime);                           // MAP button: back to the village hub (film waits there)
void toggleAnimation();
void resetAnimation(double currentTime);                        // back to the title screen
void jumpToPhase(Phase phase, double currentTime);
void seekAnimation(double deltaSeconds, double currentTime);    // skip forward / back

const AnimState& getAnimState();
const char*      phaseName(Phase phase);

// easing used everywhere: quintic "smootherstep"  6u^5 - 15u^4 + 10u^3  (zero 1st AND 2nd derivative at the ends)
float smootherstep(float u);

#endif
