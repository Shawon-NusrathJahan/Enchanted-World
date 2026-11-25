// stationsim.h - the TRAIN STATION simulation (no drawing, no OpenGL). station.cpp draws it.
//
// Algorithms (explain these to the supervisor):
//   Platform assignment : greedy INTERVAL PARTITIONING. Services are taken in order of planned
//                         arrival; each gets the platform that is free first (busy-until times).
//                         If no platform is free the arrival is delayed until one is. Re-run
//                         twice a second, so holds and swaps ripple through the timetable.
//                         Platforms are confirmed CONFIRM_AHEAD s before arrival; after that
//                         a conflict only delays the train.
//   Signals             : finite state machines with BLOCK INTERLOCKING. The home signal is red
//                         while the points or the destination platform are occupied (or held);
//                         a starter signal is green only when the train is ready and the exit
//                         block ahead is clear. Points are locked while a train is on them.
//   Train motion        : kinematics. Accelerate at ACCEL to V_MAX; brake so that the train
//                         stops exactly at the target using v^2 = 2 a d  (a = v^2 / 2d).
//   Passenger routing   : DIJKSTRA shortest path (binary-heap priority queue) on a walkway graph
//                         (concourse, platforms, stairs, footbridge, level crossing). Edge cost =
//                         walking time (stairs are slower). A closed bridge or a closed crossing
//                         gate gives an edge infinite cost and every passenger re-routes.
//   Boarding            : per-door FIFO queues, doors chosen by least load (load balancing),
//                         train capacity limit; missed passengers are re-booked on the next train.
//   Arrivals            : Poisson process (exponential gaps, seeded RNG).

#ifndef STATIONSIM_H
#define STATIONSIM_H

#include "station_config.h"

#include <vector>

namespace station
{

struct Input { float x = 0, y = 0; bool valid = false, down = false, pressed = false, released = false; };

// ---- geometry shared by simulation and drawing (world units, y up) -----------------
constexpr float P1_Y = 0.07f, P2_Y = -0.29f;          // passengers' feet on the two platforms
constexpr float TRACK_Y[2] = {-0.12f, -0.52f};        // rail height: track 0 (platform 1), track 1 (platform 2)
constexpr float P1_X0 = -1.70f, P1_X1 = 1.32f;
constexpr float P2_X0 = -1.05f, P2_X1 = 1.32f;
constexpr float PTS_X0 = -1.55f, PTS_X1 = -1.20f;     // crossover (points) from the approach line to track 0
constexpr float HOME_X = -1.64f;                      // home signal (on the approach line = track 1)
constexpr float STARTER_X = 1.30f;                    // starter signals at the platform ends
constexpr float STOP_X = 1.12f;                       // where the train FRONT stops
constexpr float XING_X = 1.50f;                       // level crossing over track 0
constexpr float SPAWN_X = -3.2f, GONE_X = 3.4f;
constexpr float CAR_L = 0.42f, CAR_GAP = 0.025f;
constexpr int   CARS = 3, DOORS = CARS * 2;
constexpr float TRAIN_L = CARS * CAR_L + (CARS - 1) * CAR_GAP;
constexpr float CAR_H = 0.24f;

// the bridge and the stairs (for drawing and clicking)
constexpr float BR1_X = -0.60f, BR1_Y = 0.48f;        // top of the platform-1 stairs
constexpr float BR2_X = -0.50f, BR2_Y = 0.14f;        // top of the platform-2 stairs
constexpr float ENTRY_X = -1.48f;

// walkway graph
enum Node { N_ENTRY, N_GATE, N_P1W, N_P1M, N_P1E, N_P1X, N_BR1, N_BR2, N_P2W, N_P2M, N_P2E, N_P2X, N_COUNT };
constexpr float NODE_X[N_COUNT] = {ENTRY_X, -1.15f, -0.80f, 0.15f, 0.95f, XING_X, BR1_X, BR2_X, -0.30f, 0.40f, 1.00f, XING_X};
constexpr float NODE_Y[N_COUNT] = {P1_Y, P1_Y, P1_Y, P1_Y, P1_Y, P1_Y, BR1_Y, BR2_Y, P2_Y, P2_Y, P2_Y, P2_Y};

enum class SvcState { Scheduled, Approaching, AtPlatform, Departed };
enum class Aspect   { Red, Yellow, Green };

struct Service
{
    int   id = 0;
    char  name[16] = "";              // e.g. "IC 104"
    int   dest = 0;                   // index into destName()
    float planArr = 0;                // timetable arrival (simulation seconds)
    float estArr = 0;                 // after platform planning (>= planArr)
    float dwell = 10;
    int   platform = 0;               // 0 = platform 1 (track 0), 1 = platform 2 (track 1)
    bool  forced = false;             // user swapped it with the points lever
    bool  confirmed = false;          // platform confirmed (CONFIRM_AHEAD before arrival)
    bool  locked = false;             // train is past the points: platform can no longer change
    bool  spawned = false;
    SvcState st = SvcState::Scheduled;
    int   load = 30, alight = 3;
    float actualArr = -1, depTime = -1;
    float flash = 0;                  // platform-change highlight
};

struct Train
{
    enum St { Approach, Dwell, Depart };
    int   svc = 0;                    // service id
    int   track = 1;                  // decided at the points
    bool  routed = false;             // has passed the points decision
    float x = SPAWN_X, v = 0;         // FRONT position and speed
    St    st = Approach;
    float t = 0;                      // seconds in state
    float doors = 0;                  // 0 closed .. 1 open
    int   toAlight = 0;
    float alightT = 0;
    bool  departNow = false;          // user clicked "dispatch now"
    bool  closing = false;            // doors are closing for departure (latched)
    float target = STOP_X;            // current braking target (for the HUD)
};

struct Passenger
{
    enum St { Walk, ToDoor, Wait, Board, Exit, NoRoute, Dragged };
    int   id = 0;
    float x = 0, y = 0;
    St    st = Walk;
    int   ticket = -1;                // service id (-1 = leaving the station)
    std::vector<int> path;            // node list from Dijkstra
    int   pathIdx = 0;                // next node to reach
    int   door = -1;                  // chosen door (0..DOORS-1)
    float waitSince = 0;
    float alpha = 1, phase = 0, facing = 1;
    int   look = 0, skin = 0, coat = 0;
    float routeCost = 0;
    bool  viaBridge = false, viaCrossing = false;
    float flash = 0;
    float fromX = 0, fromY = 0;       // where a drag started
};

struct Popup   { float x = 0, y = 0, age = 0; char text[48] = ""; int color = 0; };
struct LogLine { char text[120] = ""; int color = 0; };

struct Stats
{
    int arrived = 0, departed = 0, delayed = 0, onTime = 0;
    int boarded = 0, alighted = 0, missed = 0, entered = 0, exited = 0;
    int reroutes = 0, platformChanges = 0, interlockRefusals = 0, drags = 0;
    float totalDelay = 0;
};

void init(unsigned seed = 777u);
void reset();
void update(double dt, const Input& in);

// ---- read-only state for drawing / HUD / tests ----
float now();
const std::vector<Service>&   services();      // ordered by planned arrival (recent + upcoming)
const std::vector<Train>&     trains();
const std::vector<Passenger>& passengers();
const std::vector<Popup>&     popups();
const std::vector<LogLine>&   log();
const Stats&                  stats();
const Service*   findService(int id);
const Passenger* findPassenger(int id);
const Train*     trainOn(int platform);          // the train standing at / heading to a platform
const char*      destName(int d);
float  trackY(int track, float x);               // rail height (the crossover bends track 0)
void   doorPos(int platform, int door, float& x, float& y);
Aspect homeAspect();
Aspect starterAspect(int platform);
bool   homeHeld();
bool   starterHeld(int platform);
bool   bridgeOpen();
bool   crossingOpen();
int    pointsTrack();                            // track the points are set for (0 or 1)
bool   pointsLocked();
int    waitingOn(int platform);                  // passengers waiting / heading to a platform
int    doorLoad(int platform, int door);
float  edgeCost(int a, int b);                   // current Dijkstra weight (1e9 = closed)
std::vector<int> shortestPath(int from, int to, float* cost = nullptr);   // Dijkstra
int    hoverPassengerId();
int    hoverTrainSvc();                          // service id of the train under the cursor (-1 none)
int    hoverThing();                             // 0 none 1 home signal 2 starter P1 3 starter P2 4 points 5 bridge 6 entrance 7 crossing
bool   dragging();

// test helpers
void   testSetArrivals(bool on);                 // automatic passenger arrivals on/off
int    testSpawnPassenger(int ticketSvc);        // a passenger at the entrance with this ticket
void   testToggleBridge();
void   testToggleHome();
void   testToggleStarter(int platform);
bool   testSwapPoints();
void   testDropPassenger(int id, float x, float y);

}   // namespace station

#endif
