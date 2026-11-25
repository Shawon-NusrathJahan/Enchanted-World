// gardensim.h - the living garden: flowers, gardener, butterflies, pond ripples, windmill
//
// Everything here is a SIMULATION (state + rules), garden.cpp only draws it.
//
// Algorithms (explain these to the supervisor):
//   Flowers     : per-flower state model (water / growth / health) with individual rates
//   Gardener    : finite state machine (InHouse, Leaving, ToPond, Filling, ToRow, ToFlower,
//                 Watering, Returning, Entering, Dragged, UserWalk)
//   Scheduling  : Earliest-Deadline-First priority scheduling with a binary min-heap
//                 (std::priority_queue) - the row whose flowers will become critical first is served first
//   Traversal   : systematic row-by-row (boustrophedon / serpentine) walk from the nearest row end
//   Pathfinding : A* on a waypoint graph (door, garden path, pond bank, row ends)
//   Butterflies : Reynolds steering behaviours (seek, arrival, flee) + fitness-proportionate
//                 (roulette wheel) waypoint selection; four fixed sensitivity personalities.
//                 Fleeing is SMOOTH: the heading turns with a limited angular speed, the speed
//                 changes with a limited acceleration and the flee urgency ramps with distance.

#ifndef GARDENSIM_H
#define GARDENSIM_H

#include "animation.h"
#include "gfx.h"
#include "ui.h"

#include <array>
#include <vector>

namespace gardensim
{

// pond geometry (shared with garden.cpp so clicks hit exactly what is drawn)
constexpr float POND_X = -1.02f, POND_Y = -0.54f, POND_RX = 0.62f, POND_RY = 0.20f;   // pond on the LEFT
constexpr float FALL_X = -1.38f, FALL_Y = -0.375f;          // where the mountain waterfall plunges into the pond

constexpr int   CAN_CAPACITY = 12;        // flowers one can of water serves
constexpr float RIPPLE_LIFE  = 2.4f;      // seconds
constexpr float SHIMMER_TIME = 5.0f;      // seconds a scared butterfly keeps its glow
constexpr float TWO_CM       = 0.21f;     // ~2 cm on a 1280x720 window, in world units

struct Flower
{
    // fixed look
    float x, y, h, size;
    int   petals, cycles, row, generation;
    float phase;
    gfx::Color col;
    // individual simulation constants
    float thirst;       // water lost per second
    float growRate;     // growth gained per second when moist enough
    float frail;        // 0 tough .. 1 fragile
    // changing state
    float growth;       // 0 seed .. 1 full bloom
    float water;        // 0 dry .. 1 soaked
    float health;       // 0 dead .. 1 perfect
    bool  dead;
    float deadTime;
    bool  bloomed = false;  // full bloom reached (scored once)
};

enum class GState { InHouse, Leaving, ToPond, Filling, ToRow, ToFlower, Watering, Returning, Entering, Dragged, UserWalk };

struct Gardener
{
    float  x = 0, y = 0;            // feet position
    float  facing = 1.0f;           // -1 left .. +1 right (smooth turn)
    float  walkPhase = 0.0f;
    float  alpha = 0.0f;            // 0 inside the house
    float  can = 0.0f;              // units of water carried
    float  watering = 0.0f;         // 0..1 pose while pouring
    float  filling = 0.0f;          // 0..1 pose while dipping the can
    float  time = 0.0f;
    bool   walking = false;
    bool   dragged = false;
    float  targetX = 0, targetY = 0;   // destination chosen by the user (drag)
    GState state = GState::InHouse;
};

struct Spark { float x, y, vx, vy, age; bool on; };

struct Butterfly
{
    int    kind = 0;                // 0 highly sensitive .. 3 least sensitive
    float  x = 0, y = 0, vx = 0, vy = 0;
    float  size = 0.04f, flapPhase = 0.0f;
    gfx::Color col{1, 1, 1, 1};
    float  shimmer = 0.0f;          // seconds of glow left (0 = no effect)
    bool   fleeing = false;
    std::array<Spark, 16> sparks{};
    // behaviour internals
    float  flapRate = 14.0f;        // wing beats ease between calm and frightened
    float  wpX = 0, wpY = 0, feedTimer = 0, react = 0, dwell = 0, cooldown = 0, fleeTimer = 0, sparkTimer = 0;
    float  fleeDirX = 0, fleeDirY = 0;
    bool   feeding = false;
    unsigned rng = 1;
};

struct Ripple { float x, y, age; };

void init();                                     // build the static layout (call once)
void reset();                                    // back to the "empty garden" start
void update(double dt, const AnimState& s, const ui::Mouse& m, bool interactive);

void setWindmillHub(float x, float y);
float windAngle();

const std::vector<Flower>&   flowers();
const Gardener&              gardener();
const std::array<Butterfly, 4>& butterflies();
const std::vector<Ripple>&   ripples();

void drawHud();
void toggleHud();

// read-only numbers for tests / HUD
int  thirstyCount();
int  deadCount();
int  bloomCount();
const char* stateName(GState s);

}   // namespace gardensim

#endif
