// gardensim.h - the living garden: flowers, gardener, butterflies, pond ripples, windmill
//
// Everything here is a SIMULATION (state + rules), garden.cpp only draws it.
//
// Algorithms (explain these to the supervisor):
//   Flowers     : per-flower state model (water / growth / health) with individual rates
//   Gardener    : finite state machine (InHouse, Leaving, ToPond, Filling, ToRow, ToFlower,
//                 Watering, Returning, Entering, Dragged, UserWalk)
//                 You carry him like the playground children: press + drag, release = drop.
//                 Drop on the door stone -> he goes in; drop on the house -> he stays outside;
//                 press the house wall while he is inside -> he comes out in your hand.
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
#include "cgmath.h"
#include "gfx.h"
#include "ui.h"

#include <array>
#include <vector>

namespace gardensim
{

// ---- garden geometry (shared with garden.cpp so clicks hit exactly what is drawn) ----
// The lake on the LEFT runs off the left and bottom edges of the screen (part of the big
// water body seen in the film); a small cove reaches into the garden where the can is filled.
// Its shore is a polygon; a wooden bridge crosses it to the far bank with more flowers.
const std::vector<cg::V2>& lakeShore();          // shore polyline, far-left -> right -> bottom
bool  inLake(float x, float y);                  // point in polygon (ray casting)
constexpr float BRIDGE_X0 = -0.50f, BRIDGE_Y0 = -0.835f;     // near end (on the garden side)
constexpr float BRIDGE_X1 = -1.25f, BRIDGE_Y1 = -0.345f;    // far end (on the far bank)
constexpr float BRIDGE_ARCH = 0.10f;                        // arch height at mid-span
cg::V2 bridgeAt(float u);                         // deck point, u = 0 near end .. 1 far end
bool  onBridge(float x, float y, float* u = nullptr);
constexpr float FILL_X = -0.22f, FILL_Y = -0.675f;          // where the gardener fills the can (the cove)

// cottage: door, the stepping stone in front of it, walls, the garden gate in the fence
constexpr float DOOR_X = 1.12f, DOOR_Y = -0.205f;
constexpr float STONE_X = 1.105f, STONE_Y = -0.285f;        // the first stepping stone at the gate: drop him here -> he goes INTO the house
constexpr float HOUSE_X0 = 0.81f, HOUSE_X1 = 1.43f, HOUSE_Y0 = -0.17f, HOUSE_Y1 = 0.28f;   // the walls
constexpr float FENCE_Y = -0.235f, GATE_X0 = 1.03f, GATE_X1 = 1.21f;
float gateOpen();                                 // 0 closed .. 1 open (opens when the gardener passes)

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

enum class Drop { Ground, House, IntoHouse, Water, Bridge };   // what happened at the last drop

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
Drop lastDrop();
const char* stateName(GState s);

}   // namespace gardensim

#endif
