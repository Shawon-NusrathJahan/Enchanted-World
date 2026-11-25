// playgroundsim.cpp - rules and physics of the playground (see playgroundsim.h for the algorithm list)

#include "playgroundsim.h"

#include "cgmath.h"
#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>

namespace playground
{
using namespace playgroundcfg;
using gfx::clampf;

namespace
{

std::mt19937          rng(4242u);
unsigned              seed0 = 4242u;
std::vector<Kid>      kidsV;
Swing                 sw[SWINGS];
Seesaw                ss;
Ball                  bl;
std::deque<int>       qs[3];
std::vector<Popup>    pops;
std::vector<LogLine>  logV;
Stats                 st;
int                   dragId = -1;
int                   pendId = -1;             // pressed on a child, drag starts once the mouse moves
float                 pendX = 0, pendY = 0;
float                 dragDX = 0, dragDY = 0;
bool                  frozen = false;
Input                 lastIn;
float                 seesawRest = 0;          // seconds an end has been resting on the ground
float                 seesawWait = 0;          // a lone child waiting for a partner
Mat                   mat_;
float                 waxT = 0;                // slide wax left
int                   matDrag = 0;             // 1 = dragging the mat
float                 matDX = 0, matDY = 0;
int                   ballPend = 0;            // pressed on the ball (drag = throw, click = kick)
float                 ballPX = 0, ballPY = 0;
struct Sample { float x, y, t; };
std::deque<Sample>    ballTrail;               // recent cursor samples while holding the ball
float                 simT = 0;
int                   funRides = 0, funPushes = 0;
float                 funT = 0;

const std::deque<int> EMPTY_Q;

float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
float rnd(float a, float b) { return a + (b - a) * rnd(); }

void say(int color, const char* fmt, ...)
{
    LogLine l; l.color = color;
    va_list ap; va_start(ap, fmt); std::vsnprintf(l.text, sizeof(l.text), fmt, ap); va_end(ap);
    logV.push_back(l);
    if (logV.size() > 30) logV.erase(logV.begin());
}

void popup(float x, float y, int color, const char* fmt, ...)
{
    Popup p; p.x = x; p.y = y; p.color = color;
    va_list ap; va_start(ap, fmt); std::vsnprintf(p.text, sizeof(p.text), fmt, ap); va_end(ap);
    pops.push_back(p);
}

Kid* byId(int id)
{
    for (Kid& k : kidsV) if (k.id == id) return &k;
    return nullptr;
}

bool moveToward(Kid& k, float tx, float ty, float speed, float dt)
{
    const float dx = tx - k.x, dy = ty - k.y, d = std::sqrt(dx * dx + dy * dy);
    if (d < 1e-4f) return true;
    if (std::fabs(dx) > 0.004f) k.facing = dx > 0 ? 1.0f : -1.0f;
    const float s = speed * dt;
    if (d <= s) { k.x = tx; k.y = ty; return true; }
    k.x += dx / d * s; k.y += dy / d * s;
    k.phase += dt * 11.0f * (speed / WALK_SPEED);
    return false;
}

float slideLen() { return std::hypot(SLIDE_BOT_X - SLIDE_TOP_X, SLIDE_TOP_Y - SLIDE_BOT_Y); }
float slideAng() { return std::atan2(SLIDE_TOP_Y - SLIDE_BOT_Y, SLIDE_BOT_X - SLIDE_TOP_X); }
float seesawMaxAngle() { return std::asin((SEESAW_Y - GROUND_Y - 0.015f) / SEESAW_HALF); }

void queueSpot(Ride r, int idx, float& x, float& y)
{
    const float head = r == Ride::Swing ? -1.44f : r == Ride::Seesaw ? -0.60f : 0.20f;
    x = head + idx * 0.085f;
    y = QUEUE_Y - (idx % 2) * 0.012f;
}

void removeFromQueues(int id)
{
    for (auto& q : qs) q.erase(std::remove(q.begin(), q.end(), id), q.end());
}

void releaseReservation(Kid& k)     // the kid was walking to a seat and stops doing so
{
    for (Swing& s : sw) if (s.reserved == k.id) s.reserved = -1;
    for (int i = 0; i < 2; i++) if (ss.seat[i] == k.id) { ss.seat[i] = -1; ss.mounted[i] = false; ss.t = 0; }
}

void wanderTarget(Kid& k)
{
    k.tx = rnd(-1.60f, 1.60f);
    k.ty = rnd(LANE_Y0, LANE_Y1);
}

void becomeFree(Kid& k, Ride finished)
{
    k.st = KState::Wander;
    k.ride = Ride::None;
    k.seat = -1;
    k.t = 0;
    if (finished != Ride::None) k.lastRide = finished;
    k.decideT = rnd(1.5f, 4.0f);
    wanderTarget(k);
}

void cheer(Kid& k, float amount) { k.mood = std::min(1.0f, k.mood + amount); }

// a child starts flying through the air (projectile motion until it reaches landY)
void startFall(Kid& k, float x, float y, float vx, float vy, float landY, const char* why)
{
    removeFromQueues(k.id);
    releaseReservation(k);
    k.st = KState::Falling; k.ride = Ride::None; k.seat = -1; k.t = 0;
    k.x = x; k.y = y; k.vx = vx; k.vy = vy; k.spin = 0; k.landY = landY;
    std::snprintf(k.why, sizeof(k.why), "%s", why);
    st.falls++;
}

// hurt: cry for a moment; serious injuries go to the hospital afterwards
void injure(Kid& k, bool serious, int delta, const char* what)
{
    removeFromQueues(k.id);
    releaseReservation(k);
    for (Swing& s : sw) if (s.rider == k.id) s.rider = -1;
    k.st = KState::Hurt; k.ride = Ride::None; k.seat = -1; k.t = 0;
    k.mood = 0.0f; k.serious = serious; k.bandage = serious ? 0.0f : 8.0f;
    if (serious) { k.injuries++; st.injuries++; }
    game::addAt(game::Where::Playground, delta, k.x, k.y + 0.30f, "%s", what);
    popup(k.x, k.y + 0.34f, 1, serious ? "OUCH! HURT" : "WAAAH!");
}

int playersOnBall()
{
    int n = 0;
    for (const Kid& k : kidsV) if (k.st == KState::Playing) n++;
    return n;
}

// ---------------------------------------------------------------------------
// Load balancing: expected waiting time of each activity
// ---------------------------------------------------------------------------
float waitOf(Ride r)
{
    switch (r)
    {
    case Ride::Swing:
    {
        int busy = 0;
        for (const Swing& s : sw) if (s.rider >= 0 || s.reserved >= 0) busy++;
        const float n = (float)qs[0].size();
        return (n / SWINGS + (busy >= SWINGS ? 0.5f : 0.0f)) * (RIDE_TIME + 4.0f);
    }
    case Ride::Seesaw:
    {
        const bool used = ss.seat[0] >= 0 || ss.seat[1] >= 0;
        const float n = (float)qs[1].size();
        float w = (std::floor(n / 2.0f) + (used ? 1.0f : 0.0f)) * (RIDE_TIME + 4.0f);
        if (((int)n % 2) == 0) w += 4.0f;           // a new child must wait for a partner
        return w;
    }
    case Ride::Slide:
    {
        int busy = 0;
        for (const Kid& k : kidsV) if (k.ride == Ride::Slide && (k.st == KState::ToRide || k.st == KState::Riding)) busy++;
        return (float)(qs[2].size() + busy) * 5.0f;
    }
    case Ride::Ball:  return playersOnBall() < BALL_MAX_PLAYERS ? 2.0f : 1e9f;
    default:          return 1e9f;
    }
}

void joinRide(Kid& k, Ride r, bool byUser)
{
    removeFromQueues(k.id);
    releaseReservation(k);
    k.t = 0;
    if (r == Ride::Ball)
    {
        if (playersOnBall() >= BALL_MAX_PLAYERS)
        {
            if (byUser) popup(k.x, k.y + 0.30f, 1, "BALL GAME FULL (%d)", BALL_MAX_PLAYERS);
            becomeFree(k, Ride::None);
            return;
        }
        k.st = KState::Playing; k.ride = Ride::Ball;
        if (byUser) popup(k.x, k.y + 0.30f, 0, "JOINS THE BALL GAME");
        return;
    }
    qs[(int)r].push_back(k.id);
    k.st = KState::ToQueue; k.ride = r;
    if (byUser) popup(k.x, k.y + 0.30f, 0, "QUEUE %s: PLACE %d", rideName(r), (int)qs[(int)r].size());
}

void decide(Kid& k)
{
    const Ride opts[4] = {Ride::Swing, Ride::Seesaw, Ride::Slide, Ride::Ball};
    Ride  best = Ride::None;
    float bestCost = 1e9f;
    for (Ride r : opts)
    {
        const float c = waitOf(r) + (r == k.lastRide ? REPEAT_PENALTY : 0.0f) + rnd(0.0f, 4.0f);
        if (c < bestCost) { bestCost = c; best = r; }
    }
    if (best == Ride::None || bestCost > PATIENCE) { wanderTarget(k); return; }
    joinRide(k, best, false);
}

// ---------------------------------------------------------------------------
// Dispatcher: FIFO queues -> free seats
// ---------------------------------------------------------------------------
void computeArms()
{
    const float dmax = SEESAW_HALF - 0.02f;
    ss.arm[0] = ss.arm[1] = dmax;
    const Kid* a = ss.seat[0] >= 0 ? byId(ss.seat[0]) : nullptr;
    const Kid* b = ss.seat[1] >= 0 ? byId(ss.seat[1]) : nullptr;
    if (!ss.balance || !a || !b) return;
    // lever law  m1 d1 = m2 d2 : the heavier child moves towards the pivot
    if (a->mass > b->mass) ss.arm[0] = std::max(SEESAW_MIN_ARM, dmax * b->mass / a->mass);
    else                   ss.arm[1] = std::max(SEESAW_MIN_ARM, dmax * a->mass / b->mass);
}

void sendToSeat(Kid& k, Ride r, int seat)
{
    k.st = KState::ToRide; k.ride = r; k.seat = seat; k.t = 0;
    if (r == Ride::Swing)       { k.tx = SWING_X[seat]; k.ty = GROUND_Y - 0.05f; }
    else if (r == Ride::Seesaw) { k.tx = SEESAW_X + (seat == 0 ? -1.0f : 1.0f) * ss.arm[seat]; k.ty = GROUND_Y - 0.05f; }
    else                        { k.tx = LADDER_X - 0.01f; k.ty = GROUND_Y; }
}

void dispatch()
{
    // swings: a 2-server queue
    for (int i = 0; i < SWINGS; i++)
    {
        Swing& s = sw[i];
        if (s.rider >= 0 || s.reserved >= 0 || qs[0].empty()) continue;
        const int id = qs[0].front(); qs[0].pop_front();
        Kid* k = byId(id); if (!k) continue;
        s.reserved = id;
        sendToSeat(*k, Ride::Swing, i);
    }
    // see-saw: children go in pairs (or one fills the empty side)
    {
        const int empty = (ss.seat[0] < 0) + (ss.seat[1] < 0);
        if ((empty == 2 && qs[1].size() >= 2) || (empty == 1 && !qs[1].empty()))
        {
            for (int side = 0; side < 2; side++)
            {
                if (ss.seat[side] >= 0 || qs[1].empty()) continue;
                ss.seat[side] = qs[1].front(); qs[1].pop_front();
            }
            computeArms();
            for (int side = 0; side < 2; side++)
            {
                Kid* k = byId(ss.seat[side]);
                if (k && !ss.mounted[side]) sendToSeat(*k, Ride::Seesaw, side);
            }
            ss.t = 0;
        }
    }
    // slide: the ladder takes one child at a time
    {
        bool ladderBusy = false;
        for (const Kid& k : kidsV)
            if (k.ride == Ride::Slide && (k.st == KState::ToRide || (k.st == KState::Riding && k.slideStage <= 1))) ladderBusy = true;
        if (!ladderBusy && !qs[2].empty())
        {
            const int id = qs[2].front(); qs[2].pop_front();
            if (Kid* k = byId(id)) sendToSeat(*k, Ride::Slide, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------
float ampOf(const Swing& s)
{
    const float L = SWING_LENGTH;
    const float E = 0.5f * L * L * s.omega * s.omega + G * L * (1.0f - std::cos(s.theta));
    return std::acos(std::max(-1.0f, std::min(1.0f, 1.0f - E / (G * L))));
}

void updateSwings(float dt)
{
    const int   SUB = 4;
    const float h = dt / SUB, L = SWING_LENGTH;
    for (int i = 0; i < SWINGS; i++)
    {
        Swing& s = sw[i];
        Kid* r = s.rider >= 0 ? byId(s.rider) : nullptr;
        for (int k = 0; k < SUB; k++)
        {
            const float amp = ampOf(s);
            float a = -(G / L) * std::sin(s.theta) - SWING_DAMPING * s.omega;
            if (r && r->t < RIDE_TIME && amp < SWING_PUMP_LIMIT) a += SWING_PUMP * (s.omega >= 0.0f ? 1.0f : -1.0f);
            if (r && r->t >= RIDE_TIME) a -= 0.6f * s.omega;                  // feet drag to stop
            if (!r) a -= (s.reserved >= 0 ? 2.5f : 0.5f) * s.omega;            // empty swing settles
            s.omega += a * h;                                                  // semi-implicit Euler
            s.theta += s.omega * h;
            // pushed far too high: at the top of the arc the child loses grip and flies off
            if (r && amp > SWING_FALL_AMP && std::fabs(s.omega) < 0.8f)
            {
                float x, y;
                swingSeatPos(i, x, y);
                const float vx = SWING_LENGTH * s.omega * std::cos(s.theta), vy = SWING_LENGTH * s.omega * std::sin(s.theta);
                startFall(*r, x, y + 0.02f, vx + (s.theta > 0 ? 0.25f : -0.25f), vy + 0.30f, GROUND_Y - 0.06f, "fell off the swing");
                popup(x, y + 0.30f, 1, "HE LET GO!");
                s.rider = -1; s.warned = false;
                r = nullptr;
            }
        }
        if (r && ampOf(s) > SWING_DANGER_AMP && !s.warned)
        {
            s.warned = true;
            float x, y; swingSeatPos(i, x, y);
            popup(x, y + 0.34f, 1, "TOO HIGH! CAREFUL!");
            r->mood = std::max(0.0f, r->mood - 0.3f);
        }
        s.rewardCd = std::max(0.0f, s.rewardCd - dt);
        s.pushT = std::max(0.0f, s.pushT - dt);
        const float deg = ampOf(s) * 57.2958f;
        s.maxAmp = std::max(s.maxAmp, deg);
        if (r) st.maxSwingDeg = std::max(st.maxSwingDeg, deg);

        if (r && r->t >= RIDE_TIME && ampOf(s) < 0.14f)                        // time quantum used up
        {
            float x, y;
            swingSeatPos(i, x, y);
            r->x = x; r->y = GROUND_Y - 0.06f;
            r->rides++; st.rides[0]++; funRides++; cheer(*r, 0.2f);
            say(2, "KID #%d LEAVES SWING %d AFTER %.0fS (ROUND ROBIN)", r->id, i + 1, RIDE_TIME);
            becomeFree(*r, Ride::Swing);
            s.rider = -1; s.warned = false;
        }
    }
}

void updateSeesaw(float dt)
{
    const float pmax = seesawMaxAngle();
    const Kid* kl = ss.seat[0] >= 0 && ss.mounted[0] ? byId(ss.seat[0]) : nullptr;
    const Kid* kr = ss.seat[1] >= 0 && ss.mounted[1] ? byId(ss.seat[1]) : nullptr;
    const float mL = kl ? kl->mass : 0.0f, mR = kr ? kr->mass : 0.0f;
    const bool  riding = kl && kr && ss.t < RIDE_TIME;
    const int   SUB = 4;
    const float h = dt / SUB;
    for (int k = 0; k < SUB; k++)
    {
        const float I = mL * ss.arm[0] * ss.arm[0] + mR * ss.arm[1] * ss.arm[1] + SEESAW_BEAM_I;
        const float tau = G * std::cos(ss.phi) * (mL * ss.arm[0] - mR * ss.arm[1]);
        ss.torque = tau;
        const float a = tau / I - SEESAW_DAMPING * ss.omega;
        ss.omega += a * h;
        ss.phi += ss.omega * h;
        for (int side = 0; side < 2; side++)
        {
            const float sg = side == 0 ? 1.0f : -1.0f;              // side 0 down = phi at +pmax
            if (sg * ss.phi < pmax) continue;
            ss.phi = sg * pmax;
            if (sg * ss.omega > 0.0f)
            {
                // bounce (restitution), and the child on that end pushes off with his legs:
                // the beam leaves the ground with at least the push speed
                const Kid* pusher = side == 0 ? kl : kr;
                if (riding && pusher && std::fabs(ss.omega) > SEESAW_SLAM)      // slammed into the ground
                {
                    Kid* k = byId(pusher->id);
                    k->bumps++; st.bumps++;
                    k->mood = std::max(0.0f, k->mood - 0.4f);
                    float x, y; seesawSeatPos(side, x, y);
                    popup(x, y + 0.32f, 1, "BUMP!");
                    game::addAt(game::Where::Playground, SCORE_BUMP, x, y + 0.3f, "Ouch! The see-saw slammed down on kid #%d", k->id);
                    if (k->bumps % 2 == 0) ss.t = RIDE_TIME;                   // crying: the ride is over
                }
                const float push = riding && pusher ? SEESAW_LEG_PUSH : 0.0f;
                ss.omega = -sg * std::max(SEESAW_RESTITUTION * std::fabs(ss.omega), push);
            }
        }
    }
    // resting on the ground: the child on the low side pushes again
    if (std::fabs(std::fabs(ss.phi) - pmax) < 1e-3f && std::fabs(ss.omega) < 0.05f) seesawRest += dt; else seesawRest = 0;
    if (riding && seesawRest > 0.5f)
    {
        const int side = ss.phi > 0 ? 0 : 1;
        const Kid* pusher = side == 0 ? kl : kr;
        if (pusher) ss.omega = -(side == 0 ? 1.0f : -1.0f) * SEESAW_LEG_PUSH;
        seesawRest = 0;
    }

    if (kl && kr)
    {
        ss.t += dt;
        if (ss.t >= RIDE_TIME && (seesawRest > 0.2f || std::fabs(ss.omega) < 0.05f))   // stopped (on the ground or balanced)
        {
            for (int side = 0; side < 2; side++)
            {
                Kid* k = byId(ss.seat[side]);
                if (!k) continue;
                float x, y; seesawSeatPos(side, x, y);
                k->x = x; k->y = GROUND_Y - 0.06f;
                k->rides++;
                if (k->mood > 0.3f) { funRides++; cheer(*k, 0.2f); }
                becomeFree(*k, Ride::Seesaw);
            }
            st.rides[1]++;
            say(2, "SEE-SAW: KIDS #%d AND #%d FINISH THEIR TURN", ss.seat[0], ss.seat[1]);
            ss.seat[0] = ss.seat[1] = -1; ss.mounted[0] = ss.mounted[1] = false; ss.t = 0;
        }
    }

    // a lone child gives up if no partner comes
    const bool lone = (ss.seat[0] >= 0) != (ss.seat[1] >= 0);
    seesawWait = lone && qs[1].empty() ? seesawWait + dt : 0.0f;
    if (seesawWait > 15.0f)
    {
        for (int side = 0; side < 2; side++)
            if (Kid* k = byId(ss.seat[side])) { k->y = GROUND_Y - 0.06f; becomeFree(*k, Ride::Seesaw); say(3, "KID #%d GOT BORED WAITING FOR A SEE-SAW PARTNER", k->id); }
        ss.seat[0] = ss.seat[1] = -1; ss.mounted[0] = ss.mounted[1] = false; seesawWait = 0;
    }
}

void updateBall(float dt)
{
    if (bl.held) return;                                         // in your hand
    bl.x  += bl.vx * dt;                                         // exact for constant gravity:
    bl.h  += bl.vh * dt - 0.5f * G * dt * dt;                    //   h(t+dt) = h + v dt - G dt^2 / 2
    bl.vh -= G * dt;
    bl.spin -= bl.vx * dt / BALL_R;
    if (bl.h <= 0.0f)
    {
        const float vImpact = std::sqrt(std::max(0.0f, bl.vh * bl.vh + 2.0f * G * bl.h));   // speed when it touched h = 0
        bl.h = 0.0f;
        if (vImpact > 0.25f) { bl.vh = BALL_RESTITUTION * vImpact; st.bounces++; bl.vx *= 0.92f; }
        else bl.vh = 0.0f;
    }
    if (bl.h <= 0.0f && bl.vh == 0.0f)                         // rolling friction
    {
        const float dv = BALL_ROLL_FRICTION * dt;
        bl.vx = std::fabs(bl.vx) <= dv ? 0.0f : bl.vx - (bl.vx > 0 ? dv : -dv);
    }
    // goals at both ends: the ball must cross the goal line BELOW the crossbar
    bl.goalFlash = std::max(0.0f, bl.goalFlash - dt);
    if (std::fabs(bl.x) > WALL_X && bl.h < GOAL_H)
    {
        const int side = bl.x > 0 ? 1 : -1;
        st.goals++;
        if (bl.touch == 1) { st.userGoals++; game::addAt(game::Where::Playground, SCORE_GOAL, bl.x, BALL_Y + 0.3f, "GOAL! What a shot!"); }
        else               game::addAt(game::Where::Playground, SCORE_KIDGOAL, bl.x, BALL_Y + 0.3f, "The children scored a goal");
        popup(side * (WALL_X - 0.2f), BALL_Y + 0.35f, 2, "GOAL!");
        for (Kid& k : kidsV) if (k.st == KState::Playing) cheer(k, 0.3f);
        bl.x = 0.0f; bl.h = 0.45f; bl.vx = 0.0f; bl.vh = 0.0f; bl.touch = 0;
        bl.goalFlash = 1.6f; bl.goalSide = side;
    }
    if (bl.x > WALL_X)  { bl.x = WALL_X;  bl.vx = -0.8f * std::fabs(bl.vx); }      // hit the crossbar / net
    if (bl.x < -WALL_X) { bl.x = -WALL_X; bl.vx = 0.8f * std::fabs(bl.vx); }
    bl.maxH = std::max(bl.maxH, bl.h);
    st.maxBallHeight = std::max(st.maxBallHeight, bl.h);
}

// Ball vs children: the child's body is a capsule (segment + radius); the ball a circle.
// On contact the ball bounces (impulse: v' = v - (1+e)(v.n)n) and a fast ball hurts.
void ballHitsKids()
{
    if (bl.held) return;
    const float speed = std::hypot(bl.vx, bl.vh);
    if (speed < 0.5f) return;
    for (Kid& k : kidsV)
    {
        if (k.hitCd > 0.0f) continue;
        if (k.st == KState::Riding || k.st == KState::Away || k.st == KState::Falling || k.st == KState::Dragged || k.st == KState::Hurt) continue;
        if (std::fabs(k.y - (BALL_Y + 0.025f)) > 0.07f) continue;              // not in the ball's lane
        const cg::V2 c(bl.x, k.y + BALL_R + bl.h);
        const cg::V2 a(k.x, k.y + 0.04f), b(k.x, k.y + 0.21f);
        if (!cg::circleCapsule(c, BALL_R, a, b, 0.045f)) continue;
        // contact normal from the closest point of the capsule's segment
        const cg::V2 ab = b - a;
        const float t = std::max(0.0f, std::min(1.0f, cg::dot(c - a, ab) / cg::dot(ab, ab)));
        cg::V2 n = c - (a + ab * t);
        const float nl = cg::len(n);
        n = nl > 1e-5f ? n * (1.0f / nl) : cg::V2(bl.vx > 0 ? -1.0f : 1.0f, 0.0f);
        const float vn = bl.vx * n.x + bl.vh * n.y;
        if (vn < 0.0f) { bl.vx -= 1.5f * vn * n.x; bl.vh -= 1.5f * vn * n.y; }  // restitution e = 0.5
        k.hitCd = 0.8f;
        if (k.st == KState::Playing && bl.touch != 1) continue;                // the children's own game
        if (bl.touch == 1 && speed > BALL_HURT_SPEED)
        {
            st.bonks++;
            injure(k, true, SCORE_CRASH, "The ball hit a child very hard - injured!");
        }
        else if (bl.touch == 1 && speed > BALL_BONK_SPEED)
        {
            st.bonks++;
            injure(k, false, SCORE_BONK, "BONK! The ball hit a child");
        }
        else popup(k.x, k.y + 0.30f, 3, "HEY!");
    }
}

// ---------------------------------------------------------------------------
// Children
// ---------------------------------------------------------------------------
void updateKid(Kid& k, float dt, int playIndex)
{
    k.t += dt;
    k.kickCd = std::max(0.0f, k.kickCd - dt);
    k.flash = std::max(0.0f, k.flash - dt);

    switch (k.st)
    {
    case KState::Wander:
        if (moveToward(k, k.tx, k.ty, WALK_SPEED * 0.8f, dt) && rnd() < dt * 0.5f) wanderTarget(k);
        k.decideT -= dt;
        if (k.decideT <= 0.0f && !frozen) { k.decideT = rnd(3.0f, 6.0f); decide(k); }
        break;

    case KState::ToQueue:
    case KState::Queued:
    {
        const auto& q = qs[(int)k.ride];
        const int idx = (int)(std::find(q.begin(), q.end(), k.id) - q.begin());
        if (idx >= (int)q.size()) { becomeFree(k, Ride::None); break; }    // no longer queued
        float qx, qy; queueSpot(k.ride, idx, qx, qy);
        if (moveToward(k, qx, qy, WALK_SPEED, dt)) { k.st = KState::Queued; k.facing = -1.0f; }
        break;
    }

    case KState::ToRide:
        if (!moveToward(k, k.tx, k.ty, WALK_SPEED * 1.1f, dt)) break;
        if (k.ride == Ride::Swing)
        {
            Swing& s = sw[k.seat];
            if (std::fabs(s.theta) < 0.12f && std::fabs(s.omega) < 0.4f)
            {
                s.rider = k.id; s.reserved = -1; s.theta = 0.0f; s.omega = rnd(0.35f, 0.9f);   // a push-off with the feet
                k.st = KState::Riding; k.t = 0;
            }
        }
        else if (k.ride == Ride::Seesaw)
        {
            ss.mounted[k.seat] = true; k.st = KState::Riding; k.t = 0;
        }
        else
        {
            k.st = KState::Riding; k.t = 0; k.slideStage = 0; k.x = LADDER_X; k.y = GROUND_Y;
        }
        break;

    case KState::Riding:
        if (k.ride == Ride::Swing)       { float x, y; swingSeatPos(k.seat, x, y); k.x = x; k.y = y; }
        else if (k.ride == Ride::Seesaw) { float x, y; seesawSeatPos(k.seat, x, y); k.x = x; k.y = y; k.facing = k.seat == 0 ? 1.0f : -1.0f; }
        else
        {
            k.facing = 1.0f;
            if (k.slideStage == 0)                       // climb the ladder
            {
                if (moveToward(k, SLIDE_TOP_X - 0.04f, SLIDE_TOP_Y, CLIMB_SPEED, dt)) { k.slideStage = 1; k.t = 0; }
            }
            else if (k.slideStage == 1)                  // sit at the top until the slide is clear
            {
                bool clear = true;
                for (const Kid& o : kidsV) if (o.id != k.id && o.ride == Ride::Slide && o.st == KState::Riding && o.slideStage == 2) clear = false;
                if (k.t > 0.6f && clear) { k.slideStage = 2; k.slideS = 0; k.slideV = 0; }
            }
            else if (k.slideStage == 2)                  // inclined plane with friction
            {
                const float a = slideAng();
                const float mu = waxT > 0.0f ? WAX_FRICTION : SLIDE_FRICTION;
                k.slideV += G * (std::sin(a) - mu * std::cos(a)) * dt;
                k.slideS += k.slideV * dt;
                slidePoint(std::min(k.slideS, slideLen()), k.x, k.y);
                if (k.slideS >= slideLen())
                {
                    st.maxSlideSpeed = std::max(st.maxSlideSpeed, k.slideV);
                    if (k.slideV > SLIDE_SAFE_SPEED)                // too fast: shoots off the end of the slide
                    {
                        popup(SLIDE_BOT_X, SLIDE_BOT_Y + 0.30f, 1, "TOO FAST!");
                        startFall(k, SLIDE_LIP_X, SLIDE_BOT_Y + 0.02f, k.slideV * 0.95f, 0.35f, GROUND_Y - 0.05f, "flew off the slide");
                        break;
                    }
                    k.slideStage = 3;
                    popup(SLIDE_BOT_X, SLIDE_BOT_Y + 0.30f, 2, "WHEE!");
                }
            }
            else                                         // run-out: lip, then sliding friction on the ground
            {
                if (k.x < SLIDE_LIP_X) k.slideV -= (waxT > 0.0f ? WAX_FRICTION : SLIDE_FRICTION) * G * dt;
                else { k.slideV -= GROUND_FRICTION * G * dt; k.y = std::max(GROUND_Y - 0.03f, k.y - dt * 1.2f); }
                k.x += std::max(0.0f, k.slideV) * dt;
                if (k.slideV <= 0.0f)
                {
                    k.y = GROUND_Y - 0.04f; k.rides++; st.rides[2]++; funRides++; cheer(k, 0.2f);
                    say(0, "KID #%d SLID DOWN (MASS %.0f KG)", k.id, k.mass);
                    becomeFree(k, Ride::Slide);
                }
            }
        }
        break;

    case KState::Playing:
    {
        const float off = (playIndex - 1) * 0.09f;
        const float side = bl.x > k.x ? 1.0f : -1.0f;
        const float tx = std::max(-WALL_X, std::min(WALL_X, bl.x - side * 0.05f + (std::fabs(bl.x - k.x) > 0.25f ? off : 0.0f)));
        moveToward(k, tx, BALL_Y + 0.025f, RUN_SPEED, dt);
        k.facing = side;
        if (std::fabs(k.x - bl.x) < 0.075f && bl.h < 0.07f && k.kickCd <= 0.0f && !frozen)
        {
            float dir = bl.x > 0.4f ? -1.0f : bl.x < -0.4f ? 1.0f : (rnd() < 0.5f ? -1.0f : 1.0f);
            bl.vx = dir * rnd(0.6f, 1.25f);
            bl.vh = rnd(0.5f, 1.1f);
            k.kickCd = 1.4f; k.flash = 0.25f; k.facing = dir;
            st.kicks++;
        }
        if (k.t > BALL_TIME) becomeFree(k, Ride::Ball);
        break;
    }

    case KState::Dragged:
        break;

    case KState::Falling:                                   // projectile until it reaches the ground
    {
        k.vy -= G * dt;
        k.x += k.vx * dt;
        k.y += k.vy * dt;
        k.spin += dt * 540.0f * (k.vx >= 0 ? -1.0f : 1.0f);
        k.x = clampf(k.x, -WALL_X, WALL_X);
        // flying into another child (circle vs capsule): both tumble
        for (Kid& o : kidsV)
        {
            if (o.id == k.id || o.hitCd > 0.0f) continue;
            if (o.st != KState::Wander && o.st != KState::Queued && o.st != KState::ToQueue && o.st != KState::Playing) continue;
            if (cg::circleCapsule(cg::V2(k.x, k.y + 0.1f), 0.06f, cg::V2(o.x, o.y + 0.03f), cg::V2(o.x, o.y + 0.22f), 0.04f))
            {
                o.hitCd = 1.0f;
                injure(o, false, SCORE_BONK, "Crash! A flying child knocked another one over");
            }
        }
        if (k.vy < 0.0f && k.y <= k.landY)
        {
            k.y = k.landY; k.spin = 0;
            if (matCovers(k.x, k.y))
            {
                st.saves++;
                const bool slide = std::strncmp(k.why, "flew off the slide", 6) == 0;
                if (slide) { st.superSlides++; game::addAt(game::Where::Playground, SCORE_SUPER, k.x, k.y + 0.3f, "SUPER SLIDE! Landed safely on the mat"); }
                else game::addAt(game::Where::Playground, SCORE_SAVED, k.x, k.y + 0.3f, "Phew! The safety mat caught kid #%d", k.id);
                popup(k.x, k.y + 0.34f, 0, slide ? "SUPER SLIDE!" : "SAFE LANDING!");
                k.mood = slide ? 1.0f : 0.35f;
                becomeFree(k, Ride::None);
                k.ty = k.y = std::max(LANE_Y0, std::min(LANE_Y1, k.y));
            }
            else
            {
                const bool slide = std::strncmp(k.why, "flew off the slide", 6) == 0;
                injure(k, true, slide ? SCORE_CRASH : SCORE_FALL,
                       slide ? "A child flew off the waxed slide - injured!" : "A child fell off the swing - injured!");
            }
        }
        break;
    }

    case KState::Hurt:                                      // crying on the ground
        if (k.t > HOSPITAL_DELAY)
        {
            if (k.serious)
            {
                game::Injury inj;
                inj.kidId = k.id;
                inj.cond = std::strncmp(k.why, "flew off the slide", 6) == 0 ? 5 : (k.id % 2 ? 4 : 3);   // hospital::Cond
                inj.severity = 3;
                std::snprintf(inj.what, sizeof(inj.what), "Kid #%d (playground)", k.id);
                game::sendToHospital(inj);
                k.st = KState::ToGate; k.t = 0;
                popup(k.x, k.y + 0.30f, 2, "OFF TO ENCHANTED HEALS");
            }
            else { becomeFree(k, Ride::None); k.mood = 0.25f; k.y = std::max(LANE_Y0, std::min(LANE_Y1, k.y)); }
        }
        break;

    case KState::ToGate:                                    // limps to the gate, then is at the hospital
        if (moveToward(k, GATE_X, GATE_Y, WALK_SPEED * 0.55f, dt)) { k.st = KState::Away; k.t = 0; }
        break;

    case KState::Away:
        k.x = GATE_X + 0.3f;                                // off screen
        break;

    case KState::Returning:
        if (moveToward(k, k.tx, k.ty, WALK_SPEED, dt)) becomeFree(k, Ride::None);
        break;
    }
    if (k.st != KState::Away && k.st != KState::Hurt && k.st != KState::ToGate)       // mood relaxes slowly
        k.mood += (0.6f - k.mood) * (1.0f - std::exp(-dt * 0.02f));
    k.bandage = std::max(0.0f, k.bandage - dt);
    k.hitCd = std::max(0.0f, k.hitCd - dt);
}

Kid* pickKid(float x, float y)
{
    Kid* best = nullptr;
    for (Kid& k : kidsV)
    {
        if (k.st == KState::Riding || k.st == KState::Falling || k.st == KState::Hurt || k.st == KState::ToGate ||
            k.st == KState::Away || k.st == KState::Returning) continue;
        if (std::fabs(x - k.x) < 0.055f && y > k.y - 0.02f && y < k.y + 0.27f)
            if (!best || k.y < best->y) best = &k;                // front-most child
    }
    return best;
}

void handleInput(const Input& in)
{
    if (dragId >= 0)
    {
        Kid* k = byId(dragId);
        if (!k) { dragId = -1; return; }
        if (in.valid) { k->x = std::max(-1.70f, std::min(1.70f, in.x + dragDX)); k->y = std::max(-0.80f, std::min(0.40f, in.y + dragDY)); }
        if (in.released || !in.down)
        {
            dragId = -1;
            const Ride r = rideZoneAt(in.x, in.y);
            st.drags++;
            if (r == Ride::None)
            {
                k->y = std::max(LANE_Y0, std::min(LANE_Y1, k->y));
                becomeFree(*k, Ride::None);
                k->tx = k->x; k->ty = k->y; k->decideT = 6.0f;
                popup(k->x, k->y + 0.30f, 3, "FREE PLAY");
            }
            else
            {
                if (r != Ride::Ball) k->y = std::max(LANE_Y0, std::min(LANE_Y1, k->y));
                joinRide(*k, r, true);
                say(0, "YOU SENT KID #%d TO THE %s", k->id, rideName(r));
            }
        }
        return;
    }
    if (pendId >= 0)                                        // drag threshold: a plain click does not move anyone
    {
        Kid* k = byId(pendId);
        if (!k || !in.down || in.released || k->st == KState::Riding) { pendId = -1; return; }
        if (std::hypot(in.x - pendX, in.y - pendY) < 0.025f) return;
        pendId = -1;
        removeFromQueues(k->id);
        releaseReservation(*k);
        k->st = KState::Dragged; k->ride = Ride::None; k->seat = -1;
        dragId = k->id; dragDX = k->x - pendX; dragDY = k->y - pendY;
        return;
    }
    // ---- the safety mat follows the cursor while dragged ----
    if (matDrag)
    {
        if (in.valid) { mat_.x = clampf(in.x + matDX, -1.62f, 1.62f); mat_.y = clampf(in.y + matDY, -0.64f, -0.32f); }
        if (in.released || !in.down) { matDrag = 0; popup(mat_.x, mat_.y + 0.12f, 0, "MAT PLACED"); }
        return;
    }
    // ---- ball: drag = hold and throw (release velocity from the cursor motion), click = kick ----
    if (ballPend || bl.held)
    {
        if (in.valid) ballTrail.push_back(Sample{in.x, in.y, simT});
        while (ballTrail.size() > 2 && simT - ballTrail.front().t > 0.10f) ballTrail.pop_front();
        if (!bl.held && in.down && std::hypot(in.x - ballPX, in.y - ballPY) > 0.025f) { bl.held = true; bl.vx = bl.vh = 0; }
        if (bl.held && in.valid)
        {
            bl.x = clampf(in.x, -WALL_X + 0.05f, WALL_X - 0.05f);
            bl.h = clampf(in.y - (BALL_Y + BALL_R), 0.0f, 0.75f);
        }
        if (in.released || !in.down)
        {
            if (bl.held)
            {
                float vx = 0, vh = 0;
                predictThrow(vx, vh);
                bl.vx = vx; bl.vh = vh; bl.held = false; bl.touch = 1;
                st.userKicks++;
                popup(bl.x, BALL_Y + bl.h + 0.25f, 2, std::hypot(vx, vh) > BALL_HURT_SPEED ? "HUGE THROW!" : "THROW!");
            }
            else
            {
                const float dir = bl.x >= in.x ? 1.0f : -1.0f;
                bl.vx = dir * BALL_KICK; bl.vh = 1.05f; bl.touch = 1;
                st.userKicks++;
                popup(bl.x, BALL_Y + 0.25f, 2, "KICK!");
            }
            ballPend = 0; ballTrail.clear();
        }
        return;
    }
    if (!in.valid || !in.pressed) return;

    if (hoverBall()) { ballPend = 1; ballPX = in.x; ballPY = in.y; ballTrail.clear(); ballTrail.push_back(Sample{in.x, in.y, simT}); return; }
    if (hoverMat() && !pickKid(in.x, in.y)) { matDrag = 1; matDX = mat_.x - in.x; matDY = mat_.y - in.y; return; }
    if (Kid* k = pickKid(in.x, in.y)) { pendId = k->id; pendX = in.x; pendY = in.y; return; }
    const int s = hoverSwing();
    if (s >= 0) { testPushSwing(s); return; }
    if (hoverSlide())
    {
        const bool fresh = waxT <= 0.0f;
        waxT = WAX_TIME;
        popup(0.66f, 0.10f, 2, fresh ? "SLIDE WAXED - SUPER SLIPPERY!" : "MORE WAX!");
        game::add(game::Where::Playground, 0, "You waxed the slide: children will go MUCH faster");
        return;
    }
    int side = -1;
    if (hoverSeesaw(side))
    {
        if (side == 2)
        {
            ss.balance = !ss.balance; computeArms();
            popup(SEESAW_X, SEESAW_Y + 0.25f, 2, "LEVER BALANCE %s", ss.balance ? "ON" : "OFF");
            say(2, "SEE-SAW LEVER BALANCE %s (HEAVIER CHILD %s)", ss.balance ? "ON" : "OFF", ss.balance ? "SITS CLOSER" : "SITS AT THE END");
        }
        else
        {
            ss.omega += (side == 0 ? 1.0f : -1.0f) * SEESAW_USER_PUSH;
            st.seesawPushes++;
            popup(SEESAW_X + (side == 0 ? -0.3f : 0.3f), SEESAW_Y + 0.22f, 3, "PUSH %s DOWN", side == 0 ? "LEFT" : "RIGHT");
        }
    }
}

void placeOnRideNow(Kid& k, Ride r)    // initial scene: start with some children already playing
{
    joinRide(k, r, false);
    if (r == Ride::Ball) return;
    dispatch();
    if (k.st != KState::ToRide) return;
    k.x = k.tx; k.y = k.ty;
}

}   // namespace

// ---------------------------------------------------------------------------
// public
// ---------------------------------------------------------------------------
void init(unsigned seed) { seed0 = seed; reset(); }

void reset()
{
    rng.seed(seed0);
    kidsV.clear(); pops.clear(); logV.clear();
    for (auto& q : qs) q.clear();
    for (Swing& s : sw) s = Swing();
    ss = Seesaw(); bl = Ball(); st = Stats();
    dragId = -1; pendId = -1; frozen = false; seesawRest = 0; seesawWait = 0;
    mat_ = Mat(); waxT = 0; matDrag = 0; ballPend = 0; ballTrail.clear(); simT = 0; funRides = funPushes = 0; funT = 0;

    for (int i = 0; i < KIDS; i++)
    {
        Kid k;
        k.id = i + 1;
        k.mass = std::round(rnd(KID_MASS_MIN, KID_MASS_MAX));
        k.skin = (int)(rnd() * 4) % 4; k.hair = (int)(rnd() * 4) % 4; k.shirt = (int)(rnd() * 6) % 6; k.pants = (int)(rnd() * 3) % 3;
        k.x = rnd(-1.5f, 1.5f); k.y = rnd(LANE_Y0, LANE_Y1);
        k.phase = rnd(0.0f, 6.0f);
        k.decideT = rnd(0.5f, 5.0f);
        wanderTarget(k);
        kidsV.push_back(k);
    }
    // a lively first frame: swing, see-saw pair, slide, ball
    placeOnRideNow(kidsV[0], Ride::Swing);
    placeOnRideNow(kidsV[1], Ride::Swing);
    placeOnRideNow(kidsV[2], Ride::Seesaw);
    placeOnRideNow(kidsV[3], Ride::Seesaw);
    placeOnRideNow(kidsV[4], Ride::Slide);
    placeOnRideNow(kidsV[5], Ride::Ball);
    kidsV[5].x = bl.x - 0.3f; kidsV[5].y = BALL_Y + 0.025f;
    say(3, "%s IS OPEN - CLICK SWINGS, SEE-SAW OR BALL, DRAG CHILDREN", NAME);
}

void update(double dtIn, const Input& in)
{
    const float dt = (float)std::min(dtIn, 0.05);
    lastIn = in;
    handleInput(in);
    dispatch();
    updateSwings(dt);
    updateSeesaw(dt);
    updateBall(dt);
    ballHitsKids();
    simT += dt;
    waxT = std::max(0.0f, waxT - dt);
    // children healed in the hospital walk back in through the gate
    {
        int id = 0;
        while (game::takeHealed(id))
            if (Kid* k = byId(id))
            {
                k->st = KState::Returning; k->x = GATE_X; k->y = GATE_Y; k->t = 0;
                k->tx = rnd(0.2f, 1.3f); k->ty = rnd(LANE_Y0, LANE_Y1);
                k->mood = 1.0f; k->bandage = 25.0f; k->serious = false; k->why[0] = 0;
                st.returned++;
                game::addAt(game::Where::Playground, SCORE_RETURN, GATE_X - 0.2f, GATE_Y + 0.3f, "Kid #%d is back from Enchanted Heals, all better!", id);
                popup(GATE_X - 0.2f, GATE_Y + 0.32f, 0, "BACK FROM HOSPITAL!");
            }
    }
    // happy rides are reported in small batches (keeps the event feed readable)
    funT += dt;
    if (funT > 5.0f)
    {
        funT = 0.0f;
        const int n = funRides + funPushes;
        if (n > 0) game::add(game::Where::Playground, SCORE_RIDE * funRides + funPushes,
                             funRides ? "Children enjoyed %d happy rides" : "Great pushing - the children loved it", funRides ? funRides : funPushes);
        funRides = funPushes = 0;
    }
    int playIndex = 0;
    for (Kid& k : kidsV) updateKid(k, dt, k.st == KState::Playing ? playIndex++ : 0);
    for (Popup& p : pops) p.age += dt;
    pops.erase(std::remove_if(pops.begin(), pops.end(), [](const Popup& p) { return p.age > 2.6f; }), pops.end());
}

const std::vector<Kid>&     kids()      { return kidsV; }
const Swing&                swing(int i) { return sw[std::max(0, std::min(SWINGS - 1, i))]; }
const Seesaw&               seesaw()    { return ss; }
const Ball&                 ball()      { return bl; }
const std::deque<int>&      queue(Ride r) { const int i = (int)r; return i >= 0 && i < 3 ? qs[i] : EMPTY_Q; }
const std::vector<Popup>&   popups()    { return pops; }
const std::vector<LogLine>& log()       { return logV; }
const Stats&                stats()     { return st; }
const Kid* findKid(int id) { return byId(id); }
bool  dragging()       { return dragId >= 0; }
int   ballPlayers()    { return playersOnBall(); }
float swingAmplitude(int i) { return ampOf(swing(i)); }
float swingPeriod()    { return 6.2831853f * std::sqrt(SWING_LENGTH / G); }
float expectedWait(Ride r) { return waitOf(r); }
float slideLength()    { return slideLen(); }
float slideAngle()     { return slideAng(); }

int hoverKidId()
{
    if (!lastIn.valid || dragId >= 0) return -1;
    Kid* k = pickKid(lastIn.x, lastIn.y);
    return k ? k->id : -1;
}

int hoverSwing()
{
    if (!lastIn.valid) return -1;
    for (int i = 0; i < SWINGS; i++)
    {
        float sx, sy; swingSeatPos(i, sx, sy);
        const float px = SWING_X[i], py = SWING_PIVOT_Y;
        const float vx = sx - px, vy = sy - py, wx = lastIn.x - px, wy = lastIn.y - py;
        const float t = std::max(0.0f, std::min(1.0f, (vx * wx + vy * wy) / (vx * vx + vy * vy)));
        const float dx = wx - vx * t, dy = wy - vy * t;
        if (dx * dx + dy * dy < 0.05f * 0.05f || std::hypot(lastIn.x - sx, lastIn.y - (sy + 0.08f)) < 0.09f) return i;
    }
    return -1;
}

bool hoverSeesaw(int& side)
{
    if (!lastIn.valid) return false;
    const float mx = lastIn.x, my = lastIn.y;
    if (std::fabs(mx - SEESAW_X) < 0.05f && my > GROUND_Y - 0.02f && my < SEESAW_Y + 0.04f) { side = 2; return true; }
    const float c = std::cos(ss.phi), s = std::sin(ss.phi);
    const float lx = mx - SEESAW_X, ly = my - SEESAW_Y;
    const float along = -lx * c - ly * s;                 // distance along the beam towards the LEFT end
    const float perp  = lx * s - ly * c;
    if (std::fabs(along) > SEESAW_HALF + 0.02f || std::fabs(perp) > 0.07f) return false;
    side = along > 0.0f ? 0 : 1;
    return true;
}

bool hoverBall()
{
    if (!lastIn.valid) return false;
    return std::hypot(lastIn.x - bl.x, lastIn.y - (BALL_Y + BALL_R + bl.h)) < 0.075f;
}

Ride rideZoneAt(float x, float y)
{
    if (y < -0.68f && y > -0.86f) return Ride::Ball;
    if (y < -0.68f || y > 0.46f) return Ride::None;
    if (x >= SWING_FRAME_X0 - 0.04f && x <= SWING_FRAME_X1 + 0.03f) return Ride::Swing;
    if (x >= -0.70f && x <= 0.10f && y < 0.05f) return Ride::Seesaw;
    if (x >= 0.16f && x <= 1.05f && y < 0.32f) return Ride::Slide;
    return Ride::None;
}

void swingSeatPos(int i, float& x, float& y)
{
    const Swing& s = swing(i);
    x = SWING_X[std::max(0, std::min(SWINGS - 1, i))] + SWING_LENGTH * std::sin(s.theta);
    y = SWING_PIVOT_Y - SWING_LENGTH * std::cos(s.theta);
}

void seesawSeatPos(int side, float& x, float& y)
{
    const float sg = side == 0 ? -1.0f : 1.0f, d = ss.arm[side == 0 ? 0 : 1];
    x = SEESAW_X + sg * d * std::cos(ss.phi);
    y = SEESAW_Y + sg * d * std::sin(ss.phi) + 0.02f;
}

void slidePoint(float s, float& x, float& y)
{
    const float t = s / slideLen();
    x = SLIDE_TOP_X + (SLIDE_BOT_X - SLIDE_TOP_X) * t;
    y = SLIDE_TOP_Y + (SLIDE_BOT_Y - SLIDE_TOP_Y) * t;
}

const char* rideName(Ride r)
{
    switch (r)
    {
    case Ride::Swing:  return "SWINGS";
    case Ride::Seesaw: return "SEE-SAW";
    case Ride::Slide:  return "SLIDE";
    case Ride::Ball:   return "BALL GAME";
    default:           return "FREE PLAY";
    }
}

const char* stateName(const Kid& k)
{
    switch (k.st)
    {
    case KState::Wander:  return "WANDERING";
    case KState::ToQueue: return "WALKING TO QUEUE";
    case KState::Queued:  return "WAITING IN QUEUE";
    case KState::ToRide:  return "GOING TO SEAT";
    case KState::Riding:
        if (k.ride == Ride::Slide) return k.slideStage == 0 ? "CLIMBING" : k.slideStage == 1 ? "AT THE TOP" : k.slideStage == 2 ? "SLIDING" : "LANDING";
        return "RIDING";
    case KState::Playing: return "PLAYING BALL";
    case KState::Falling: return "FALLING!";
    case KState::Hurt:    return k.serious ? "HURT - NEEDS A DOCTOR" : "CRYING";
    case KState::ToGate:  return "GOING TO THE HOSPITAL";
    case KState::Away:    return "AT THE HOSPITAL";
    case KState::Returning: return "BACK FROM HOSPITAL";
    default:              return "HELD BY YOU";
    }
}

void testPushSwing(int i)
{
    Swing& s = sw[std::max(0, std::min(SWINGS - 1, i))];
    s.omega += std::fabs(s.omega) > 0.05f ? (s.omega > 0 ? SWING_PUSH : -SWING_PUSH) : SWING_PUSH;
    s.pushT = 0.4f;
    st.pushes++;
    if (s.rider >= 0)
    {
        const float a = ampOf(s);
        Kid* r = byId(s.rider);
        if (a > 0.40f && a < SWING_DANGER_AMP && s.rewardCd <= 0.0f && r) { funPushes++; s.rewardCd = 1.2f; cheer(*r, 0.08f); }
    }
    float x, y; swingSeatPos(i, x, y);
    popup(x, y + 0.32f, 2, "PUSH! AMPLITUDE %.0f DEG", ampOf(s) * 57.2958f);
}
void testKickBall(float vx, float vh) { bl.vx = vx; bl.vh = vh; st.userKicks++; }
void testSetSeesawBalance(bool on) { ss.balance = on; computeArms(); }
void testSendKid(int id, Ride r) { if (Kid* k = byId(id)) { removeFromQueues(id); releaseReservation(*k); joinRide(*k, r, true); } }
void testFreeze(bool on) { frozen = on; }
void testWaxSlide() { waxT = WAX_TIME; }
void testSetMat(float x, float y) { mat_.x = x; mat_.y = y; }
void testThrowBall(float x, float h, float vx, float vh) { bl.x = x; bl.h = h; bl.vx = vx; bl.vh = vh; bl.touch = 1; bl.held = false; }

const Mat& mat() { return mat_; }
float slideWax() { return waxT; }
bool  matCovers(float x, float y) { return std::fabs(x - mat_.x) <= MAT_HW + 0.02f && std::fabs(y - mat_.y) <= 0.12f; }
bool  hoverMat()
{
    if (!lastIn.valid) return false;
    return std::fabs(lastIn.x - mat_.x) <= MAT_HW + 0.02f && lastIn.y > mat_.y - MAT_HH - 0.03f && lastIn.y < mat_.y + MAT_HH + 0.03f;
}
bool  hoverSlide()
{
    if (!lastIn.valid) return false;
    const cg::V2 m(lastIn.x, lastIn.y);
    return cg::distPointSegment(m, cg::V2(SLIDE_TOP_X, SLIDE_TOP_Y), cg::V2(SLIDE_BOT_X, SLIDE_BOT_Y)) < 0.045f ||
           cg::distPointSegment(m, cg::V2(SLIDE_BOT_X, SLIDE_BOT_Y), cg::V2(SLIDE_LIP_X, SLIDE_BOT_Y)) < 0.035f;
}
int happyCount()
{
    int n = 0;
    for (const Kid& k : kidsV) if (k.st != KState::Away && k.st != KState::Hurt && k.st != KState::ToGate && k.mood > 0.55f) n++;
    return n;
}
int injuredCount()
{
    int n = 0;
    for (const Kid& k : kidsV) if ((k.st == KState::Hurt && k.serious) || k.st == KState::ToGate || k.st == KState::Away) n++;
    return n;
}
// release velocity of a throw = average cursor velocity over the last 0.1 s (finite difference)
bool predictThrow(float& vx, float& vh)
{
    if (ballTrail.size() < 2) { vx = vh = 0.0f; return false; }
    const Sample& a = ballTrail.front();
    const Sample& b = ballTrail.back();
    const float dt = std::max(0.016f, b.t - a.t);
    vx = (b.x - a.x) / dt; vh = (b.y - a.y) / dt;
    const float sp = std::hypot(vx, vh);
    if (sp > BALL_MAX_THROW) { vx *= BALL_MAX_THROW / sp; vh *= BALL_MAX_THROW / sp; }
    return true;
}
void testClearRides()
{
    for (auto& q : qs) q.clear();
    for (Swing& s : sw) s = Swing();
    ss = Seesaw(); bl = Ball();
    for (Kid& k : kidsV) { becomeFree(k, Ride::None); k.lastRide = Ride::None; k.x = rnd(-1.5f, 1.5f); k.y = rnd(LANE_Y0, LANE_Y1); }
}

}   // namespace playground
