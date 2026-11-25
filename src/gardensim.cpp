#include "gardensim.h"

#include "pathfind.h"
#include "game.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <queue>

using namespace gfx;
using pf::Vec2;

namespace gardensim
{
namespace
{

// ----------------------------------------------------------------------------
// Tunable numbers (all simulation rules live here)
// ----------------------------------------------------------------------------
constexpr float NEED_WATER   = 0.45f;     // below this a flower "needs water"
constexpr float CRIT_WATER   = 0.18f;     // below this the flower starts to suffer
constexpr float POUR_AMOUNT  = 0.50f;     // water added by one pour
constexpr float POUR_TIME    = 1.00f;     // seconds to water one flower
constexpr float FILL_TIME    = 1.80f;     // seconds to fill the can at the pond
constexpr float WALK_SPEED   = 0.55f;     // world units / second
constexpr float MIN_INSIDE   = 2.0f;      // gardener rests at least this long indoors
constexpr int   WAKE_COUNT   = 3;         // leave the house when this many flowers are thirsty ...
constexpr float WAKE_DEADLINE = 15.0f;    // ... or when any flower will be critical within this many seconds
constexpr float REPLANT_AFTER = 25.0f;    // dead flower is replaced by a new seed after this long
constexpr float GARDENER_SCALE = 0.46f;

const Vec2 DOOR{DOOR_X, DOOR_Y};                 // cottage door (cottage on the RIGHT)

struct Row
{
    char name[8];
    float y, xMin, xMax, standDy;     // standDy: where the gardener stands relative to the row
    std::vector<int> idx;           // flowers of this row, sorted by x
    int nodeL, nodeR;               // graph nodes at the two ends
};

struct Rng
{
    unsigned s = 1;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFFu) / 16777216.0f; }
};

std::vector<Flower>     flowers_;
std::vector<Row>        rows_;
pf::Graph               graph_;
int                     nodeDoor_ = 0, nodePond_ = 0;
Gardener                g_;
std::array<Butterfly, 4> bf_;
std::vector<Ripple>     ripples_;
std::vector<Vec2>       meadow_;
float  windAngle_ = 0.0f, windBoost_ = 0.0f, windX_ = 1.18f, windY_ = 0.2f;
bool   hudOn_ = true, started_ = false;
double simTime_ = 0.0;
int    bloomed_ = 0, withered_ = 0;   // batched gameplay events
float  reportT_ = 0.0f;

// gardener working memory
std::vector<Vec2> route_;
size_t            routeIdx_ = 0;
std::vector<int>  rowQueue_;
size_t            rowPos_ = 0;
int               curRow_ = -1, targetFlower_ = -1;
float             stateTime_ = 0.0f, wakeTimer_ = 0.0f, stuck_ = 0.0f;
char              schedInfo_[64] = "SCHEDULER IDLE";
Vec2              grabOff_{0, 0};
float             gate_ = 0.0f;           // gate opening 0..1
bool              sentHome_ = false;      // the user sent him in: do not turn back on the way
Drop              lastDrop_ = Drop::Ground;
bool              pendPick_ = false;      // drag threshold
float             pendX_ = 0, pendY_ = 0;
std::vector<cg::V2> shore_, lakePoly_;

const char* const STATE_NAMES[] = {"RESTING IN HOUSE", "LEAVING HOUSE", "WALKING TO POND", "FILLING CAN", "WALKING TO ROW",
                                   "WALKING TO FLOWER", "WATERING", "GOING HOME", "ENTERING HOUSE", "PICKED UP", "WALKING TO SPOT"};

// ----------------------------------------------------------------------------
// Flowers
// ----------------------------------------------------------------------------
float thirstMul()      // the sun dries the soil: hotter by day, slower at night
{
    switch (theme::mode())
    {
    case theme::MODE_DAY:       return 1.3f;
    case theme::MODE_AFTERNOON: return 1.6f;
    case theme::MODE_EVENING:   return 1.0f;
    default:                    return 0.6f;
    }
}

void seedFlower(Flower& f, int index, int generation)
{
    const float k = index * 13.7f + generation * 5.3f;
    f.generation = generation;
    f.thirst   = lerpf(0.0006f, 0.0022f, hash1(k + 1.0f));   // more flowers now: a little less thirsty each
    if (hash1(k + 6.0f) > 0.82f) f.thirst *= 2.0f;          // a few flowers are real drinkers
    f.growRate = lerpf(0.022f, 0.050f, hash1(k + 2.0f));
    f.frail    = hash1(k + 3.0f);
    f.growth   = generation == 0 ? lerpf(0.35f, 0.95f, hash1(k + 8.0f)) : 0.0f;   // first garden: already blooming
    f.water    = lerpf(0.16f, 0.50f, hash1(k + 4.0f));   // soil is already dry
    f.health   = 0.85f;
    f.dead     = false;
    f.deadTime = 0.0f;
    f.bloomed  = false;
}

bool  isNeedy(const Flower& f)  { return !f.dead && f.water < NEED_WATER; }
float deadlineOf(const Flower& f)     // seconds until the flower becomes critical
{
    if (f.water <= CRIT_WATER) return 0.0f;
    return (f.water - CRIT_WATER) / std::max(1e-4f, f.thirst * thirstMul());
}
int needyCount()
{
    int n = 0;
    for (const Flower& f : flowers_) if (isNeedy(f)) n++;
    return n;
}
bool anyUrgent()
{
    for (const Flower& f : flowers_) if (isNeedy(f) && deadlineOf(f) < WAKE_DEADLINE) return true;
    return false;
}
bool shouldWake() { return needyCount() >= WAKE_COUNT || anyUrgent(); }

void updateFlowers(float dt)
{
    const float mul = thirstMul();
    for (size_t i = 0; i < flowers_.size(); i++)
    {
        Flower& f = flowers_[i];
        if (f.dead)
        {
            f.deadTime += dt;
            if (f.deadTime > REPLANT_AFTER) seedFlower(f, (int)i, f.generation + 1);    // a new seed is planted
            continue;
        }
        f.water = std::max(0.0f, f.water - f.thirst * mul * dt);
        const float moist = 0.30f + 0.25f * f.frail;                  // fragile flowers need moister soil
        if (f.water > moist)                                          // good conditions: grow and heal
        {
            f.growth = std::min(1.0f, f.growth + f.growRate * (0.4f + 0.6f * f.health) * dt);
            f.health = std::min(1.0f, f.health + (0.012f + 0.020f * (1.0f - f.frail)) * dt);
        }
        else if (f.water < 0.20f)                                     // too dry: suffer
        {
            f.health -= (0.004f + 0.012f * f.frail) * (1.0f + (0.20f - f.water) * 5.0f) * dt;
            if (f.growth > 0.05f) f.growth = std::max(0.05f, f.growth - 0.004f * dt);
        }
        else if (f.frail > 0.70f) f.health -= 0.0025f * dt;           // fragile ones slowly deteriorate
        if (f.health <= 0.0f) { f.health = 0.0f; f.dead = true; f.deadTime = 0.0f; withered_++; }
        if (f.growth >= 1.0f && !f.bloomed) { f.bloomed = true; bloomed_++; }
    }
}

// ----------------------------------------------------------------------------
// Static layout: flower rows + walking graph
// ----------------------------------------------------------------------------
int  rowDir_ = 1;      // +1: walking the row left->right, -1: right->left
// The gardener stands beside the flower (spout is ~0.17 ahead of him) so the water lands on it.
Vec2 standFor(const Flower& f) { return Vec2{f.x - 0.17f * rowDir_, f.y + rows_[f.row].standDy}; }

void buildLayout()
{
    static const Color petalCols[] = {rgb(0.95f, 0.35f, 0.50f), rgb(1.00f, 0.82f, 0.20f), rgb(0.92f, 0.20f, 0.20f),
                                      rgb(0.70f, 0.40f, 0.90f), rgb(0.98f, 0.98f, 0.95f), rgb(1.00f, 0.55f, 0.20f),
                                      rgb(0.35f, 0.55f, 0.95f)};
    static const int cyc[] = {8, 10, 12, 15};

    struct Bed { const char* tag; float xa, xb, y0, dy; int rows, cols; float seed; float standDy; };
    // pond/lake on the left, cottage on the right; beds: A big bed right, B between the cove and
    // the path, C along the fence (centre), D on the FAR BANK (over the bridge), E/F at the cottage
    const Bed beds[] = {{"A", 0.80f, 1.66f, -0.55f, -0.10f, 4, 7, 1.0f, -0.045f},
                        {"B", -0.12f, 0.12f, -0.615f, -0.075f, 2, 5, 7.0f, -0.040f},
                        {"C", -0.30f, 0.48f, -0.285f, 0.0f, 1, 6, 13.0f, -0.030f},
                        {"D", -1.74f, -0.92f, -0.290f, 0.0f, 1, 8, 19.0f, -0.022f},
                        {"E", 0.58f, 0.98f, -0.300f, 0.0f, 1, 4, 23.0f, -0.032f},
                        {"F", 1.28f, 1.70f, -0.300f, 0.0f, 1, 3, 29.0f, -0.032f}};
    constexpr int NB = 6;

    flowers_.clear();
    rows_.clear();
    graph_ = pf::Graph();

    // --- walking graph: door -> gate -> stepping-stone path -> branch path -> bridge -> far bank ---
    nodeDoor_ = graph_.add(DOOR.x, DOOR.y);
    const int gate = graph_.add(0.5f * (GATE_X0 + GATE_X1), FENCE_Y - 0.03f);
    const int p0 = graph_.add(1.00f, -0.33f), p1 = graph_.add(0.60f, -0.46f);
    const int p2 = graph_.add(0.20f, -0.66f), p3 = graph_.add(0.20f, -0.88f);
    const int s1 = graph_.add(-0.02f, -0.805f), s2 = graph_.add(-0.30f, -0.815f);
    nodePond_    = graph_.add(FILL_X, FILL_Y);
    graph_.link(nodeDoor_, gate); graph_.link(gate, p0); graph_.link(p0, p1); graph_.link(p1, p2); graph_.link(p2, p3);
    graph_.link(p3, s1); graph_.link(s1, s2); graph_.link(p2, nodePond_); graph_.link(s2, nodePond_);
    int prev = graph_.add(BRIDGE_X0 + 0.05f, BRIDGE_Y0 - 0.005f);                         // bridge foot
    graph_.link(s2, prev);
    for (int k = 0; k <= 6; k++)                                                          // along the deck
    {
        const cg::V2 q = bridgeAt(k / 6.0f);
        const int n = graph_.add(q.x, q.y);
        graph_.link(prev, n);
        prev = n;
    }
    const int farFoot = graph_.add(BRIDGE_X1 + 0.02f, -0.315f);
    graph_.link(prev, farFoot);

    int bedFirstRow[NB] = {0};
    int index = 0;
    for (int b = 0; b < NB; b++)
    {
        const Bed& bd = beds[b];
        bedFirstRow[b] = (int)rows_.size();
        for (int r = 0; r < bd.rows; r++)
        {
            Row row;
            std::snprintf(row.name, sizeof(row.name), "%s%d", bd.tag, (r + 1) % 10);
            row.y = bd.y0 + bd.dy * r;
            row.standDy = bd.standDy;
            row.xMin = 1e9f; row.xMax = -1e9f;
            for (int c = 0; c < bd.cols; c++, index++)
            {
                Flower f{};
                const float k = index * 1.0f + bd.seed;
                f.x = lerpf(bd.xa, bd.xb, (c + 0.5f) / bd.cols) + (hash1(k * 12.9f) - 0.5f) * 0.04f;
                f.y = row.y + (hash1(k * 78.2f) - 0.5f) * 0.012f;
                const float depth = -f.y;
                f.h      = (0.10f + 0.10f * hash1(k * 4.1f)) * (0.7f + 0.5f * depth) * 1.15f;
                f.size   = (0.030f + 0.020f * hash1(k * 9.3f)) * (0.7f + 0.5f * depth);
                const int kind = (int)(hash1(k * 5.7f) * 9.0f);
                f.petals = kind < 6 ? (5 + (kind & 1)) : 0;
                f.cycles = cyc[(int)(hash1(k * 2.9f) * 4.0f) & 3];
                f.phase  = hash1(k * 6.1f);
                f.col    = petalCols[(int)(hash1(k * 3.3f) * 7.0f) % 7];
                f.row    = (int)rows_.size();
                seedFlower(f, index, 0);
                row.xMin = std::min(row.xMin, f.x);
                row.xMax = std::max(row.xMax, f.x);
                row.idx.push_back((int)flowers_.size());
                flowers_.push_back(f);
            }
            std::sort(row.idx.begin(), row.idx.end(), [](int a, int b2) { return flowers_[a].x < flowers_[b2].x; });
            row.nodeL = graph_.add(row.xMin - 0.08f, row.y + row.standDy);
            row.nodeR = graph_.add(row.xMax + 0.08f, row.y + row.standDy);
            graph_.link(row.nodeL, row.nodeR);
            rows_.push_back(row);
        }
    }
    for (int b = 0; b < NB; b++)                                     // aisles at both ends of a bed
        for (int r = bedFirstRow[b]; r < bedFirstRow[b] + beds[b].rows - 1; r++)
        {
            graph_.link(rows_[r].nodeL, rows_[r + 1].nodeL);
            graph_.link(rows_[r].nodeR, rows_[r + 1].nodeR);
        }
    const int A = bedFirstRow[0], B = bedFirstRow[1], C = bedFirstRow[2], D = bedFirstRow[3], E = bedFirstRow[4], F = bedFirstRow[5];
    graph_.link(rows_[A].nodeL, p1); graph_.link(rows_[A].nodeL, p2); graph_.link(rows_[A + 1].nodeL, p2);
    graph_.link(rows_[A + 2].nodeL, p2); graph_.link(rows_[A + 3].nodeL, p3);
    graph_.link(rows_[B].nodeR, p2); graph_.link(rows_[B + 1].nodeR, p2); graph_.link(rows_[B + 1].nodeL, s1); graph_.link(rows_[B].nodeL, nodePond_); graph_.link(rows_[B + 1].nodeL, nodePond_);
    graph_.link(rows_[C].nodeR, p1); graph_.link(rows_[C].nodeL, p2);
    graph_.link(rows_[D].nodeL, farFoot); graph_.link(rows_[D].nodeR, farFoot);           // the far bank: only over the bridge
    graph_.link(rows_[E].nodeL, p1); graph_.link(rows_[E].nodeR, p0);
    graph_.link(rows_[F].nodeL, p0);

    // butterfly meadow points
    meadow_.clear();
    const float mx[] = {1.5f, 1.0f, 0.5f, 0.0f, -0.5f, -1.0f, -1.5f, 1.2f, 0.2f, -0.1f, -1.3f, -0.2f};
    const float my[] = {-0.55f, -0.75f, -0.45f, -0.30f, -0.50f, -0.20f, -0.20f, -0.30f, -0.90f, -0.80f, -0.24f, -0.15f};
    for (int i = 0; i < 12; i++) meadow_.push_back(Vec2{mx[i], my[i]});
}

// ----------------------------------------------------------------------------
// Gardener
// ----------------------------------------------------------------------------
void setState(GState s) { g_.state = s; stateTime_ = 0.0f; }

// keep the gardener out of the water (the bridge deck is dry): move to the nearest shore point
void pushOutOfPond(float& x, float& y)
{
    if (!inLake(x, y) || onBridge(x, y)) return;
    float best = 1e9f; cg::V2 q(x, y);
    for (size_t i = 1; i < shore_.size(); i++)
    {
        const cg::V2 a = shore_[i - 1], ab = shore_[i] - a;
        const float t = clampf(cg::dot(cg::V2(x, y) - a, ab) / std::max(1e-6f, cg::dot(ab, ab)), 0.0f, 1.0f);
        const cg::V2 c = a + ab * t;
        const float d = cg::len(cg::V2(x, y) - c);
        if (d < best) { best = d; q = c; }
    }
    // step a little further onto land
    const cg::V2 dir = q - cg::V2(x, y);
    const float l = std::max(1e-5f, cg::len(dir));
    x = q.x + dir.x / l * 0.03f; y = q.y + dir.y / l * 0.03f;
}

void planRouteToNode(int node)
{
    route_.clear();
    routeIdx_ = 0;
    const int s = pf::nearestNode(graph_, Vec2{g_.x, g_.y});
    const std::vector<int> p = pf::aStar(graph_, s, node);
    for (int n : p) route_.push_back(graph_.pos[n]);
    if (route_.empty()) route_.push_back(graph_.pos[node]);
}

float travelTime(int node)
{
    const int s = pf::nearestNode(graph_, Vec2{g_.x, g_.y});
    float c = 0.0f;
    pf::aStar(graph_, s, node, &c);
    return (c + pf::dist(Vec2{g_.x, g_.y}, graph_.pos[s])) / WALK_SPEED;
}

bool followRoute(float dt)       // true when the last waypoint is reached
{
    g_.walking = false;
    if (routeIdx_ >= route_.size()) return true;
    const Vec2 t = route_[routeIdx_];
    const float dx = t.x - g_.x, dy = t.y - g_.y, d = std::sqrt(dx * dx + dy * dy);
    const float step = WALK_SPEED * dt;
    if (d <= step) { g_.x = t.x; g_.y = t.y; routeIdx_++; stuck_ = 0.0f; return routeIdx_ >= route_.size(); }
    g_.x += dx / d * step;
    g_.y += dy / d * step;
    pushOutOfPond(g_.x, g_.y);
    {   // safety net: if an obstacle keeps him from getting closer, give up on this waypoint
        const float nd = std::hypot(t.x - g_.x, t.y - g_.y);
        stuck_ = (nd > d - step * 0.25f) ? stuck_ + dt : 0.0f;
        if (stuck_ > 1.0f) { g_.x = t.x; g_.y = t.y; routeIdx_++; stuck_ = 0.0f; return routeIdx_ >= route_.size(); }
    }
    g_.walking = true;
    g_.walkPhase += dt * 8.0f;
    if (std::fabs(dx) > 0.004f) g_.facing += ((dx > 0 ? 1.0f : -1.0f) - g_.facing) * std::min(1.0f, dt * 9.0f);
    return false;
}

void goHome();
void decideNext();

void goToPond()
{
    planRouteToNode(nodePond_);
    setState(GState::ToPond);
}

void goHome()
{
    planRouteToNode(nodeDoor_);
    setState(GState::Returning);
    std::snprintf(schedInfo_, sizeof(schedInfo_), "ALL FLOWERS OK - GOING HOME");
}

// EDF scheduling: put every row that has thirsty flowers into a min-heap ordered by deadline.
bool planNextRow()
{
    typedef std::pair<float, int> Item;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
    std::vector<float> rowDeadline(rows_.size(), 0.0f);
    for (size_t r = 0; r < rows_.size(); r++)
    {
        float dl = 1e9f;
        for (int i : rows_[r].idx) if (isNeedy(flowers_[i])) dl = std::min(dl, deadlineOf(flowers_[i]));
        if (dl > 1e8f) continue;
        rowDeadline[r] = dl;
        const float travel = std::min(travelTime(rows_[r].nodeL), travelTime(rows_[r].nodeR));
        heap.push(Item(dl + travel, (int)r));                 // key = time left + time to get there
    }
    if (heap.empty()) return false;
    const int r = heap.top().second;

    // systematic traversal: start at the nearest end, walk the row in one direction
    const bool fromLeft = travelTime(rows_[r].nodeL) <= travelTime(rows_[r].nodeR);
    rowQueue_.clear();
    std::vector<int> order = rows_[r].idx;
    if (!fromLeft) std::reverse(order.begin(), order.end());
    for (int i : order) if (isNeedy(flowers_[i])) rowQueue_.push_back(i);
    rowPos_ = 0;
    rowDir_ = fromLeft ? 1 : -1;
    curRow_ = r;
    std::snprintf(schedInfo_, sizeof(schedInfo_), "ROW %s FIRST - DEADLINE %.0fS - %s", rows_[r].name, rowDeadline[r],
                  fromLeft ? "LEFT>RIGHT" : "RIGHT>LEFT");
    planRouteToNode(fromLeft ? rows_[r].nodeL : rows_[r].nodeR);
    setState(GState::ToRow);
    return true;
}

void visitNext()
{
    while (rowPos_ < rowQueue_.size() && !isNeedy(flowers_[rowQueue_[rowPos_]])) rowPos_++;
    if (rowPos_ >= rowQueue_.size()) { decideNext(); return; }
    if (g_.can < 1.0f) { goToPond(); return; }
    targetFlower_ = rowQueue_[rowPos_];
    route_.clear();
    route_.push_back(standFor(flowers_[targetFlower_]));
    routeIdx_ = 0;
    setState(GState::ToFlower);
}

void decideNext()
{
    if (needyCount() == 0)  { goHome(); return; }
    if (g_.can < 1.0f)      { goToPond(); return; }
    if (!planNextRow())       goHome();
}

void resetGardener()
{
    g_ = Gardener();
    g_.x = DOOR.x; g_.y = DOOR.y; g_.alpha = 0.0f; g_.facing = 1.0f;
    g_.state = GState::InHouse;
    stateTime_ = 0.0f;
    route_.clear(); routeIdx_ = 0; rowQueue_.clear(); rowPos_ = 0; curRow_ = -1; targetFlower_ = -1;
    std::snprintf(schedInfo_, sizeof(schedInfo_), "SCHEDULER IDLE");
}

void updateGardener(float dt)
{
    g_.time += dt;
    stateTime_ += dt;
    switch (g_.state)
    {
    case GState::InHouse:
        g_.alpha = 0.0f;
        if (stateTime_ > MIN_INSIDE && shouldWake())
        {
            g_.x = DOOR.x; g_.y = DOOR.y;
            setState(GState::Leaving);
        }
        break;
    case GState::Leaving:
        g_.alpha = std::min(1.0f, stateTime_ / 0.4f);
        if (stateTime_ >= 0.4f) decideNext();
        break;
    case GState::ToPond:
        if (followRoute(dt)) { g_.facing = -1.0f; setState(GState::Filling); }  // faces the pond (on his left)
        break;
    case GState::Filling:
        g_.filling = std::min(1.0f, stateTime_ / 0.4f);
        g_.can = std::min((float)CAN_CAPACITY, g_.can + CAN_CAPACITY * dt / FILL_TIME);
        if (stateTime_ >= FILL_TIME) { g_.can = (float)CAN_CAPACITY; g_.filling = 0.0f; decideNext(); }
        break;
    case GState::ToRow:
        if (followRoute(dt)) visitNext();
        break;
    case GState::ToFlower:
        if (followRoute(dt))
        {
            g_.facing = (float)rowDir_;
            setState(GState::Watering);
        }
        break;
    case GState::Watering:
    {
        Flower& f = flowers_[targetFlower_];
        g_.watering = std::min(1.0f, stateTime_ / 0.3f);
        if (!f.dead) f.water = std::min(1.0f, f.water + POUR_AMOUNT * dt / POUR_TIME);
        if (stateTime_ >= POUR_TIME)
        {
            g_.can = std::max(0.0f, g_.can - 1.0f);
            g_.watering = 0.0f;
            rowPos_++;
            visitNext();
        }
        break;
    }
    case GState::Returning:
        wakeTimer_ += dt;
        if (wakeTimer_ > 0.5f) { wakeTimer_ = 0.0f; if (!sentHome_ && shouldWake()) { decideNext(); break; } }   // new work appeared
        if (followRoute(dt)) setState(GState::Entering);
        break;
    case GState::Entering:
        g_.alpha = std::max(0.0f, 1.0f - stateTime_ / 0.4f);
        if (stateTime_ >= 0.4f) { g_.alpha = 0.0f; g_.x = DOOR.x; g_.y = DOOR.y; sentHome_ = false; setState(GState::InHouse); }
        break;
    case GState::Dragged:
        g_.walking = false;               // stands still while the user chooses a destination
        break;
    case GState::UserWalk:
        if (followRoute(dt)) { g_.watering = 0.0f; decideNext(); }
        break;
    }
    // the garden gate swings open while the gardener is near it (eased, never snaps)
    {
        const bool near = g_.alpha > 0.05f && g_.state != GState::Dragged && std::fabs(g_.x - 0.5f * (GATE_X0 + GATE_X1)) < 0.16f && std::fabs(g_.y - FENCE_Y) < 0.10f;
        gate_ += ((near ? 1.0f : 0.0f) - gate_) * std::min(1.0f, dt * 6.0f);
    }
    if (g_.state != GState::Filling) g_.filling = std::max(0.0f, g_.filling - dt * 3.0f);
    if (g_.state != GState::Watering) g_.watering = std::max(0.0f, g_.watering - dt * 3.0f);
    if (g_.alpha < 0.999f && g_.state != GState::Leaving && g_.state != GState::Entering && g_.state != GState::InHouse)
        g_.alpha = 1.0f;
}

bool gardenerVisibleForClick()
{
    return g_.state != GState::InHouse && g_.state != GState::Leaving && g_.state != GState::Entering;
}

// ----------------------------------------------------------------------------
// Butterflies  (Reynolds steering: seek / arrival / flee)
// ----------------------------------------------------------------------------
struct Personality
{
    const char* name;
    float detectR;      // cursor closer than this scares it
    float reactDelay;   // seconds the cursor must stay inside detectR before it reacts
    float dwell;        // (kind 3) seconds the cursor may hover on it
    float fleeSpeed, fleeMin, maxSpeed, maxForce, cooldown;
    bool  hardMin;      // never lets the cursor closer than TWO_CM
    float homeX, homeY, homeR;
    Color col;
    float size;
};

const Personality PERS[4] = {
    {"HIGHLY SENSITIVE",  0.46f, 0.00f, 0.0f, 1.35f, 1.30f, 0.42f, 2.6f, 0.0f, false,  1.00f, -0.55f, 0.90f, rgb(1.00f, 0.62f, 0.20f), 0.045f},
    {"MODERATE",          0.30f, 0.30f, 0.0f, 1.00f, 1.10f, 0.36f, 2.2f, 0.0f, false, -1.00f, -0.70f, 0.90f, rgb(0.55f, 0.55f, 1.00f), 0.042f},
    {"STRONG REPULSION",  0.42f, 0.00f, 0.0f, 1.80f, 0.90f, 0.40f, 2.6f, 0.0f, true,   0.00f, -0.45f, 1.10f, rgb(1.00f, 0.45f, 0.70f), 0.040f},
    {"LEAST SENSITIVE",   0.10f, 0.00f, 1.5f, 0.85f, 2.00f, 0.32f, 1.8f, 1.2f, false, -0.30f, -0.25f, 1.30f, rgb(0.70f, 0.95f, 0.45f), 0.038f},
};

const float BX0 = -1.72f, BX1 = 1.72f, BY0 = -0.98f, BY1 = 0.52f;

float rnd(Butterfly& b) { Rng r; r.s = b.rng ? b.rng : 1u; const float v = r.next(); b.rng = r.s; return v; }

void clampBounds(float& x, float& y) { x = clampf(x, BX0, BX1); y = clampf(y, BY0, BY1); }

void chooseWaypoint(Butterfly& b, bool cursorOn, float cx, float cy)
{
    // roulette-wheel selection over candidate flowers / meadow spots
    const Personality& P = PERS[b.kind];
    struct Cand { float x, y, w; };
    std::vector<Cand> cands;
    auto consider = [&](float x, float y, float bonus)
    {
        if (cursorOn && std::hypot(x - cx, y - cy) < P.detectR * 1.3f + 0.15f) return;      // never fly into the cursor
        const float d = std::hypot(x - b.x, y - b.y);
        if (d < 0.15f) return;
        const float hd   = std::hypot(x - P.homeX, y - P.homeY) / P.homeR;
        const float home = std::exp(-hd * hd);                                              // each butterfly has a home area
        cands.push_back({x, y, bonus * (0.15f + home) / (0.25f + d)});
    };
    for (const Vec2& m : meadow_) consider(m.x, m.y, 1.0f);
    for (const Flower& f : flowers_)
        if (!f.dead && f.growth > 0.6f) consider(f.x, f.y + f.h * 0.9f, 2.5f * f.growth);  // blooming flowers = nectar
    if (cands.empty()) { b.wpX = P.homeX; b.wpY = P.homeY; return; }
    float total = 0.0f;
    for (const Cand& c : cands) total += c.w;
    float pick = rnd(b) * total;
    for (const Cand& c : cands)
    {
        pick -= c.w;
        if (pick <= 0.0f) { b.wpX = c.x; b.wpY = c.y; return; }
    }
    b.wpX = cands.back().x; b.wpY = cands.back().y;
}

void limit(float& x, float& y, float m)
{
    const float l = std::hypot(x, y);
    if (l > m && l > 1e-6f) { x *= m / l; y *= m / l; }
}

void resetButterflies()
{
    for (int i = 0; i < 4; i++)
    {
        Butterfly& b = bf_[i];
        b = Butterfly();
        b.kind = i;
        b.col  = PERS[i].col;
        b.size = PERS[i].size;
        b.rng  = 2654435761u * (unsigned)(i + 3);
        b.x = PERS[i].homeX; b.y = PERS[i].homeY;
        b.flapPhase = i * 1.7f;
        chooseWaypoint(b, false, 0, 0);
    }
}

void updateButterfly(Butterfly& b, float dt, bool cursorOn, float cx, float cy)
{
    const Personality& P = PERS[b.kind];
    const float dist = cursorOn ? std::hypot(b.x - cx, b.y - cy) : 1e9f;
    b.cooldown = std::max(0.0f, b.cooldown - dt);

    // ---- does the cursor frighten it? (each personality has its own rule) ----
    bool trigger = false;
    if (P.dwell > 0.0f)                       // least sensitive: cursor may rest on it for a while
    {
        if (dist < P.detectR) b.dwell += dt; else b.dwell = std::max(0.0f, b.dwell - 2.0f * dt);
        trigger = dist < P.detectR * 1.5f && b.dwell >= P.dwell && b.cooldown <= 0.0f;
    }
    else
    {
        if (dist < P.detectR) b.react += dt; else b.react = std::max(0.0f, b.react - 2.0f * dt);
        trigger = dist < P.detectR && b.react >= P.reactDelay;
    }

    if (!b.fleeing && trigger)
    {
        b.fleeing = true; b.feeding = false;
        b.fleeTimer = P.fleeMin;
        b.dwell = 0.0f; b.react = 0.0f;
    }

    if (b.fleeing)
    {
        // escape direction: away from the cursor, bent toward the side with more room
        float ax = b.x - cx, ay = b.y - cy;
        float l = std::hypot(ax, ay);
        if (!cursorOn || l < 1e-4f) { ax = b.fleeDirX; ay = b.fleeDirY; l = std::hypot(ax, ay); if (l < 1e-4f) { ax = 1; ay = 0; l = 1; } }
        ax /= l; ay /= l;
        // containment steering (Reynolds): the borders push back before the butterfly reaches them,
        // so it curves along the edge instead of being trapped in a corner
        {
            const float R = 0.45f;
            auto push = [&](float d) { const float k = clampf(1.0f - d / R, 0.0f, 1.0f); return 2.2f * k * k; };
            ax += push(b.x - BX0) - push(BX1 - b.x);
            ay += push(b.y - BY0) - push(BY1 - b.y);
            const float l2 = std::hypot(ax, ay);
            if (l2 > 1e-4f) { ax /= l2; ay /= l2; }
        }
        // escape planning: test 16 headings; score = predicted clearance from the cursor after
        // 0.35 s + room left to the borders (+ a small preference for the plain "away" direction)
        float bestScore = -1e9f, bx = ax, by = ay;
        for (int k = 0; k < 16; k++)
        {
            const float ang = TAU * k / 16.0f, dx = std::cos(ang), dy = std::sin(ang);
            float px = b.x + dx * P.fleeSpeed * 0.35f, py = b.y + dy * P.fleeSpeed * 0.35f;
            clampBounds(px, py);
            const float room = std::min(std::min(px - BX0, BX1 - px), std::min(py - BY0, BY1 - py));
            const float clear = cursorOn ? std::hypot(px - cx, py - cy) : 1.0f;
            const float turn = dx * std::cos(std::atan2(b.vy, b.vx)) + dy * std::sin(std::atan2(b.vy, b.vx));   // avoid U-turns
            const float score = clear + 0.9f * std::min(room, 0.35f) + 0.25f * (dx * ax + dy * ay) + 0.08f * turn;
            if (score > bestScore) { bestScore = score; bx = dx; by = dy; }
        }
        b.fleeDirX = bx; b.fleeDirY = by;
        // SMOOTH escape (no "thrown away" look):
        //  * urgency grows smoothly as the cursor gets closer
        //  * the heading turns toward the escape direction with a LIMITED angular speed
        //  * the speed changes with a LIMITED acceleration
        const float urgency = cursorOn ? 1.0f - smoothstep(TWO_CM, P.detectR * 1.6f + 0.05f, dist) : 0.3f;
        const float targetSpeed = P.fleeSpeed * (0.55f + 0.45f * urgency);
        float speed = std::hypot(b.vx, b.vy);
        float heading = speed > 1e-3f ? std::atan2(b.vy, b.vx) : std::atan2(by, bx);
        float want = std::atan2(by, bx), dAng = want - heading;
        while (dAng > PI) dAng -= TAU;
        while (dAng < -PI) dAng += TAU;
        const float maxTurn = (4.0f + 4.0f * urgency) * dt;            // rad per frame
        heading += clampf(dAng, -maxTurn, maxTurn);
        const float maxAcc = P.maxForce * 0.75f * dt;
        speed += clampf(targetSpeed - speed, -maxAcc, maxAcc);
        b.vx = std::cos(heading) * speed;
        b.vy = std::sin(heading) * speed;
        b.fleeTimer -= dt;
        b.shimmer = SHIMMER_TIME;                                   // only butterflies that evade get the glow
        const float safe = P.dwell > 0.0f ? P.detectR * 4.0f : P.detectR * 1.35f;
        if (b.fleeTimer <= 0.0f && (!cursorOn || dist > safe))
        {
            b.fleeing = false;
            b.cooldown = P.cooldown;
            chooseWaypoint(b, cursorOn, cx, cy);
        }
    }
    else if (b.feeding)                                            // hover on a flower
    {
        b.vx *= std::exp(-4.0f * dt); b.vy *= std::exp(-4.0f * dt);
        b.feedTimer -= dt;
        if (b.feedTimer <= 0.0f) { b.feeding = false; chooseWaypoint(b, cursorOn, cx, cy); }
    }
    else                                                           // seek the waypoint (with arrival slow-down)
    {
        const float dx = b.wpX - b.x, dy = b.wpY - b.y, d = std::hypot(dx, dy);
        if (d < 0.05f) { b.feeding = true; b.feedTimer = 0.8f + 1.4f * rnd(b); }
        else
        {
            const float sp = P.maxSpeed * std::min(1.0f, d / 0.25f);
            float wx = dx / d * sp, wy = dy / d * sp;
            const float fl = std::sin(simTime_ * 5.0f + b.kind * 2.0f) * 0.22f * P.maxSpeed;   // fluttery side motion
            wx += -dy / d * fl; wy += dx / d * fl;
            float sx = wx - b.vx, sy = wy - b.vy;
            limit(sx, sy, P.maxForce * dt);
            b.vx += sx; b.vy += sy;
        }
        // an unlucky waypoint inside the cursor's zone: pick another
        if (cursorOn && std::hypot(b.wpX - cx, b.wpY - cy) < P.detectR * 0.8f) chooseWaypoint(b, cursorOn, cx, cy);
    }

    b.x += b.vx * dt; b.y += b.vy * dt;
    // soft keep-in-garden
    if (b.x < BX0 + 0.05f) b.vx += 1.5f * dt;
    if (b.x > BX1 - 0.05f) b.vx -= 1.5f * dt;
    if (b.y < BY0 + 0.05f) b.vy += 1.5f * dt;
    if (b.y > BY1 - 0.05f) b.vy -= 1.5f * dt;
    // collision response with the garden borders: reflect the normal velocity (damped)
    if (b.x <= BX0 && b.vx < 0.0f) b.vx = -0.5f * b.vx;
    if (b.x >= BX1 && b.vx > 0.0f) b.vx = -0.5f * b.vx;
    if (b.y <= BY0 && b.vy < 0.0f) b.vy = -0.5f * b.vy;
    if (b.y >= BY1 && b.vy > 0.0f) b.vy = -0.5f * b.vy;
    clampBounds(b.x, b.y);

    // strong repulsion: hard minimum distance - the cursor can never reach it
    if (P.hardMin && cursorOn)
    {
        float dx = b.x - cx, dy = b.y - cy, d = std::hypot(dx, dy);
        if (d < TWO_CM)
        {
            if (d < 1e-4f) { dx = 1.0f; dy = 0.0f; d = 1.0f; }
            dx /= d; dy /= d;
            float nx = cx + dx * TWO_CM, ny = cy + dy * TWO_CM;
            float tx = nx, ty = ny;
            clampBounds(tx, ty);
            if (std::hypot(tx - cx, ty - cy) < TWO_CM - 1e-3f)        // pinned against an edge:
            {                                                           // nearest in-bounds point of the safety circle
                float best = 1e9f;
                for (int k = 0; k < 24; k++)
                {
                    const float a = TAU * k / 24.0f;
                    const float px = cx + std::cos(a) * TWO_CM * 1.02f, py = cy + std::sin(a) * TWO_CM * 1.02f;
                    if (px < BX0 || px > BX1 || py < BY0 || py > BY1) continue;
                    const float d2 = (px - b.x) * (px - b.x) + (py - b.y) * (py - b.y);
                    if (d2 < best) { best = d2; tx = px; ty = py; }
                }
            }
            // move toward the safe point with a bounded speed (2.6 u/s): a fast cursor can never
            // catch it, but a sudden jump of the cursor does not throw the butterfly away
            const float cx2 = tx - b.x, cy2 = ty - b.y, cd = std::hypot(cx2, cy2), maxStep = 2.6f * dt;
            if (cd > maxStep) { b.x += cx2 / cd * maxStep; b.y += cy2 / cd * maxStep; }
            else { b.x = tx; b.y = ty; }
            if (cd > 1e-4f)                                         // fly WITH the correction, not against it
            {
                const float k = 1.0f - std::exp(-dt * 10.0f), sp = std::max(0.9f, std::hypot(b.vx, b.vy));
                b.vx += (cx2 / cd * sp - b.vx) * k; b.vy += (cy2 / cd * sp - b.vy) * k;
            }
            b.fleeing = true; b.fleeTimer = std::max(b.fleeTimer, 0.5f);
            b.shimmer = SHIMMER_TIME;
        }
    }

    // wings + glow bookkeeping
    const float wantRate = b.fleeing ? 24.0f : 14.0f + 2.0f * b.kind;     // wing beats ease in/out
    b.flapRate += (wantRate - b.flapRate) * (1.0f - std::exp(-dt * 3.0f));
    b.flapPhase += dt * b.flapRate;
    if (!b.fleeing) b.shimmer = std::max(0.0f, b.shimmer - dt);
    for (Spark& s : b.sparks)
        if (s.on) { s.age += dt; s.x += s.vx * dt; s.y += s.vy * dt; s.vy -= 0.10f * dt; if (s.age > 0.9f) s.on = false; }
    if (b.shimmer > 0.0f)
    {
        b.sparkTimer -= dt;
        if (b.sparkTimer <= 0.0f)
        {
            b.sparkTimer = 0.045f;
            for (Spark& s : b.sparks)
                if (!s.on)
                {
                    s.on = true; s.age = 0.0f;
                    s.x = b.x + (rnd(b) - 0.5f) * 0.07f; s.y = b.y + (rnd(b) - 0.5f) * 0.07f;
                    s.vx = (rnd(b) - 0.5f) * 0.10f; s.vy = (rnd(b) - 0.5f) * 0.06f;
                    break;
                }
        }
    }
}

// ----------------------------------------------------------------------------
// Input: drag the gardener, click the pond, click the windmill
// ----------------------------------------------------------------------------
bool inPond(float x, float y) { return inLake(x, y) && !onBridge(x, y); }
bool overHouse(float x, float y) { return x >= HOUSE_X0 && x <= HOUSE_X1 && y >= HOUSE_Y0 - 0.06f && y <= HOUSE_Y1 + 0.40f; }
bool onDoorStone(float x, float y) { return std::fabs(x - STONE_X) < 0.10f && std::fabs(y - STONE_Y) < 0.045f; }

// where the gardener lands when the user lets go of him
void resolveDrop(float x, float y)
{
    float u = 0.0f;
    if (onDoorStone(x, y))                                         // the door stone: he goes inside
    {
        g_.x = STONE_X; g_.y = STONE_Y;
        lastDrop_ = Drop::IntoHouse;
        goHome();                                                   // through the gate, then in at the door
        sentHome_ = true;
        return;
    }
    if (overHouse(x, y))                                           // on the house: he does NOT go in, he stands outside
    {
        g_.x = clampf(x, HOUSE_X0 + 0.05f, HOUSE_X1 - 0.05f);
        if (std::fabs(g_.x - DOOR.x) < 0.10f) g_.x = DOOR.x + (g_.x < DOOR.x ? -0.10f : 0.10f);   // not onto the door stone
        g_.y = DOOR.y - 0.005f;
        lastDrop_ = Drop::House;
    }
    else if (onBridge(x, y, &u))                                   // on the bridge: snap onto the deck
    {
        const cg::V2 q = bridgeAt(u);
        g_.x = q.x; g_.y = q.y;
        lastDrop_ = Drop::Bridge;
    }
    else
    {
        g_.x = clampf(x, -1.70f, 1.70f);
        g_.y = clampf(y, -1.0f, FENCE_Y - 0.02f);                   // the fence keeps him in the garden
        lastDrop_ = inLake(g_.x, g_.y) ? Drop::Water : Drop::Ground;
        pushOutOfPond(g_.x, g_.y);                                  // dropped into the water: he wades to the shore
    }
    decideNext();                                                  // carry on with the work from here
}

void handleInput(const ui::Mouse& m, bool interactive)
{
    if (!interactive || !m.valid)
    {
        if (g_.dragged && !interactive) { g_.dragged = false; resolveDrop(g_.x, g_.y); }
        pendPick_ = false;
        if (!interactive) return;
    }

    // ---- carrying the gardener (like the playground children): he follows the cursor ----
    if (g_.dragged)
    {
        if (m.valid)
        {
            g_.x = clampf(m.x + grabOff_.x, -1.72f, 1.72f);
            g_.y = clampf(m.y + grabOff_.y, -1.02f, 0.45f);
            g_.targetX = g_.x; g_.targetY = g_.y;
            g_.walkPhase += 0.25f;                                  // legs dangle
        }
        if (!m.down) { g_.dragged = false; resolveDrop(g_.x, g_.y); }
        return;
    }
    if (pendPick_)                                                 // drag threshold: a plain click does not move him
    {
        if (!m.down) { pendPick_ = false; return; }
        if (std::hypot(m.x - pendX_, m.y - pendY_) < 0.02f) return;
        pendPick_ = false;
        g_.dragged = true;
        sentHome_ = false;
        grabOff_.x = g_.x - pendX_; grabOff_.y = g_.y - pendY_;
        setState(GState::Dragged);
        return;
    }

    if (m.pressed && m.valid)
    {
        const bool hitGardener = gardenerVisibleForClick() && std::fabs(m.x - g_.x) < 0.14f && m.y > g_.y - 0.04f && m.y < g_.y + 0.52f;
        if (hitGardener) { pendPick_ = true; pendX_ = m.x; pendY_ = m.y; return; }
        // he is inside: press on the house WALL (not the roof) -> he comes straight out, into your hand
        if (g_.state == GState::InHouse && m.x >= HOUSE_X0 && m.x <= HOUSE_X1 && m.y >= HOUSE_Y0 && m.y <= HOUSE_Y1)
        {
            g_.alpha = 1.0f;
            g_.dragged = true;
            grabOff_.x = 0.0f; grabOff_.y = -0.22f;
            g_.x = m.x; g_.y = m.y - 0.22f;
            setState(GState::Dragged);
            return;
        }
        if (std::hypot(m.x - windX_, m.y - windY_) < 0.34f)       // wind gust: windmill spins faster
        {
            windBoost_ = std::min(420.0f, windBoost_ + 200.0f);
        }
        else if (inPond(m.x, m.y))                                 // ripple exactly where the user clicked
        {
            if (ripples_.size() >= 12) ripples_.erase(ripples_.begin());
            ripples_.push_back(Ripple{m.x, m.y, 0.0f});
        }
    }
}

}   // namespace

// ----------------------------------------------------------------------------
// public API
// ----------------------------------------------------------------------------
// ---- lake + bridge geometry ----
const std::vector<cg::V2>& lakeShore() { return shore_; }
bool inLake(float x, float y) { return cg::pointInPolygon(cg::V2(x, y), lakePoly_); }
cg::V2 bridgeAt(float u)
{
    const cg::V2 a(BRIDGE_X0, BRIDGE_Y0), b(BRIDGE_X1, BRIDGE_Y1);
    return a + (b - a) * u + cg::V2(0.0f, BRIDGE_ARCH * std::sin(u * PI));
}
bool onBridge(float x, float y, float* uOut)
{
    // nearest deck sample; the deck is about 0.09 wide (more near the viewer)
    float best = 1e9f, bu = 0.0f;
    for (int i = 0; i <= 40; i++) { const float u = i / 40.0f; const cg::V2 q = bridgeAt(u); const float d = std::hypot(x - q.x, (y - q.y) * 1.6f); if (d < best) { best = d; bu = u; } }
    if (uOut) *uOut = bu;
    return best < 0.065f;
}
float gateOpen() { return gate_; }
Drop  lastDrop() { return lastDrop_; }

void init()
{
    // lake shore (far-left -> right -> bottom); the polygon closes off-screen at the bottom-left,
    // so the water runs off the left and bottom edges like part of a much bigger lake
    shore_ = {cg::V2(-2.20f, -0.355f), cg::V2(-1.80f, -0.360f), cg::V2(-1.50f, -0.370f), cg::V2(-1.20f, -0.375f), cg::V2(-0.95f, -0.370f),
              cg::V2(-0.72f, -0.375f), cg::V2(-0.56f, -0.400f), cg::V2(-0.46f, -0.460f), cg::V2(-0.42f, -0.540f), cg::V2(-0.37f, -0.600f),
              cg::V2(-0.30f, -0.640f), cg::V2(-0.29f, -0.700f), cg::V2(-0.35f, -0.750f), cg::V2(-0.44f, -0.790f), cg::V2(-0.58f, -0.850f),
              cg::V2(-0.68f, -0.930f), cg::V2(-0.76f, -1.020f), cg::V2(-0.84f, -1.120f), cg::V2(-0.88f, -1.250f)};
    lakePoly_ = shore_;
    lakePoly_.push_back(cg::V2(-2.20f, -1.25f));
    buildLayout();
    reset();
}

void reset()
{
    for (size_t i = 0; i < flowers_.size(); i++) seedFlower(flowers_[i], (int)i, 0);
    resetGardener();
    resetButterflies();
    ripples_.clear();
    gate_ = 0.0f; lastDrop_ = Drop::Ground; pendPick_ = false; sentHome_ = false;
    windAngle_ = 0.0f; windBoost_ = 0.0f;
    simTime_ = 0.0;
    started_ = false;
}

void setWindmillHub(float x, float y) { windX_ = x; windY_ = y; }
float windAngle() { return windAngle_; }

void update(double dtIn, const AnimState& s, const ui::Mouse& m, bool interactive)
{
    float dt = (float)dtIn;
    if (dt > 0.05f) dt = 0.05f;
    if (dt < 0.0f)  dt = 0.0f;

    if (s.waiting || s.time < timeline::LIVING_START)   // the garden is not on screen yet (title / film):
        return;                                         // it simply pauses - restarting never resets it
    started_ = true;
    simTime_ += dt;

    handleInput(m, interactive);
    updateFlowers(dt);
    updateGardener(dt);
    const bool cursorOn = interactive && m.valid;
    for (Butterfly& b : bf_) updateButterfly(b, dt, cursorOn, m.x, m.y);

    for (Ripple& r : ripples_) r.age += dt;
    ripples_.erase(std::remove_if(ripples_.begin(), ripples_.end(), [](const Ripple& r) { return r.age > RIPPLE_LIFE; }), ripples_.end());

    // gameplay: report blooms / withered flowers in small batches (keeps the event feed readable)
    reportT_ += dt;
    if (reportT_ > 4.0f)
    {
        reportT_ = 0.0f;
        if (bloomed_ > 0)  game::add(game::Where::Garden, 2 * bloomed_, bloomed_ == 1 ? "A flower burst into full bloom" : "%d flowers burst into full bloom", bloomed_);
        if (withered_ > 0) game::add(game::Where::Garden, -5 * withered_, withered_ == 1 ? "A flower withered - it was too dry" : "%d flowers withered - too dry", withered_);
        bloomed_ = withered_ = 0;
    }

    windAngle_ += (36.0f + windBoost_) * dt;
    if (windAngle_ > 3600.0f) windAngle_ -= 3600.0f;
    windBoost_ *= std::exp(-dt / 2.5f);
}

const std::vector<Flower>&      flowers()     { return flowers_; }
const Gardener&                 gardener()    { return g_; }
const std::array<Butterfly, 4>& butterflies() { return bf_; }
const std::vector<Ripple>&      ripples()     { return ripples_; }

int thirstyCount() { return needyCount(); }
int deadCount()    { int n = 0; for (const Flower& f : flowers_) if (f.dead) n++; return n; }
int bloomCount()   { int n = 0; for (const Flower& f : flowers_) if (!f.dead && f.growth > 0.75f) n++; return n; }
const char* stateName(GState s) { return STATE_NAMES[(int)s]; }

void toggleHud() { hudOn_ = !hudOn_; }

void drawHud()
{
    if (!hudOn_) return;
    const float px = 0.0068f, lh = 0.068f, x0 = -WORLD_W + 0.05f;
    char line[5][96];
    int shimmering = 0;
    for (const Butterfly& b : bf_) if (b.shimmer > 0.0f) shimmering++;
    std::snprintf(line[0], 96, "GARDEN SIMULATION   (H = HIDE)");
    std::snprintf(line[1], 96, "GARDENER: %s%s%s", stateName(g_.state), curRow_ >= 0 && (g_.state == GState::ToFlower || g_.state == GState::Watering || g_.state == GState::ToRow) ? "  ROW " : "",
                  curRow_ >= 0 && (g_.state == GState::ToFlower || g_.state == GState::Watering || g_.state == GState::ToRow) ? rows_[curRow_].name : "");
    std::snprintf(line[2], 96, "CAN %d/%d   FLOWERS %d: THIRSTY %d  BLOOM %d  DEAD %d", (int)(g_.can + 0.5f), CAN_CAPACITY,
                  (int)flowers_.size(), thirstyCount(), bloomCount(), deadCount());
    std::snprintf(line[3], 96, "EDF HEAP: %s", schedInfo_);
    std::snprintf(line[4], 96, "BUTTERFLIES GLOWING: %d", shimmering);
    float w = 0.0f;
    for (int i = 0; i < 5; i++) w = std::max(w, ui::textWidth(line[i], px));
    const float y0 = 0.70f;
    glColor4f(0.0f, 0.0f, 0.0f, 0.45f);
    rect(x0 - 0.03f, y0 - 5 * lh + 0.03f, x0 + w + 0.03f, y0 + 0.06f);
    for (int i = 0; i < 5; i++)
        ui::drawText(x0, y0 - i * lh, px, i == 0 ? rgb(1.0f, 0.95f, 0.6f) : rgb(1, 1, 1), line[i]);
}

}   // namespace gardensim
