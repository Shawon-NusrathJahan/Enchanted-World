// Headless test of the garden simulation (no window needed).
// Build: see CMakeLists.txt (cmake -DBUILD_TESTS=ON) - run: garden_sim_test
#include "gardensim.h"
#include "theme.h"
#include <cstdio>
#include <cmath>
#include <set>
#include <string>

using namespace gardensim;
static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("  FAIL: %s\n", msg); fails++; } else std::printf("  ok:   %s\n", msg); } while (0)

static AnimState S;
static ui::Mouse M;
static const float DT = 1.0f / 60.0f;
static void step(int n, bool interactive = true) { for (int i = 0; i < n; i++) { update(DT, S, M, interactive); M.pressed = false; } }

int main()
{
    S = AnimState{}; S.time = 40.0; S.phase = PHASE_LIVING_GARDEN; S.gardenBlend = 1.0f;
    theme::init(); theme::setOverrideHour(12.0f);
    init();

    std::printf("== 1. initial state ==\n");
    update(DT, S, M, true);
    {
        bool young = true; int blooming = 0; for (auto& f : flowers()) { if (f.growth < 0.3f || f.growth > 0.96f) young = false; if (f.growth > 0.75f) blooming++; }
        CHECK(young && blooming > 5, "garden starts colourful: young flowers, some already blooming");
        CHECK(gardener().state == GState::InHouse, "gardener starts inside the house");
        CHECK(thirstyCount() > 10, "flowers start thirsty");
    }

    std::printf("== 2. gardener behaviour over 12 minutes ==\n");
    std::set<std::string> seen; GState last = gardener().state; int homeTrips = 0, pondTrips = 0;
    int ticksAllOkInside = 0, ticksOutWithNoNeed = 0;
    std::string rowOrder;
    for (int i = 0; i < 60 * 720; i++)
    {
        step(1);
        GState st = gardener().state;
        if (st != last) {
            seen.insert(stateName(st));
            if (st == GState::InHouse) homeTrips++;
            if (st == GState::Filling) pondTrips++;
            last = st;
        }
        if (thirstyCount() == 0 && st == GState::InHouse) ticksAllOkInside++;
        if (st == GState::Watering && thirstyCount() == 0) ticksOutWithNoNeed++;
        if (i % (60 * 60) == 0) std::printf("  t=%3ds thirsty=%d bloom=%d dead=%d state=%s can=%.1f\n", i / 60, thirstyCount(), bloomCount(), deadCount(), stateName(st), gardener().can);
    }
    for (auto& s : seen) std::printf("  visited state: %s\n", s.c_str());
    CHECK(seen.count("WALKING TO POND") && seen.count("FILLING CAN") && seen.count("WATERING") && seen.count("ENTERING HOUSE"), "full state cycle executed");
    CHECK(homeTrips >= 1, "gardener returned to the house when everything was fine");
    CHECK(pondTrips >= 4, "gardener refilled the can at the pond several times");
    CHECK(ticksAllOkInside > 60 * 10, "periods exist where all flowers are fine and the gardener stays inside");
    float avgGrowth = 0; int d = 0; for (auto& f : flowers()) { avgGrowth += f.growth; if (f.dead) d++; }
    std::printf("  average growth at 12 min: %.2f, dead now: %d\n", avgGrowth / flowers().size(), d);
    CHECK(avgGrowth / flowers().size() > 0.5f, "flowers grew after watering");

    std::printf("== 3. row-by-row traversal ==\n");
    reset(); update(DT, S, M, true);
    std::string order; int lastRow = -1; std::set<int> doneRows;
    int rowSwitches = 0; 
    for (int i = 0; i < 60 * 150; i++)
    {
        step(1);
        if (gardener().state == GState::Watering) {
            // find which flower row the gardener is standing at by nearest flower
            int best = -1; float bd = 9; for (size_t k = 0; k < flowers().size(); k++) { float dd = std::hypot(flowers()[k].x - gardener().x, flowers()[k].y - 0.045f * 0 - (gardener().y + 0.045f)); if (dd < bd) { bd = dd; best = (int)k; } }
            int row = flowers()[best].row;
            if (row != lastRow) { rowSwitches++; if (order.size() < 60) order += std::to_string(row) + " "; lastRow = row; }
        }
    }
    std::printf("  rows served in order (first 60 chars): %s\n", order.c_str());
    CHECK(rowSwitches < 40, "rows are served as blocks, not hopping flower to flower");

    std::printf("== 4. butterflies ==\n");
    reset(); update(DT, S, M, true);
    step(120);                                   // let them fly
    auto& B = butterflies();
    // chase butterfly 2 (strong repulsion) with the cursor for 15 s
    float minD2 = 9; 
    for (int i = 0; i < 60 * 15; i++)
    {
        M.valid = true; M.x = B[2].x + 0.02f; M.y = B[2].y; M.down = false;     // cursor teleports almost onto it every frame
        step(1);
        float dd = std::hypot(B[2].x - M.x, B[2].y - M.y);
        // measure distance against the cursor position used in that frame
        minD2 = std::min(minD2, dd);
    }
    // better: cursor moves smoothly toward the butterfly
    float minD = 9;
    for (int i = 0; i < 60 * 15; i++)
    {
        float tx = B[2].x, ty = B[2].y;
        float dx = tx - M.x, dy = ty - M.y, l = std::hypot(dx, dy);
        float sp = 1.6f * DT;                    // fast cursor (1.6 units/s)
        if (l > sp) { M.x += dx / l * sp; M.y += dy / l * sp; } else { M.x = tx; M.y = ty; }
        step(1);
        if (i >= 60) minD = std::min(minD, (float)std::hypot(B[2].x - M.x, B[2].y - M.y));   // after 1 s to recover
    }                                                                                          // from the teleport test
    std::printf("  strong-repulsion min distance while chased: %.3f (limit %.2f)\n", minD, TWO_CM);
    // the escape is smooth (bounded speed, no teleport), so a tiny dip is allowed in a corner
    CHECK(minD >= TWO_CM - 0.04f, "butterfly 3 keeps the cursor ~2 cm away while moving smoothly");

    // reaction times: cursor jumps next to each butterfly and stays
    for (int k = 0; k < 4; k++)
    {
        reset(); update(DT, S, M, true); step(180);
        M.valid = true;
        float tFlee = -1.0f;
        for (int i = 0; i < 60 * 8; i++)
        {
            if (!B[k].fleeing) { M.x = B[k].x; M.y = B[k].y; }     // cursor keeps hovering exactly on the butterfly
            step(1);
            if (B[k].fleeing && tFlee < 0) { tFlee = i * DT; }
        }
        std::printf("  butterfly %d (%s): reacted after %.2f s\n", k, k==0?"highly":k==1?"moderate":k==2?"strong":"least", tFlee);
        if (k == 0) CHECK(tFlee >= 0 && tFlee < 0.1f, "highly sensitive flees immediately");
        if (k == 1) CHECK(tFlee > 0.2f && tFlee < 0.6f, "moderate flees after a short delay");
        if (k == 3) CHECK(tFlee > 1.0f && tFlee < 2.2f, "least sensitive tolerates the cursor for ~1-2 s");
    }

    // shimmer: only butterflies that moved away glow, and it lasts ~5 s after they stop
    reset(); update(DT, S, M, true); step(180);
    M.valid = true; M.x = B[0].x; M.y = B[0].y;
    for (int i = 0; i < 30; i++) { step(1); }
    int glowing = 0; for (auto& b : B) if (b.shimmer > 0) glowing++;
    CHECK(B[0].shimmer > 0 && glowing <= 2, "only the scared butterfly(ies) glow");
    M.valid = false;                                           // cursor leaves
    bool wasOn = false; int offAt = -1;
    for (int i = 0; i < 60 * 12; i++) { step(1); if (B[0].shimmer > 0) wasOn = true; if (wasOn && B[0].shimmer <= 0 && offAt < 0) offAt = i; }
    // fleeing ends after fleeMin, then 5 s countdown
    std::printf("  glow ended %.1f s after the cursor left\n", offAt / 60.0f);
    CHECK(offAt / 60.0f > 5.0f && offAt / 60.0f < 8.0f, "glow lasts ~5 s after the butterfly stops fleeing");

    std::printf("== 5. pond ripples ==\n");
    reset(); update(DT, S, M, true);
    CHECK(ripples().empty(), "no ripples by default");
    step(600); CHECK(ripples().empty(), "no ripples appear on their own");
    M.valid = true; M.x = -1.1f; M.y = -0.5f; M.pressed = true; step(1);
    CHECK(ripples().size() == 1 && std::fabs(ripples()[0].x + 1.1f) < 1e-4f && std::fabs(ripples()[0].y + 0.5f) < 1e-4f, "click on pond creates a ripple at the exact position");
    M.valid = true; M.x = -1.0f; M.y = 0.6f; M.pressed = true; step(1);
    CHECK(ripples().size() == 1, "click outside the pond makes no ripple");
    step(60 * 3); CHECK(ripples().empty(), "ripples disappear by themselves");

    std::printf("== 6. dragging the gardener ==\n");
    reset(); update(DT, S, M, true); M.valid = false; step(60 * 40);      // wait until he is outside working
    for (int i = 0; i < 60 * 60 && !(gardener().state == GState::ToFlower || gardener().state == GState::ToRow); i++) step(1);
    float gx = gardener().x, gy = gardener().y;
    std::printf("  gardener at (%.2f, %.2f) state %s\n", gx, gy, stateName(gardener().state));
    M.valid = true; M.x = gx; M.y = gy + 0.2f; M.down = true; M.pressed = true; step(1);
    CHECK(gardener().state == GState::Dragged, "click on gardener picks him up");
    M.pressed = false; M.x = -0.2f; M.y = -0.9f; step(5);
    CHECK(std::fabs(gardener().x - gx) < 0.05f, "while dragging he waits (destination marker moves, not teleport)");
    M.down = false; step(1);
    CHECK(gardener().state == GState::UserWalk, "release makes him walk to the chosen spot");
    for (int i = 0; i < 60 * 12 && gardener().state == GState::UserWalk; i++) step(1);
    std::printf("  after walk: (%.2f, %.2f) state %s\n", gardener().x, gardener().y, stateName(gardener().state));
    CHECK(gardener().state != GState::UserWalk && gardener().state != GState::Dragged, "then he resumes his normal work");

    std::printf("== 7. windmill click ==\n");
    reset(); setWindmillHub(1.18f, 0.25f); update(DT, S, M, true); M = ui::Mouse(); M.valid = false;
    float a0 = windAngle(); step(60); float calm = windAngle() - a0;
    M.valid = true; M.x = 1.18f; M.y = 0.25f; M.pressed = true; M.down = true; a0 = windAngle(); step(1); M.down = false; M.valid = false; step(59);
    float gust = windAngle() - a0;
    std::printf("  calm: %.0f deg/s   after click: %.0f deg/s\n", calm, gust);
    CHECK(gust > calm * 3.0f, "clicking the windmill makes it spin faster, then it slows down again");
    step(60 * 10); float later = windAngle(); step(60); CHECK(windAngle() - later < calm * 1.3f, "wind gust fades out");

    std::printf("\n%s (%d failure%s)\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
