// stationsim.cpp - rules of the train station (see stationsim.h for the algorithm list)

#include "stationsim.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <queue>
#include <random>

namespace station
{
using namespace stationcfg;

namespace
{

const float INF = 1e9f;
const char* DEST[5]   = {"HILLTOP", "RIVERSIDE", "LAKEVIEW", "CITY CENTRE", "OLD MILL"};
const char* PREFIX[5] = {"IC", "RE", "EX", "IC", "RE"};

enum EdgeKind { FLAT, STAIRS, BRIDGE, XING };
struct Edge { int a, b; EdgeKind k; };
const Edge EDGES[] = {
    {N_ENTRY, N_GATE, FLAT}, {N_GATE, N_P1W, FLAT}, {N_P1W, N_P1M, FLAT}, {N_P1M, N_P1E, FLAT}, {N_P1E, N_P1X, FLAT},
    {N_P1W, N_BR1, STAIRS}, {N_BR1, N_BR2, BRIDGE}, {N_BR2, N_P2W, STAIRS},
    {N_P2W, N_P2M, FLAT}, {N_P2M, N_P2E, FLAT}, {N_P2E, N_P2X, FLAT}, {N_P1X, N_P2X, XING},
};
const int P1_NODES[4] = {N_P1W, N_P1M, N_P1E, N_P1X};
const int P2_NODES[4] = {N_P2W, N_P2M, N_P2E, N_P2X};

std::mt19937            rng(777u);
unsigned                seed0 = 777u;
float                   simT = 0;
std::vector<Service>    svcV;
std::vector<Train>      trainsV;
std::vector<Passenger>  paxV;
std::vector<Popup>      pops;
std::vector<LogLine>    logV;
Stats                   st;
int                     nextSvc = 1, nextPax = 1;
float                   lastPlan = 0, nextArrival = 0, replanT = 0;
bool                    arrivalsOn = true, homeHold = false, starterHold[2] = {false, false}, bridgeClosed = false;
bool                    lastCrossingOpen = true;
int                     dragId = -1;
int                     pendId = -1;           // pressed on a passenger, drag starts once the mouse moves
float                   pendX = 0, pendY = 0;
float                   dragDX = 0, dragDY = 0;
Input                   lastIn;
float                   lastBoard[2][DOORS];

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

Service* svcById(int id)
{
    for (Service& s : svcV) if (s.id == id) return &s;
    return nullptr;
}
Train* trainOfSvc(int id)
{
    for (Train& t : trainsV) if (t.svc == id) return &t;
    return nullptr;
}
Passenger* paxById(int id)
{
    for (Passenger& p : paxV) if (p.id == id) return &p;
    return nullptr;
}

float approachTime() { return (STOP_X - SPAWN_X - V_MAX * V_MAX / (2.0f * BRAKE)) / V_MAX + V_MAX / BRAKE; }
float rearOf(const Train& t) { return t.x - TRAIN_L; }

// ---------------------------------------------------------------------------
// Walkway graph + Dijkstra
// ---------------------------------------------------------------------------
bool crossingClear()
{
    for (const Train& t : trainsV)
    {
        if (!t.routed || t.track != 0) continue;
        const bool onIt = t.x > XING_X - 0.12f && rearOf(t) < XING_X + 0.12f;
        const bool coming = t.x > XING_X - 0.55f && t.x <= XING_X - 0.12f && t.v > 0.02f;
        if (onIt || coming) return false;
    }
    return true;
}

const Edge* findEdge(int a, int b)
{
    for (const Edge& e : EDGES) if ((e.a == a && e.b == b) || (e.a == b && e.b == a)) return &e;
    return nullptr;
}

float factorOf(EdgeKind k) { return k == STAIRS ? STAIR_FACTOR : k == BRIDGE ? BRIDGE_FACTOR : 1.0f; }

float costOf(const Edge& e)
{
    if (bridgeClosed && (e.k == STAIRS || e.k == BRIDGE)) return INF;
    if (e.k == XING && !lastCrossingOpen) return INF;
    const float len = std::hypot(NODE_X[e.a] - NODE_X[e.b], NODE_Y[e.a] - NODE_Y[e.b]);
    return len * factorOf(e.k) / WALK_SPEED;                  // seconds of walking
}

std::vector<int> dijkstra(int from, int to, float* outCost)
{
    std::vector<float> dist(N_COUNT, INF);
    std::vector<int>   prev(N_COUNT, -1);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;      // binary min-heap
    dist[from] = 0; pq.push({0.0f, from});
    while (!pq.empty())
    {
        const QE top = pq.top(); pq.pop();
        const int u = top.second;
        if (top.first > dist[u]) continue;                                // stale entry
        if (u == to) break;
        for (const Edge& e : EDGES)
        {
            if (e.a != u && e.b != u) continue;
            const int v = e.a == u ? e.b : e.a;
            const float w = costOf(e);
            if (w >= INF) continue;                                       // closed walkway
            if (dist[u] + w < dist[v]) { dist[v] = dist[u] + w; prev[v] = u; pq.push({dist[v], v}); }
        }
    }
    std::vector<int> path;
    if (dist[to] >= INF) { if (outCost) *outCost = INF; return path; }
    for (int v = to; v != -1; v = prev[v]) path.push_back(v);
    std::reverse(path.begin(), path.end());
    if (outCost) *outCost = dist[to];
    return path;
}

int nearestPlatformNode(int platform, float x)
{
    const int* nodes = platform == 0 ? P1_NODES : P2_NODES;
    int best = nodes[0];
    for (int i = 0; i < 4; i++) if (std::fabs(NODE_X[nodes[i]] - x) < std::fabs(NODE_X[best] - x)) best = nodes[i];
    return best;
}

int platformOfY(float y) { return std::fabs(y - P1_Y) < std::fabs(y - P2_Y) ? 0 : 1; }

// ---------------------------------------------------------------------------
// Passengers
// ---------------------------------------------------------------------------
int ticketPlatform(const Passenger& p)
{
    const Service* s = p.ticket >= 0 ? svcById(p.ticket) : nullptr;
    return s ? s->platform : -1;
}

int countDoor(int platform, int door, int exceptId)
{
    int n = 0;
    for (const Passenger& p : paxV)
        if (p.id != exceptId && p.door == door && p.ticket >= 0 && ticketPlatform(p) == platform &&
            (p.st == Passenger::Walk || p.st == Passenger::ToDoor || p.st == Passenger::Wait || p.st == Passenger::NoRoute))
            n++;
    return n;
}

int chooseDoor(const Passenger& p, int platform)                  // least-loaded door, then nearest
{
    int best = 0; float bestScore = INF;
    for (int d = 0; d < DOORS; d++)
    {
        float x, y; doorPos(platform, d, x, y);
        const float score = countDoor(platform, d, p.id) * 10.0f + std::fabs(x - p.x);
        if (score < bestScore) { bestScore = score; best = d; }
    }
    return best;
}

void describeRoute(Passenger& p)
{
    p.viaBridge = p.viaCrossing = false;
    for (size_t i = 1; i < p.path.size(); i++)
    {
        const Edge* e = findEdge(p.path[i - 1], p.path[i]);
        if (e && e->k == BRIDGE) p.viaBridge = true;
        if (e && e->k == XING) p.viaCrossing = true;
    }
}

// route from where the passenger is. `startNode` = the node he walks to first.
bool routeFrom(Passenger& p, int startNode, bool countReroute)
{
    int goal = N_ENTRY;
    if (p.ticket >= 0)
    {
        const int plat = ticketPlatform(p);
        if (plat < 0) return false;
        if (p.door < 0 || countReroute) p.door = chooseDoor(p, plat);
        float dx, dy; doorPos(plat, p.door, dx, dy);
        goal = nearestPlatformNode(plat, dx);
    }
    float cost = 0;
    std::vector<int> rest = dijkstra(startNode, goal, &cost);
    const std::vector<int> old(p.path.begin() + std::min<size_t>(p.pathIdx, p.path.size()), p.path.end());
    if (rest.empty())
    {
        p.path = {startNode}; p.pathIdx = 0; p.st = Passenger::NoRoute; p.routeCost = INF;
        return false;
    }
    p.path = rest; p.pathIdx = 0; p.routeCost = cost;
    if (p.st == Passenger::NoRoute || p.st == Passenger::Wait || p.st == Passenger::ToDoor || p.st == Passenger::Walk) p.st = Passenger::Walk;
    describeRoute(p);
    if (countReroute && old != p.path) { st.reroutes++; p.flash = 1.0f; }
    return true;
}

int currentTargetNode(const Passenger& p)        // the node a walking passenger is heading to now
{
    if (p.st == Passenger::Walk && p.pathIdx < (int)p.path.size()) return p.path[p.pathIdx];
    if (p.st == Passenger::NoRoute && !p.path.empty()) return p.path[0];
    return nearestPlatformNode(platformOfY(p.y), p.x);
}

void rerouteAll(bool onlyTicket = false, int ticket = -1)
{
    for (Passenger& p : paxV)
    {
        if (onlyTicket && p.ticket != ticket) continue;
        if (p.st != Passenger::Walk && p.st != Passenger::ToDoor && p.st != Passenger::Wait && p.st != Passenger::NoRoute) continue;
        // already standing on the right platform: nothing to re-plan
        if ((p.st == Passenger::Wait || p.st == Passenger::ToDoor) && platformOfY(p.y) == ticketPlatform(p)) continue;
        routeFrom(p, currentTargetNode(p), true);
    }
}

Passenger makePax(float x, float y)
{
    Passenger p;
    p.id = nextPax++;
    p.x = x; p.y = y;
    p.look = (int)(rnd() * 4) % 4; p.skin = (int)(rnd() * 4) % 4; p.coat = (int)(rnd() * 6) % 6;
    p.phase = rnd(0.0f, 6.0f);
    return p;
}

int pickTicket()
{
    std::vector<int> opts;
    for (const Service& s : svcV)
    {
        if (s.st == SvcState::Departed) continue;
        if (s.st == SvcState::AtPlatform) continue;                  // too late to buy a ticket for it
        if (s.estArr < simT + 10.0f) continue;
        opts.push_back(s.id);
        if (opts.size() >= 3) break;
    }
    if (opts.empty()) return -1;
    return opts[(size_t)(rnd() * opts.size()) % opts.size()];
}

int spawnTraveller(int ticket)
{
    if (ticket < 0) return -1;
    Passenger p = makePax(ENTRY_X - 0.06f, P1_Y);
    p.ticket = ticket;
    p.path = {N_ENTRY}; p.pathIdx = 0;
    routeFrom(p, N_ENTRY, false);
    paxV.push_back(p);
    st.entered++;
    return p.id;
}

void rebook(Passenger& p, const char* why)
{
    const int old = p.ticket;
    p.ticket = -1;
    for (const Service& s : svcV)                                    // the next train he can still reach
        if (s.id != old && (s.st == SvcState::Scheduled || s.st == SvcState::Approaching) && s.estArr > simT + 8.0f) { p.ticket = s.id; break; }
    st.missed++;
    if (p.ticket < 0) { p.ticket = -1; routeFrom(p, currentTargetNode(p), false); return; }
    const Service* s = svcById(p.ticket);
    popup(p.x, p.y + 0.26f, 1, "%s: NOW ON %s P%d", why, s->name, s->platform + 1);
    p.door = -1;
    routeFrom(p, currentTargetNode(p), true);
}

void updatePassengers(float dt)
{
    std::vector<int> done;
    for (Passenger& p : paxV)
    {
        p.flash = std::max(0.0f, p.flash - dt);
        if (p.st == Passenger::Dragged) continue;

        if (p.ticket >= 0)
        {
            const Service* s = svcById(p.ticket);
            if (!s || s->st == SvcState::Departed)
            {
                if (p.st != Passenger::Board) { say(1, "PASSENGER #%d MISSED THE TRAIN - RE-BOOKED", p.id); rebook(p, "MISSED"); }
            }
        }

        auto move = [&](float tx, float ty, float speed) -> bool {
            const float dx = tx - p.x, dy = ty - p.y, d = std::hypot(dx, dy);
            if (std::fabs(dx) > 0.003f) p.facing = dx > 0 ? 1.0f : -1.0f;
            if (d <= speed * dt) { p.x = tx; p.y = ty; return true; }
            p.x += dx / d * speed * dt; p.y += dy / d * speed * dt;
            p.phase += dt * 10.0f;
            return false;
        };

        switch (p.st)
        {
        case Passenger::Walk:
        {
            if (p.pathIdx >= (int)p.path.size())
            {
                if (p.ticket >= 0) p.st = Passenger::ToDoor;
                else p.st = Passenger::Exit;
                break;
            }
            const int n = p.path[p.pathIdx];
            float speed = WALK_SPEED;
            if (p.pathIdx > 0) { const Edge* e = findEdge(p.path[p.pathIdx - 1], n); if (e) speed = WALK_SPEED / factorOf(e->k); }
            if (move(NODE_X[n], NODE_Y[n], speed))
            {
                // about to step onto a walkway that just closed (gate down / bridge shut)? re-plan here
                if (p.pathIdx + 1 < (int)p.path.size())
                {
                    const Edge* e = findEdge(n, p.path[p.pathIdx + 1]);
                    if (e && costOf(*e) >= INF) { routeFrom(p, n, true); break; }
                }
                p.pathIdx++;
            }
            break;
        }
        case Passenger::NoRoute:
        {
            p.waitSince += dt;
            if (p.waitSince > 0.5f) { p.waitSince = 0; if (!p.path.empty()) routeFrom(p, p.path[0], false); }
            break;
        }
        case Passenger::ToDoor:
        {
            const int plat = ticketPlatform(p);
            if (plat < 0) break;
            if (platformOfY(p.y) != plat) { routeFrom(p, nearestPlatformNode(platformOfY(p.y), p.x), true); break; }
            float dx, dy; doorPos(plat, p.door, dx, dy);
            int rank = 0;
            for (const Passenger& o : paxV) if (o.id != p.id && o.st == Passenger::Wait && o.door == p.door && o.ticket >= 0 && ticketPlatform(o) == plat) rank++;
            const float ox = (rank % 2 ? 1.0f : -1.0f) * 0.035f * ((rank + 1) / 2), oy = 0.014f * (rank % 3);
            if (move(dx + ox, dy + oy, WALK_SPEED)) { p.st = Passenger::Wait; p.waitSince = simT; p.facing = plat == 0 ? 1.0f : -1.0f; }
            break;
        }
        case Passenger::Wait:
        {
            const int plat = ticketPlatform(p);
            if (plat < 0) break;
            Train* t = trainOfSvc(p.ticket);
            if (!t || t->st != Train::Dwell || t->doors < 0.95f || t->toAlight > 0 || t->departNow || t->closing) break;
            Service* s = svcById(p.ticket);
            // FIFO per door: the one who has waited longest goes first
            bool first = true;
            for (const Passenger& o : paxV)
                if (o.id != p.id && o.st == Passenger::Wait && o.ticket == p.ticket && o.door == p.door && o.waitSince < p.waitSince) first = false;
            if (!first || simT - lastBoard[plat][p.door] < BOARD_TIME) break;
            if (s->load >= CAPACITY) { say(1, "%s IS FULL - PASSENGER #%d WAITS FOR THE NEXT TRAIN", s->name, p.id); rebook(p, "TRAIN FULL"); break; }
            lastBoard[plat][p.door] = simT;
            s->load++;
            p.st = Passenger::Board;
            st.boarded++;
            break;
        }
        case Passenger::Board:
        {
            const int plat = ticketPlatform(p);
            float dx, dy; doorPos(plat < 0 ? 0 : plat, p.door, dx, dy);
            move(dx, dy - 0.035f, WALK_SPEED);
            p.alpha -= dt * 2.5f;
            if (p.alpha <= 0.0f) done.push_back(p.id);
            break;
        }
        case Passenger::Exit:
            move(ENTRY_X - 0.06f, P1_Y, WALK_SPEED);
            p.alpha -= dt * 2.0f;
            if (p.alpha <= 0.0f) { done.push_back(p.id); st.exited++; }
            break;
        case Passenger::Dragged:
            break;
        }
    }
    paxV.erase(std::remove_if(paxV.begin(), paxV.end(), [&](const Passenger& p) { return std::find(done.begin(), done.end(), p.id) != done.end(); }), paxV.end());
}

// ---------------------------------------------------------------------------
// Timetable and platform planning (greedy interval partitioning)
// ---------------------------------------------------------------------------
void addService()
{
    Service s;
    s.id = nextSvc++;
    s.dest = (int)(rnd() * 5) % 5;
    std::snprintf(s.name, sizeof(s.name), "%s %d", PREFIX[s.dest], 100 + s.id);
    lastPlan = (svcV.empty() ? simT + 3.0f : lastPlan + HEADWAY + rnd(-HEADWAY_JITTER, HEADWAY_JITTER));
    s.planArr = s.estArr = lastPlan;
    s.dwell = DWELL + rnd(-DWELL_JITTER, DWELL_JITTER);
    s.load = (int)rnd(18.0f, 48.0f);
    s.alight = (int)rnd(2.0f, 6.0f);
    s.platform = s.id % 2;
    svcV.push_back(s);
}

void replan()
{
    float busy[2] = {simT, simT};
    for (const Train& t : trainsV)
    {
        if (!t.routed) continue;
        const Service* s = svcById(t.svc);
        const float dwell = s ? s->dwell : DWELL;
        float until = simT;
        if (t.st == Train::Approach) until = simT + (STOP_X - t.x) / std::max(t.v, 0.3f) + dwell + 1.5f;
        else if (t.st == Train::Dwell) until = simT + std::max(0.0f, dwell - t.t) + 1.5f + (starterHold[t.track] ? 20.0f : 0.0f);
        else until = simT + std::max(0.0f, (STOP_X + 0.05f - rearOf(t)) / std::max(t.v, 0.2f));
        busy[t.track] = std::max(busy[t.track], until + CLEARANCE);
    }
    std::vector<Service*> todo;
    for (Service& s : svcV) if (!s.locked && (s.st == SvcState::Scheduled || s.st == SvcState::Approaching)) todo.push_back(&s);
    std::sort(todo.begin(), todo.end(), [](const Service* a, const Service* b) { return a->planArr < b->planArr; });
    for (Service* s : todo)
    {
        float tmin = s->planArr;
        if (const Train* t = trainOfSvc(s->id)) tmin = std::max(tmin, simT + (STOP_X - t->x) / V_MAX + 1.0f);
        else tmin = std::max(tmin, simT + approachTime());
        if (homeHold) tmin = std::max(tmin, simT + approachTime() + 2.0f);
        int p = s->platform;
        if (!s->forced && !s->confirmed)
        {
            // greedy: the platform that is free first; keep the current one unless the
            // other is clearly (> 2 s) earlier, so passengers are not sent back and forth
            const float e0 = std::max(tmin, busy[0]), e1 = std::max(tmin, busy[1]);
            const float eCur = p == 0 ? e0 : e1, eOther = p == 0 ? e1 : e0;
            if (eOther + 2.0f < eCur) p = 1 - p;
        }
        const float est = std::max(tmin, busy[p]);                    // no platform free -> delayed
        busy[p] = est + s->dwell + 1.5f + CLEARANCE;
        s->estArr = est;
        if (est - simT < CONFIRM_AHEAD) s->confirmed = true;
        if (p != s->platform)
        {
            s->platform = p; s->flash = 3.0f; st.platformChanges++;
            say(2, "PLANNER: %s TO %s MOVED TO PLATFORM %d", s->name, DEST[s->dest], p + 1);
            rerouteAll(true, s->id);
        }
    }
}

// ---------------------------------------------------------------------------
// Trains: signals, interlocking, kinematics
// ---------------------------------------------------------------------------
bool pointsOccupied(int exceptSvc)
{
    for (const Train& t : trainsV)
        if (t.svc != exceptSvc && t.x >= PTS_X0 - 0.02f && rearOf(t) <= PTS_X1 + 0.02f) return true;
    return false;
}

bool platformBlocked(int track, int exceptSvc)
{
    for (const Train& t : trainsV)
        if (t.svc != exceptSvc && t.routed && t.track == track && rearOf(t) < STOP_X + 0.05f) return true;
    return false;
}

Train* nextAtHome()          // the leading train that has not passed the home signal yet
{
    Train* best = nullptr;
    for (Train& t : trainsV) if (!t.routed && (!best || t.x > best->x)) best = &t;
    return best;
}

bool routeClearFor(const Train& t)
{
    const Service* s = svcById(t.svc);
    if (!s || homeHold) return false;
    return !pointsOccupied(t.svc) && !platformBlocked(s->platform, t.svc);
}

bool readyToLeave(const Train& t)
{
    const Service* s = svcById(t.svc);
    if (!s || t.st != Train::Dwell) return false;
    if (t.closing) return true;
    if (t.toAlight > 0 || t.doors < 0.95f) return false;
    if (t.departNow) return true;
    if (t.t < s->dwell) return false;
    if (t.t >= s->dwell + MAX_EXTRA_DWELL || s->load >= CAPACITY) return true;
    for (const Passenger& p : paxV)                       // keep the doors open while people are boarding
        if (p.ticket == t.svc && (p.st == Passenger::Wait || p.st == Passenger::Board)) return false;
    return true;
}

bool exitClear(const Train& t)
{
    for (const Train& o : trainsV)
        if (&o != &t && o.routed && o.track == t.track && o.x > t.x && rearOf(o) < STOP_X + 0.6f) return false;
    return true;
}

void drive(Train& t, float target, float dt)          // kinematics with exact stopping (v^2 = 2 a d)
{
    t.target = target;
    const float d = target - t.x;
    if (d <= 0.0005f && t.v < 0.05f) { t.v = 0.0f; return; }         // standing at (or behind) the target
    float a;
    const float brakeDist = t.v * t.v / (2.0f * BRAKE);
    if (d <= brakeDist + t.v * dt) a = -(t.v * t.v) / (2.0f * std::max(d, 1e-3f));   // a = v^2 / 2d
    else a = t.v < V_MAX ? ACCEL : 0.0f;
    a = std::max(a, -4.0f * BRAKE);                                    // emergency brake limit
    const float before = t.x;
    t.v = std::max(0.0f, std::min(V_MAX, t.v + a * dt));
    t.x += t.v * dt;
    if (before <= target && t.x > target) { t.x = target; t.v = 0.0f; }
}

void updateTrains(float dt)
{
    // spawn the next service when it is due (leaving space on the approach line)
    for (Service& s : svcV)
    {
        if (s.spawned || s.st != SvcState::Scheduled) continue;
        if (simT < s.estArr - approachTime()) break;
        bool spaceFree = true;
        for (const Train& t : trainsV) if (rearOf(t) < SPAWN_X + 0.6f) spaceFree = false;
        if (!spaceFree) break;
        Train t; t.svc = s.id; t.track = s.platform; t.x = SPAWN_X; t.v = V_MAX;
        trainsV.push_back(t);
        s.spawned = true; s.st = SvcState::Approaching;
        break;
    }

    Train* lead = nextAtHome();
    std::vector<int> gone;
    for (Train& t : trainsV)
    {
        Service* s = svcById(t.svc);
        if (!s) { gone.push_back(t.svc); continue; }
        t.t += dt;
        switch (t.st)
        {
        case Train::Approach:
        {
            float target = STOP_X;
            if (!t.routed)
            {
                // APPROACH LOCKING: once the leading train is within braking distance of a clear
                // home signal its route is committed (points set + locked, platform fixed).
                const float stopAt = t.x + t.v * t.v / (2.0f * BRAKE) + 0.06f;
                if (&t == lead && routeClearFor(t) && stopAt >= HOME_X - 0.03f)
                {
                    t.routed = true; t.track = s->platform; s->locked = true;
                }
                else target = HOME_X - 0.03f;                                    // home signal at danger
            }
            for (const Train& o : trainsV)                                        // never run into the train ahead
                if (&o != &t && o.x > t.x && (!o.routed || o.track == t.track || !t.routed)) target = std::min(target, rearOf(o) - 0.12f);
            drive(t, target, dt);
            if (t.routed && t.x >= STOP_X - 0.001f && t.v == 0.0f)
            {
                t.st = Train::Dwell; t.t = 0; t.toAlight = std::min(s->alight, s->load);
                s->st = SvcState::AtPlatform; s->actualArr = simT;
                st.arrived++;
                const float late = simT - s->planArr;
                if (late > 3.0f) { st.delayed++; st.totalDelay += late; } else st.onTime++;
                say(late > 3.0f ? 2 : 0, "%s TO %s ARRIVED AT PLATFORM %d%s", s->name, DEST[s->dest], t.track + 1, late > 3.0f ? " (LATE)" : "");
                for (float& b : lastBoard[t.track]) b = -10.0f;
            }
            break;
        }
        case Train::Dwell:
        {
            // the starter signal clears -> doors close (latched, so the train cannot get stuck half-closed)
            if (!t.closing && readyToLeave(t) && !starterHold[t.track] && exitClear(t)) t.closing = true;
            if (t.closing && starterHold[t.track]) t.closing = false;          // held while closing: re-open
            const bool leaving = t.closing;
            t.doors = leaving ? std::max(0.0f, t.doors - dt * 1.4f) : std::min(1.0f, t.doors + dt * 1.4f);
            if (t.doors >= 0.95f && t.toAlight > 0)
            {
                t.alightT -= dt;
                if (t.alightT <= 0.0f)
                {
                    t.alightT = 0.35f;
                    const int door = (int)(rnd() * DOORS) % DOORS;
                    float dx, dy; doorPos(t.track, door, dx, dy);
                    Passenger p = makePax(dx, dy - 0.035f);
                    p.ticket = -1; p.st = Passenger::Walk;
                    routeFrom(p, nearestPlatformNode(t.track, dx), false);
                    paxV.push_back(p);
                    t.toAlight--; s->load--; st.alighted++;
                }
            }
            if (leaving && t.doors <= 0.0f)
            {
                t.st = Train::Depart; t.t = 0;
                s->st = SvcState::Departed; s->depTime = simT;
                st.departed++;
                say(3, "%s DEPARTED FROM PLATFORM %d WITH %d ON BOARD", s->name, t.track + 1, s->load);
            }
            break;
        }
        case Train::Depart:
            drive(t, GONE_X + 10.0f, dt);
            if (rearOf(t) > GONE_X - 1.0f) gone.push_back(t.svc);
            break;
        }
    }
    trainsV.erase(std::remove_if(trainsV.begin(), trainsV.end(), [&](const Train& t) { return std::find(gone.begin(), gone.end(), t.svc) != gone.end(); }), trainsV.end());

    // keep the timetable topped up, forget old services nobody refers to
    int upcoming = 0;
    for (const Service& s : svcV) if (s.st != SvcState::Departed) upcoming++;
    while (upcoming < 6) { addService(); upcoming++; }
    svcV.erase(std::remove_if(svcV.begin(), svcV.end(), [](const Service& s) {
        if (s.st != SvcState::Departed || simT - s.depTime < 30.0f) return false;
        for (const Passenger& p : paxV) if (p.ticket == s.id) return false;
        for (const Train& t : trainsV) if (t.svc == s.id) return false;
        return true; }), svcV.end());
    for (Service& s : svcV) s.flash = std::max(0.0f, s.flash - dt);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
Passenger* pickPax(float x, float y)
{
    Passenger* best = nullptr;
    for (Passenger& p : paxV)
    {
        if (p.st == Passenger::Board || p.alpha < 0.6f) continue;
        if (std::fabs(x - p.x) < 0.04f && y > p.y - 0.01f && y < p.y + 0.22f)
            if (!best || p.y < best->y) best = &p;
    }
    return best;
}

bool swapPoints()
{
    if (pointsLocked())
    {
        st.interlockRefusals++;
        popup(PTS_X0 + 0.2f, TRACK_Y[1] + 0.05f, 1, "INTERLOCKED: TRAIN ON POINTS");
        say(1, "INTERLOCKING REFUSED: THE POINTS ARE LOCKED WHILE A TRAIN IS ON THEM");
        return false;
    }
    Service* target = nullptr;
    if (Train* t = nextAtHome()) target = svcById(t->svc);
    if (!target) for (Service& s : svcV) if (s.st == SvcState::Scheduled) { target = &s; break; }
    if (!target) return false;
    target->platform = 1 - target->platform; target->forced = true; target->flash = 3.0f;
    st.platformChanges++;
    popup(PTS_X0 + 0.2f, TRACK_Y[1] + 0.05f, 2, "POINTS: %s -> PLATFORM %d", target->name, target->platform + 1);
    say(2, "YOU SWITCHED THE POINTS: %s WILL USE PLATFORM %d", target->name, target->platform + 1);
    rerouteAll(true, target->id);
    replan();
    return true;
}

void dropPax(Passenger& p, float x, float y)
{
    int plat = -1;
    if (std::fabs(y - P1_Y) < 0.13f && x > P1_X0 + 0.05f && x < P1_X1 + 0.25f) plat = 0;
    else if (std::fabs(y - P2_Y) < 0.10f && x > P2_X0 && x < P2_X1 + 0.25f) plat = 1;
    st.drags++;
    if (plat < 0)
    {
        p.x = p.fromX; p.y = p.fromY;
        popup(x, y + 0.08f, 1, "NOT ON THE TRACKS!");
        say(1, "PASSENGER #%d CANNOT STAND THERE - PUT BACK", p.id);
    }
    else { p.x = std::max(P1_X0 + 0.05f, std::min(XING_X, x)); p.y = plat == 0 ? P1_Y : P2_Y; }
    p.st = Passenger::Walk;
    const int start = nearestPlatformNode(platformOfY(p.y), p.x);
    routeFrom(p, start, true);
    if (plat >= 0)
    {
        if (p.routeCost >= INF) popup(p.x, p.y + 0.26f, 1, "NO WAY THROUGH - WAITING");
        else popup(p.x, p.y + 0.26f, 0, "NEW ROUTE %s %.0fS", p.viaBridge ? "VIA BRIDGE" : p.viaCrossing ? "VIA CROSSING" : "ON PLATFORM", p.routeCost);
    }
}

void handleInput(const Input& in)
{
    if (dragId >= 0)
    {
        Passenger* p = paxById(dragId);
        if (!p) { dragId = -1; return; }
        if (in.valid) { p->x = std::max(-1.74f, std::min(1.74f, in.x + dragDX)); p->y = std::max(-0.84f, std::min(0.60f, in.y + dragDY)); }
        if (in.released || !in.down) { dragId = -1; dropPax(*p, p->x, p->y); }
        return;
    }
    if (pendId >= 0)                                        // drag threshold: a plain click does not move anyone
    {
        Passenger* p = paxById(pendId);
        if (!p || !in.down || in.released || p->st == Passenger::Board) { pendId = -1; return; }
        if (std::hypot(in.x - pendX, in.y - pendY) < 0.025f) return;
        pendId = -1;
        p->fromX = p->x; p->fromY = p->y;
        p->st = Passenger::Dragged;
        dragId = p->id; dragDX = p->x - pendX; dragDY = p->y - pendY;
        return;
    }
    if (!in.valid || !in.pressed) return;
    if (Passenger* p = pickPax(in.x, in.y)) { pendId = p->id; pendX = in.x; pendY = in.y; return; }
    switch (hoverThing())
    {
    case 1: testToggleHome(); return;
    case 2: testToggleStarter(0); return;
    case 3: testToggleStarter(1); return;
    case 4: swapPoints(); return;
    case 5: testToggleBridge(); return;
    case 6:
    {
        int n = 0;
        for (int i = 0; i < 5 && (int)paxV.size() < MAX_PASSENGERS + 10; i++) if (spawnTraveller(pickTicket()) >= 0) n++;
        popup(ENTRY_X, P1_Y + 0.30f, 2, "RUSH HOUR: +%d PASSENGERS", n);
        return;
    }
    default: break;
    }
    const int sv = hoverTrainSvc();
    if (sv >= 0)
    {
        Train* t = trainOfSvc(sv);
        if (t && t->st == Train::Dwell)
        {
            t->departNow = true;
            popup(t->x - TRAIN_L / 2, trackY(t->track, t->x) + CAR_H + 0.06f, 2, starterHold[t->track] ? "DISPATCH: STARTER HELD!" : "DISPATCH NOW");
        }
    }
}

void initialScene()
{
    // a lively first frame: one train already standing at platform 1, people waiting
    Service& s0 = svcV[0];
    s0.platform = 0; s0.locked = true; s0.spawned = true; s0.st = SvcState::AtPlatform; s0.actualArr = simT; s0.planArr = s0.estArr = simT;
    Train t; t.svc = s0.id; t.track = 0; t.routed = true; t.x = STOP_X; t.v = 0; t.st = Train::Dwell; t.toAlight = 2; t.doors = 0.3f;
    trainsV.push_back(t);
    st.arrived++; st.onTime++;
    for (int i = 0; i < 9; i++)
    {
        const int ticket = svcV[(size_t)(i % 3)].id;
        const int id = spawnTraveller(ticket);
        Passenger* p = paxById(id);
        if (!p) continue;
        const int plat = ticketPlatform(*p);
        if (i < 6)                                    // already waiting at the doors
        {
            float dx, dy; doorPos(plat, p->door, dx, dy);
            p->x = dx + rnd(-0.05f, 0.05f); p->y = dy; p->st = Passenger::ToDoor;
        }
        else { p->x = NODE_X[N_GATE] + 0.2f * i - 1.2f; p->x = std::max(-1.4f, p->x); routeFrom(*p, N_P1W, false); }
    }
}

}   // namespace

// ---------------------------------------------------------------------------
// public
// ---------------------------------------------------------------------------
void init(unsigned seed) { seed0 = seed; reset(); }

void reset()
{
    rng.seed(seed0);
    simT = 0; svcV.clear(); trainsV.clear(); paxV.clear(); pops.clear(); logV.clear(); st = Stats();
    nextSvc = 1; nextPax = 1; lastPlan = 0; replanT = 0;
    arrivalsOn = true; homeHold = false; starterHold[0] = starterHold[1] = false; bridgeClosed = false; lastCrossingOpen = true;
    dragId = -1; pendId = -1;
    for (auto& row : lastBoard) for (float& b : row) b = -10.0f;
    for (int i = 0; i < 6; i++) addService();
    nextArrival = 1.0f;
    initialScene();
    replan();
    say(3, "%s - CLICK SIGNALS, POINTS, BRIDGE OR A TRAIN - DRAG PASSENGERS", NAME);
}

void update(double dtIn, const Input& in)
{
    const float dt = (float)std::min(dtIn, 0.05);
    simT += dt;
    lastIn = in;
    handleInput(in);

    // the crossing gate is part of the graph: when it changes, everyone re-plans (Dijkstra)
    const bool xo = crossingClear();
    if (xo != lastCrossingOpen)
    {
        lastCrossingOpen = xo;
        rerouteAll();
    }

    replanT -= dt;
    if (replanT <= 0.0f) { replanT = 0.5f; replan(); }

    if (arrivalsOn && simT >= nextArrival)                // Poisson arrivals: exponential gaps
    {
        nextArrival = simT - ARRIVAL_MEAN * std::log(1.0f - std::min(0.999f, rnd()));
        if ((int)paxV.size() < MAX_PASSENGERS) spawnTraveller(pickTicket());
    }

    updateTrains(dt);
    updatePassengers(dt);
    for (Popup& p : pops) p.age += dt;
    pops.erase(std::remove_if(pops.begin(), pops.end(), [](const Popup& p) { return p.age > 2.6f; }), pops.end());
}

float now() { return simT; }
const std::vector<Service>&   services()   { return svcV; }
const std::vector<Train>&     trains()     { return trainsV; }
const std::vector<Passenger>& passengers() { return paxV; }
const std::vector<Popup>&     popups()     { return pops; }
const std::vector<LogLine>&   log()        { return logV; }
const Stats&                  stats()      { return st; }
const Service*   findService(int id)   { return svcById(id); }
const Passenger* findPassenger(int id) { return paxById(id); }
const char* destName(int d) { return DEST[((d % 5) + 5) % 5]; }
bool dragging() { return dragId >= 0; }
bool homeHeld() { return homeHold; }
bool starterHeld(int p) { return starterHold[p ? 1 : 0]; }
bool bridgeOpen() { return !bridgeClosed; }
bool crossingOpen() { return lastCrossingOpen; }
std::vector<int> shortestPath(int from, int to, float* cost) { return dijkstra(from, to, cost); }

const Train* trainOn(int platform)
{
    const Train* best = nullptr;
    for (const Train& t : trainsV)
    {
        const Service* s = svcById(t.svc);
        const int p = t.routed ? t.track : (s ? s->platform : -1);
        if (p != platform || t.st == Train::Depart) continue;
        if (!best || t.x > best->x) best = &t;
    }
    return best;
}

float trackY(int track, float x)
{
    if (track != 0 || x >= PTS_X1) return TRACK_Y[track != 0 ? 1 : 0];
    if (x <= PTS_X0) return TRACK_Y[1];
    const float u = (x - PTS_X0) / (PTS_X1 - PTS_X0), s = u * u * (3.0f - 2.0f * u);
    return TRACK_Y[1] + (TRACK_Y[0] - TRACK_Y[1]) * s;
}

void doorPos(int platform, int door, float& x, float& y)
{
    const int c = std::max(0, std::min(DOORS - 1, door)) / 2;
    const float carMid = STOP_X - CAR_L * 0.5f - c * (CAR_L + CAR_GAP);
    x = carMid + (door % 2 ? 0.11f : -0.11f);
    y = platform == 0 ? P1_Y : P2_Y;
}

Aspect homeAspect()
{
    const Train* t = nullptr;
    for (const Train& o : trainsV) if (!o.routed && (!t || o.x > t->x)) t = &o;
    if (homeHold || !t) return Aspect::Red;
    return routeClearFor(*t) ? Aspect::Yellow : Aspect::Red;
}

Aspect starterAspect(int platform)
{
    if (starterHold[platform ? 1 : 0]) return Aspect::Red;
    for (const Train& t : trainsV)
        if (t.routed && t.track == platform && t.st == Train::Dwell && readyToLeave(t) && exitClear(t)) return Aspect::Green;
    return Aspect::Red;
}

int pointsTrack()
{
    for (const Train& t : trainsV) if (t.x >= PTS_X0 - 0.02f && rearOf(t) <= PTS_X1 + 0.02f && t.routed) return t.track;
    if (const Train* t = [] { const Train* b = nullptr; for (const Train& o : trainsV) if (!o.routed && (!b || o.x > b->x)) b = &o; return b; }())
        if (const Service* s = svcById(t->svc)) return s->platform;
    for (const Service& s : svcV) if (s.st == SvcState::Scheduled) return s.platform;
    return 1;
}

bool pointsLocked() { return pointsOccupied(-1); }

int waitingOn(int platform)
{
    int n = 0;
    for (const Passenger& p : paxV)
        if (p.ticket >= 0 && ticketPlatform(p) == platform && p.st != Passenger::Board) n++;
    return n;
}

int doorLoad(int platform, int door) { return countDoor(platform, door, -1); }

float edgeCost(int a, int b)
{
    const Edge* e = findEdge(a, b);
    return e ? costOf(*e) : INF;
}

int hoverPassengerId()
{
    if (!lastIn.valid || dragId >= 0) return -1;
    Passenger* p = pickPax(lastIn.x, lastIn.y);
    return p ? p->id : -1;
}

int hoverTrainSvc()
{
    if (!lastIn.valid) return -1;
    for (const Train& t : trainsV)
    {
        if (lastIn.x < rearOf(t) || lastIn.x > t.x) continue;
        const float y0 = trackY(t.track, lastIn.x);
        if (lastIn.y >= y0 && lastIn.y <= y0 + CAR_H) return t.svc;
    }
    return -1;
}

int hoverThing()
{
    if (!lastIn.valid) return 0;
    const float x = lastIn.x, y = lastIn.y;
    auto close = [&](float cx, float cy, float r) { return std::hypot(x - cx, y - cy) < r; };
    if (close(HOME_X, TRACK_Y[1] + 0.29f, 0.07f)) return 1;
    if (close(STARTER_X + 0.04f, TRACK_Y[0] + 0.29f, 0.07f)) return 2;
    if (close(STARTER_X + 0.04f, TRACK_Y[1] + 0.29f, 0.07f)) return 3;
    if (close(PTS_X0 + 0.10f, TRACK_Y[1] - 0.10f, 0.08f)) return 4;
    if (x > BR1_X - 0.07f && x < BR2_X + 0.07f && y > BR2_Y && y < BR1_Y + 0.08f) return 5;
    if (std::fabs(x - ENTRY_X) < 0.08f && y > P1_Y && y < P1_Y + 0.26f) return 6;
    if (close(XING_X, TRACK_Y[0] + 0.03f, 0.08f)) return 7;
    return 0;
}

void testSetArrivals(bool on) { arrivalsOn = on; }
int  testSpawnPassenger(int ticketSvc) { return spawnTraveller(ticketSvc); }
void testToggleBridge()
{
    bridgeClosed = !bridgeClosed;
    popup(BR1_X, BR1_Y + 0.10f, bridgeClosed ? 1 : 0, bridgeClosed ? "BRIDGE CLOSED" : "BRIDGE OPEN");
    say(bridgeClosed ? 1 : 0, bridgeClosed ? "FOOTBRIDGE CLOSED FOR MAINTENANCE - DIJKSTRA RE-ROUTES EVERYONE" : "FOOTBRIDGE OPEN AGAIN");
    rerouteAll();
}
void testToggleHome()
{
    homeHold = !homeHold;
    popup(HOME_X + 0.25f, TRACK_Y[1] + 0.42f, homeHold ? 1 : 0, homeHold ? "HOME SIGNAL HELD AT RED" : "HOME SIGNAL RELEASED");
    say(homeHold ? 1 : 0, homeHold ? "YOU HOLD ARRIVING TRAINS AT THE HOME SIGNAL" : "HOME SIGNAL RELEASED");
    replan();
}
void testToggleStarter(int p)
{
    p = p ? 1 : 0;
    starterHold[p] = !starterHold[p];
    popup(STARTER_X - 0.2f, TRACK_Y[p] + 0.45f, starterHold[p] ? 1 : 0, starterHold[p] ? "PLATFORM %d HELD" : "PLATFORM %d RELEASED", p + 1);
    say(starterHold[p] ? 1 : 0, starterHold[p] ? "STARTER SIGNAL P%d HELD AT RED - THE TRAIN WAITS" : "STARTER SIGNAL P%d RELEASED", p + 1);
    replan();
}
bool testSwapPoints() { return swapPoints(); }
void testDropPassenger(int id, float x, float y)
{
    Passenger* p = paxById(id);
    if (!p) return;
    p->fromX = p->x; p->fromY = p->y;
    dropPax(*p, x, y);
}

}   // namespace station
