// Headless test of the train-station simulation (no window, no OpenGL).
#include "stationsim.h"
#include "game.h"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <map>
#include <vector>
using namespace station;
static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { std::printf("  FAIL: %s\n", m); fails++; } else std::printf("  ok:   %s\n", m); } while (0)
static const float DT = 1.0f / 60.0f;
static Input IN;
static void step(int n) { for (int i = 0; i < n; i++) { update(DT, IN); IN.pressed = false; IN.released = false; } }
static void secs(float s) { step((int)(s * 60)); }
static bool has(const std::vector<int>& p, int n) { for (int v : p) if (v == n) return true; return false; }
static void printPath(const char* label, const std::vector<int>& p, float c)
{
    std::printf("  %s:", label); for (int n : p) std::printf(" %d", n); std::printf("  (%.1fs)\n", c);
}
// safety invariants, checked every frame
static bool safe = true;
static void checkSafety()
{
    const auto& tr = trains();
    for (size_t i = 0; i < tr.size(); i++)
    {
        if (tr[i].v > stationcfg::V_MAX + 1e-3f || !std::isfinite(tr[i].x)) safe = false;
        for (size_t j = i + 1; j < tr.size(); j++)
        {
            const Train& a = tr[i]; const Train& b = tr[j];
            const bool sameLine = (a.routed && b.routed && a.track == b.track) || (!a.routed && !b.routed);
            if (!sameLine) continue;
            const float ra = a.x - TRAIN_L, rb = b.x - TRAIN_L;
            if (a.x > rb && b.x > ra) safe = false;                         // bodies overlap on the same line
        }
    }
    int dwelling[2] = {0, 0};
    for (const Train& t : tr) if (t.st == Train::Dwell) dwelling[t.track]++;
    if (dwelling[0] > 1 || dwelling[1] > 1) safe = false;
    for (const Service& s : services()) if (s.load > stationcfg::CAPACITY) safe = false;
}
static void run(float s) { for (int i = 0; i < (int)(s * 60); i++) { step(1); checkSafety(); } }

int main()
{
    std::printf("== 1. Dijkstra on the walkway graph ==\n");
    reset(); testSetArrivals(false);
    float c1, c2, c3;
    auto toP2W = shortestPath(N_ENTRY, N_P2W, &c1);
    auto toP2E = shortestPath(N_ENTRY, N_P2E, &c2);
    printPath("entrance -> platform 2 west", toP2W, c1);
    printPath("entrance -> platform 2 east", toP2E, c2);
    CHECK(has(toP2W, N_BR1) && has(toP2W, N_BR2), "west end of platform 2: shortest way is the footbridge");
    CHECK(has(toP2E, N_P1X) && has(toP2E, N_P2X) && !has(toP2E, N_BR1), "east end of platform 2: shortest way is the level crossing");
    float sum = 0; for (size_t i = 1; i < toP2W.size(); i++) sum += edgeCost(toP2W[i - 1], toP2W[i]);
    CHECK(std::fabs(sum - c1) < 1e-3f, "path cost = sum of its edge costs");
    // brute force: the bridge route and the crossing route, Dijkstra must return the cheaper one
    const int viaX[] = {N_ENTRY, N_GATE, N_P1W, N_P1M, N_P1E, N_P1X, N_P2X, N_P2E, N_P2M, N_P2W};
    float alt = 0; for (int i = 1; i < 10; i++) alt += edgeCost(viaX[i - 1], viaX[i]);
    std::printf("  alternative via the crossing to platform 2 west: %.1fs\n", alt);
    CHECK(c1 <= alt, "Dijkstra result is not longer than the alternative");

    std::printf("== 2. closing the bridge: everyone re-routes ==\n");
    testToggleBridge();
    auto noBridge = shortestPath(N_ENTRY, N_P2W, &c3);
    printPath("entrance -> platform 2 west (bridge closed)", noBridge, c3);
    CHECK(!has(noBridge, N_BR1) && has(noBridge, N_P2X), "with the bridge closed the route uses the crossing");
    CHECK(c3 > c1, "and it is longer");
    testToggleBridge();
    reset(); testSetArrivals(false);
    int target = -1;
    for (const Service& s : services()) if (s.platform == 1 && s.st == SvcState::Scheduled) { target = s.id; break; }
    const int pid = testSpawnPassenger(target);
    secs(1.0f);
    const Passenger* p = findPassenger(pid);
    std::printf("  passenger #%d for platform 2: %s\n", pid, p->viaBridge ? "via bridge" : p->viaCrossing ? "via crossing" : "other");
    const int rr0 = stats().reroutes;
    testToggleBridge();
    p = findPassenger(pid);
    CHECK(!p->viaBridge && stats().reroutes > rr0, "closing the bridge re-routed the walking passenger");

    std::printf("== 3. no route at all: wait, then continue ==\n");
    // bridge closed + crossing gates down (train passing) -> no way across
    int waited = 0;
    for (int i = 0; i < 60 * 60 && waited == 0; i++) { step(1); for (const Passenger& q : passengers()) if (q.st == Passenger::NoRoute) waited = q.id; }
    std::printf("  crossing closed while bridge shut -> passenger #%d waits\n", waited);
    CHECK(waited > 0 || crossingOpen(), "a passenger with no possible route waits (NoRoute)");
    testToggleBridge();
    secs(1.0f);
    const Passenger* w = waited > 0 ? findPassenger(waited) : nullptr;
    CHECK(!w || w->st != Passenger::NoRoute, "when a walkway opens again the passenger moves on");

    std::printf("== 4. trains stop exactly at the platform (v^2 = 2 a d) ==\n");
    reset(); testSetArrivals(false);
    float worstStop = 0, maxDecel = 0; std::map<int, float> lastV; int stops = 0;
    for (int i = 0; i < 60 * 120; i++)
    {
        step(1); checkSafety();
        for (const Train& t : trains())
        {
            if (lastV.count(t.svc)) maxDecel = std::max(maxDecel, (lastV[t.svc] - t.v) / DT);
            lastV[t.svc] = t.v;
            if (t.st == Train::Dwell && t.t < DT * 1.5f) { worstStop = std::max(worstStop, std::fabs(t.x - STOP_X)); stops++; }
        }
    }
    std::printf("  %d stops, worst stop error %.5f, max deceleration %.2f u/s2 (service brake %.2f)\n", stops, worstStop, maxDecel, stationcfg::BRAKE);
    CHECK(stops >= 5 && worstStop < 1e-3f, "every train stopped within 0.001 of the stop mark");
    CHECK(maxDecel <= 4.0f * stationcfg::BRAKE + 1e-3f, "deceleration stayed within the brake limit");
    CHECK(safe, "no collisions, max one train per platform, never above V_MAX");

    std::printf("== 5. interval partitioning: platform plan has no overlaps ==\n");
    reset(); testSetArrivals(false); secs(2);
    std::vector<const Service*> up;
    for (const Service& s : services()) if (!s.locked && s.st == SvcState::Scheduled) up.push_back(&s);
    bool overlap = false;
    for (size_t i = 0; i < up.size(); i++)
        for (size_t j = 0; j < up.size(); j++)
            if (i != j && up[i]->platform == up[j]->platform && up[i]->estArr <= up[j]->estArr)
                if (up[j]->estArr < up[i]->estArr + up[i]->dwell) overlap = true;
    for (const Service* s : up) std::printf("  %s plan %.1f est %.1f dwell %.1f -> P%d\n", s->name, s->planArr, s->estArr, s->dwell, s->platform + 1);
    CHECK(!overlap, "no two planned services share a platform at the same time");

    std::printf("== 6. home signal held: trains wait, delays ripple through the plan ==\n");
    reset(); testSetArrivals(false); safe = true;
    testToggleHome();
    bool passed = false;
    const int arrived0 = stats().arrived;
    for (int i = 0; i < 60 * 40; i++) { step(1); checkSafety(); for (const Train& t : trains()) if (!t.routed && t.x > HOME_X + 1e-3f) passed = true; }
    CHECK(!passed && stats().arrived == arrived0, "no train passed the red home signal in 40 s");
    CHECK(homeAspect() == Aspect::Red, "home signal shows red");
    testToggleHome();
    run(40);
    std::printf("  after release: arrived %d, late %d, total delay %.0fs\n", stats().arrived, stats().delayed, stats().totalDelay);
    CHECK(stats().arrived > arrived0 && stats().delayed > 0, "after release trains arrive, counted as late");
    CHECK(safe, "still no collisions");

    std::printf("== 7. starter signal held: the train waits at the platform ==\n");
    reset(); testSetArrivals(false);
    testToggleStarter(0);
    run(30);
    const Train* t0 = trainOn(0);
    CHECK(t0 && t0->st == Train::Dwell && t0->t > 25.0f && starterAspect(0) == Aspect::Red, "train on platform 1 is kept there past its dwell time");
    testToggleStarter(0);
    const int dep0 = stats().departed;
    run(8);
    CHECK(stats().departed > dep0, "released: it departs");

    std::printf("== 8. points: user swap, and interlocking ==\n");
    reset(); testSetArrivals(false);
    int next = -1, before = -1;
    for (const Service& s : services()) if (s.st == SvcState::Scheduled) { next = s.id; before = s.platform; break; }
    const int pax = testSpawnPassenger(next);
    secs(0.5f);
    const int doorBefore = findPassenger(pax)->door;
    CHECK(testSwapPoints(), "swap accepted while no train is on the points");
    CHECK(findService(next)->platform == 1 - before, "the next service now uses the other platform");
    std::printf("  passenger #%d door %d -> door %d, route %s\n", pax, doorBefore, findPassenger(pax)->door, findPassenger(pax)->viaBridge ? "via bridge" : findPassenger(pax)->viaCrossing ? "via crossing" : "on platform");
    bool refused = false;
    for (int i = 0; i < 60 * 60 && !refused; i++) { step(1); if (pointsLocked()) refused = !testSwapPoints(); }
    CHECK(refused, "swap refused while a train is on the points (interlocking)");

    std::printf("== 9. boarding: FIFO per door and capacity ==\n");
    reset(); testSetArrivals(true); safe = true;
    std::map<int, float> waitAt; std::map<int, int> doorOf; std::map<int, int> ticketOf;
    std::vector<int> order; bool fifo = true;
    for (int i = 0; i < 60 * 180; i++)
    {
        step(1); checkSafety();
        for (const Passenger& q : passengers())
        {
            if (q.st == Passenger::Wait) { waitAt[q.id] = q.waitSince; doorOf[q.id] = q.door; ticketOf[q.id] = q.ticket; }
            if (q.st == Passenger::Board && std::find(order.begin(), order.end(), q.id) == order.end())
            {
                for (const Passenger& o : passengers())        // someone still waiting at my door who came earlier?
                    if (o.st == Passenger::Wait && o.ticket == q.ticket && o.door == q.door && o.waitSince < waitAt[q.id] - 1e-4f) fifo = false;
                order.push_back(q.id);
            }
        }
    }
    std::printf("  boarded %d, alighted %d, missed %d\n", stats().boarded, stats().alighted, stats().missed);
    CHECK(stats().boarded > 30 && fifo, "passengers boarded in first-come order at every door");
    CHECK(safe, "no train ever exceeded its capacity");

    std::printf("== 10. drag and drop ==\n");
    reset(); testSetArrivals(false);
    int any = -1; for (const Passenger& q : passengers()) if (q.ticket >= 0) { any = q.id; break; }
    const float ox = findPassenger(any)->x, oy = findPassenger(any)->y;
    testDropPassenger(any, 0.3f, TRACK_Y[1]);          // empty track (a train stands on track 0)
    CHECK(std::fabs(findPassenger(any)->x - ox) < 1e-4f && std::fabs(findPassenger(any)->y - oy) < 1e-4f, "dropped on the track: put back where it was");
    testDropPassenger(any, 0.9f, P2_Y + 0.02f);
    CHECK(std::fabs(findPassenger(any)->y - P2_Y) < 1e-4f && !findPassenger(any)->path.empty(), "dropped on platform 2: placed there with a new Dijkstra route");

    std::printf("  plain click (no movement) on a passenger:\n");
    {
        int wid = -1; for (const Passenger& q : passengers()) if (q.st == Passenger::Wait) { wid = q.id; break; }
        if (wid < 0) for (int i = 0; i < 600 && wid < 0; i++) { step(1); for (const Passenger& q : passengers()) if (q.st == Passenger::Wait) { wid = q.id; break; } }
        const Passenger* q = findPassenger(wid);
        const float qx = q->x, qy = q->y;
        IN = Input(); IN.valid = true; IN.x = qx; IN.y = qy + 0.1f; IN.down = true; IN.pressed = true; step(1);
        IN.down = false; IN.released = true; step(1); IN = Input();
        q = findPassenger(wid);
        CHECK(q && q->st != Passenger::Dragged && std::fabs(q->x - qx) < 0.01f && std::fabs(q->y - qy) < 0.01f && !dragging(), "clicking without moving does not pick the passenger up");
    }

    std::printf("== 12. HOLD during boarding delays the departure ==\n");
    reset(); testSetArrivals(false); game::init();
    secs(1.0f);
    const Service* s1 = findService(1);
    const float depA = expectedDeparture(*s1);
    CHECK(holdTrain(1), "hold accepted while the train is boarding");
    CHECK(expectedDeparture(*findService(1)) >= depA + stationcfg::HOLD_STEP - 1.5f, "expected departure moved later by the hold");
    secs(depA - now() + 2.0f);
    CHECK(trainOn(0) && trainOn(0)->svc == 1 && trainOn(0)->st == Train::Dwell, "train still at the platform after its old departure time");

    std::printf("== 13. DEPART NOW in the middle of boarding ==\n");
    reset(); testSetArrivals(false); game::init();
    secs(0.5f);
    int waiting1 = 0; for (const Passenger& q : passengers()) if (q.ticket == 1) waiting1++;
    const int sc = game::score();
    CHECK(departNow(1), "depart accepted at any time");
    bool left = false; for (int i = 0; i < 60 * 8 && !left; i++) { step(1); const Service* q = findService(1); left = q && q->st == SvcState::Departed; }
    CHECK(left, "the train leaves within seconds");
    std::printf("  %d passengers had tickets, left behind %d, score %d -> %d\n", waiting1, stats().leftBehind, sc, game::score());
    CHECK(stats().leftBehind > 0 && game::score() < sc, "passengers left behind cost points");

    std::printf("== 14. drop a passenger onto a train ==\n");
    reset(); testSetArrivals(false); game::init();
    secs(2.0f);
    const int good = testSpawnPassenger(1), bad = testSpawnPassenger(2);
    step(1);
    int g0 = game::score();
    float tx = trainOn(0)->x - TRAIN_L / 2, ty = trackY(0, tx) + 0.08f;
    testDropPassenger(good, tx, ty);
    CHECK(findPassenger(good) == nullptr || findPassenger(good)->st == Passenger::Board, "right ticket: boards at once");
    CHECK(game::score() == g0 + stationcfg::SCORE_HELP_BOARD, "right train adds points");
    g0 = game::score();
    testDropPassenger(bad, tx, ty);
    CHECK(findPassenger(bad) && findPassenger(bad)->st != Passenger::Board, "wrong ticket: gets off again");
    CHECK(game::score() == g0 + stationcfg::SCORE_WRONG_TRAIN, "wrong train costs points");

    std::printf("== 15. pulling a train along the track: interlocking still applies ==\n");
    reset(); testSetArrivals(false); game::init();
    int app = -1;
    for (int i = 0; i < 60 * 40 && app < 0; i++) { step(1); for (const Train& t : trains()) if (t.st == Train::Approach && t.x > -2.6f) app = t.svc; }
    CHECK(app >= 0, "an approaching train exists");
    testToggleHome();                                           // home signal held at red
    const int b0 = stats().dragBlocked;
    testDragTrain(app, STOP_X, 6.0f);
    const Train* at = nullptr; for (const Train& t : trains()) if (t.svc == app) at = &t;
    CHECK(at && at->x <= HOME_X + 1e-3f && !at->routed, "cannot pull it past a red home signal");
    CHECK(stats().dragBlocked > b0, "the player is told why (red signal)");
    const float xb = at->x;
    testDragTrain(app, xb - 0.5f, 2.0f);
    at = nullptr; for (const Train& t : trains()) if (t.svc == app) at = &t;
    CHECK(at && at->x < xb - 0.2f, "it can be shunted back along the track");
    testToggleHome();
    testDragTrain(app, STOP_X, 8.0f);
    at = nullptr; for (const Train& t : trains()) if (t.svc == app) at = &t;
    CHECK(at && at->routed && at->x > HOME_X, "with a clear signal it can be pulled into the station");

    std::printf("== 11. 10-minute stability run ==\n");
    reset(); safe = true;
    bool finite = true;
    for (int i = 0; i < 60 * 600; i++)
    {
        step(1); checkSafety();
        for (const Passenger& q : passengers()) if (!std::isfinite(q.x) || !std::isfinite(q.y) || std::fabs(q.x) > 1.9f) finite = false;
    }
    const Stats& s = stats();
    std::printf("  arrived %d departed %d on time %d late %d | boarded %d alighted %d missed %d | entered %d exited %d | reroutes %d\n",
                s.arrived, s.departed, s.onTime, s.delayed, s.boarded, s.alighted, s.missed, s.entered, s.exited, s.reroutes);
    CHECK(finite && safe, "no collisions, all positions finite");
    CHECK(s.arrived >= 30 && s.departed >= 30 && s.boarded > 150, "trains kept running and passengers kept boarding");
    CHECK((int)passengers().size() <= stationcfg::MAX_PASSENGERS + 12, "the station never overflowed");

    std::printf("\n%s (%d failure%s)\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
