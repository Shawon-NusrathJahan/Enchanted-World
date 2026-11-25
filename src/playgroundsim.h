// playgroundsim.h - the PLAYGROUND simulation (no drawing, no OpenGL). playground.cpp draws it.
//
// Algorithms (explain these to the supervisor):
//   Swings     : damped, driven pendulum  theta'' = -(G/L) sin(theta) - c*theta' + pump,
//                integrated with semi-implicit (symplectic) Euler in 4 sub-steps.
//                Amplitude is read back from the energy  E = 1/2 L^2 w^2 + G L (1 - cos theta).
//   See-saw    : rigid-body rotation about the pivot. Net torque  G cos(phi) (mL dL - mR dR),
//                inertia  I = mL dL^2 + mR dR^2 + I_beam, ground contact with restitution.
//                LEVER-LAW BALANCING: the heavier child sits at  d = d_light * m_light / m_heavy.
//   Slide      : motion on an inclined plane  a = G (sin(alpha) - mu cos(alpha)),
//                then sliding friction on the ground  a = -mu_g G.
//   Ball       : projectile motion with gravity, restitution on bounces, rolling friction,
//                wall reflections.
//   Consequences: swing over 75 deg -> the child flies off (projectile) and lands; the safety
//                mat (AABB test) decides between a soft landing and an injury. A waxed slide
//                launches fast children off the end. Ball vs child = circle-capsule collision
//                with an impulse response; a fast hit hurts. Injured children go to the hospital.
//   Scheduling : one FIFO queue per ride (multi-server for the 2 swings, pairs for the see-saw),
//                round-robin time quantum RIDE_TIME, and "join the shortest expected wait"
//                (load balancing) when a child picks the next activity.

#ifndef PLAYGROUNDSIM_H
#define PLAYGROUNDSIM_H

#include "playground_config.h"

#include <deque>
#include <vector>

namespace playground
{

enum class Ride   { None = -1, Swing = 0, Seesaw = 1, Slide = 2, Ball = 3 };
enum class KState { Wander, ToQueue, Queued, ToRide, Riding, Playing, Dragged,
                    Falling,     // flying through the air (projectile)
                    Hurt,        // landed badly: crying, waiting for help
                    ToGate,      // walking to the gate (to the hospital)
                    Away,        // in the hospital (not on screen)
                    Returning }; // back from the hospital

struct Input { float x = 0, y = 0; bool valid = false, down = false, pressed = false, released = false; };

// ---- geometry shared by simulation and drawing (world units, y up) ------------------
constexpr float GROUND_Y   = -0.30f;                 // base line of the equipment
constexpr float QUEUE_Y    = -0.50f;                 // queues stand in front of each ride
constexpr float LANE_Y0    = -0.66f, LANE_Y1 = -0.36f;   // children wander in this band
constexpr float BALL_Y     = -0.74f;                 // ground line of the ball game (front)
constexpr float BALL_R     = 0.034f;
constexpr float WALL_X     = 1.70f;

constexpr int   SWINGS = 2;
constexpr float SWING_PIVOT_Y = 0.40f;
constexpr float SWING_X[SWINGS] = {-1.33f, -0.99f};
constexpr float SWING_FRAME_X0 = -1.56f, SWING_FRAME_X1 = -0.76f;

constexpr float SEESAW_X = -0.30f, SEESAW_Y = -0.20f;    // pivot

constexpr float LADDER_X = 0.32f;
constexpr float SLIDE_TOP_X = 0.40f, SLIDE_TOP_Y = 0.14f;
constexpr float SLIDE_BOT_X = 0.90f, SLIDE_BOT_Y = -0.24f;
constexpr float SLIDE_LIP_X = 1.00f;

constexpr float GATE_X = 1.58f, GATE_Y = -0.46f;     // children leave for / return from the hospital here
constexpr float MAT_HW = 0.15f, MAT_HH = 0.03f;      // safety mat half size

struct Kid
{
    int   id = 0;
    float x = 0, y = 0, tx = 0, ty = 0;
    float mass = 30;
    int   skin = 0, hair = 0, shirt = 0, pants = 0;
    KState st = KState::Wander;
    Ride  ride = Ride::None;          // ride being queued for / used
    Ride  lastRide = Ride::None;
    int   seat = -1;                  // swing index, or see-saw side (0 left, 1 right)
    float t = 0;                      // seconds in the current state
    float decideT = 0;                // wander: time until the next decision
    float phase = 0, facing = 1;
    int   slideStage = 0;             // 0 climb, 1 sit at the top, 2 slide, 3 run-out
    float slideS = 0, slideV = 0;     // distance along the slide / speed
    float kickCd = 0, flash = 0;
    int   rides = 0;
    float vx = 0, vy = 0, spin = 0;   // falling (projectile)
    float landY = 0;                  // ground height where the fall ends
    float mood = 0.7f;                // 0 crying .. 1 delighted
    float bandage = 0;                // seconds the plaster stays visible
    int   bumps = 0, injuries = 0;
    bool  serious = false;            // hurt badly -> goes to the hospital
    float hitCd = 0;                  // ball-hit cooldown
    char  why[24] = "";               // what happened (tooltip)
};

struct Mat { float x = 1.32f, y = -0.40f; };      // the draggable safety mat

struct Swing
{
    float theta = 0, omega = 0;       // rad, rad/s
    int   rider = -1;                 // kid id sitting on it
    int   reserved = -1;              // kid id walking to it
    float pushT = 0;                  // visual: hand push
    float rewardCd = 0;               // good-push reward cooldown
    bool  warned = false;             // "too high" warning shown for this ride
    float maxAmp = 0;                 // largest amplitude seen (degrees, statistics)
};

struct Seesaw
{
    float phi = 0, omega = 0;         // phi > 0: LEFT end down
    int   seat[2] = {-1, -1};         // kid ids (left, right)
    bool  mounted[2] = {false, false};
    float arm[2] = {playgroundcfg::SEESAW_HALF - 0.02f, playgroundcfg::SEESAW_HALF - 0.02f};   // seat distance from the pivot
    bool  balance = true;             // lever-law seat placement on/off (click the pivot)
    float t = 0;                      // ride time with both children seated
    float torque = 0;                 // last net torque (for the HUD)
};

struct Ball
{
    float x = 1.25f, h = 0.0f, vx = 0.0f, vh = 0.0f, spin = 0.0f;
    float maxH = 0.0f;
    int   touch = 0;                  // last touched by: 0 nobody, 1 you, 2 a child
    bool  held = false;               // you are holding it (drag to throw)
    float goalFlash = 0; int goalSide = 0;
};

struct Popup   { float x = 0, y = 0, age = 0; char text[48] = ""; int color = 0; };   // 0 green 1 red 2 yellow 3 white
struct LogLine { char text[120] = ""; int color = 0; };

struct Stats
{
    int rides[3] = {0, 0, 0};         // swing, see-saw, slide
    int pushes = 0, kicks = 0, userKicks = 0, bounces = 0, drags = 0, seesawPushes = 0;
    float maxSwingDeg = 0, maxSlideSpeed = 0, maxBallHeight = 0;
    int falls = 0, injuries = 0, goals = 0, userGoals = 0, bumps = 0, bonks = 0, saves = 0, superSlides = 0, returned = 0;
};

void init(unsigned seed = 4242u);
void reset();
void update(double dt, const Input& in);

// ---- read-only state for drawing / HUD / tests ----
const std::vector<Kid>&     kids();
const Swing&                swing(int i);
const Seesaw&               seesaw();
const Ball&                 ball();
const std::deque<int>&      queue(Ride r);          // Swing / Seesaw / Slide
const std::vector<Popup>&   popups();
const std::vector<LogLine>& log();
const Stats&                stats();
const Kid*  findKid(int id);
const Mat&  mat();
float slideWax();                                   // seconds of wax left (0 = normal slide)
bool  hoverSlide();
bool  hoverMat();
bool  matCovers(float x, float y);                  // would a fall landing at (x, y) hit the mat?
int   happyCount();
int   injuredCount();                               // hurt + at the hospital
bool  predictThrow(float& vx, float& vh);           // while holding the ball: release velocity
int   hoverKidId();
int   hoverSwing();                                 // -1 none
bool  hoverSeesaw(int& side);                       // side 0 left, 1 right, 2 pivot
bool  hoverBall();
bool  dragging();
Ride  rideZoneAt(float x, float y);                 // drop target under a point
int   ballPlayers();
float swingAmplitude(int i);                        // rad, from the pendulum energy
float swingPeriod();                                // small-angle period 2*pi*sqrt(L/G)
float expectedWait(Ride r);                         // seconds (load-balancing estimate)
void  swingSeatPos(int i, float& x, float& y);
void  seesawSeatPos(int side, float& x, float& y);
void  slidePoint(float s, float& x, float& y);      // point on the slide surface at distance s
float slideLength();
float slideAngle();
const char* rideName(Ride r);
const char* stateName(const Kid& k);

// test helpers
void  testPushSwing(int i);
void  testKickBall(float vx, float vh);
void  testSetSeesawBalance(bool on);
void  testSendKid(int id, Ride r);                  // same as dropping the kid on that ride
void  testFreeze(bool on);                          // children stop making their own decisions
void  testClearRides();                             // everybody off the rides, queues empty, ball still
void  testWaxSlide();
void  testSetMat(float x, float y);
void  testThrowBall(float x, float h, float vx, float vh);   // as if you threw it

}   // namespace playground

#endif
