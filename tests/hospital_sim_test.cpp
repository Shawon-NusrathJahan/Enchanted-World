// Headless test of the hospital simulation (no window, no OpenGL).
#include "hospitalsim.h"
#include "game.h"
#include <cstdio>
#include <cmath>
#include <string>
using namespace hospital;
static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { std::printf("  FAIL: %s\n", m); fails++; } else std::printf("  ok:   %s\n", m); } while (0)
static const float DT = 1.0f / 60.0f;
static Input IN;
static void step(int n, View v = View::Inside) { for (int i = 0; i < n; i++) { update(DT, v, IN); IN.pressed = false; IN.released = false; } }
static void secs(float s, View v = View::Inside) { step((int)(s * 60), v); }
static const Patient* last() { return patients().empty() ? nullptr : &patients().back(); }
static void clearWorld() { reset(); setAmbulancesEnabled(false); setPedestriansEnabled(false); testClearPedestrians(); }
static void seat(int id) { for (int i = 0; i < 600; i++) { step(1); const Patient* p = findPatient(id); if (p && p->st == PState::Seated) return; } }
// drag patient id to world point (x,y) and release
static void dragTo(int id, float x, float y)
{
    const Patient* p = findPatient(id);
    if (!p) { std::printf("  (patient #%d no longer exists)\n", id); fails++; return; }
    IN = Input(); IN.valid = true; IN.x = p->x; IN.y = p->y + 0.15f; IN.down = true; IN.pressed = true; step(1);
    for (int i = 1; i <= 10; i++) { IN.x = p->x + (x - p->x) * i / 10.f; IN.y = IN.y + (y - IN.y) * i / 10.f; IN.pressed = false; step(1); }
    IN.x = x; IN.y = y; IN.down = false; IN.released = true; step(1);
    IN = Input(); IN.valid = false;
}
static std::string lastLog() { return log().empty() ? "" : log().back().text; }

int main()
{
    std::printf("== 1. triage priority queue ==\n");
    clearWorld();
    testSpawnPatient(Kind::General, Cond::Fever, 2); int g1 = last()->id;
    testSpawnPatient(Kind::Emergency, Cond::BrokenArm, 2); int e1 = last()->id;
    testSpawnPatient(Kind::Emergency, Cond::Surgery, 5); int e2 = last()->id;
    testSpawnPatient(Kind::General, Cond::Cough, 1); int g2 = last()->id;
    secs(8);
    auto order = triageOrder();
    std::printf("  triage order: ");
    for (int id : order) std::printf("#%d(%s s%d) ", id, kindName(findPatient(id)->kind), findPatient(id)->severity);
    std::printf("\n");
    CHECK(order.size() == 4 && order[0] == e2 && order[1] == e1, "emergencies first, sickest (severity 5) before severity 2");
    CHECK(order[2] == g1 && order[3] == g2, "then general patients, higher severity first");

    std::printf("== 2. correct / incorrect placement ==\n");
    clearWorld();
    testSpawnPatient(Kind::Emergency, Cond::BrokenLeg, 3); int E = last()->id;
    testSpawnPatient(Kind::General, Cond::Checkup, 1); int G = last()->id;
    secs(6); seat(E); seat(G);
    dragTo(E, 0.5f, -0.3f);                       // emergency -> ER
    secs(3);
    CHECK(findPatient(E)->zone == Zone::Emergency && findPatient(E)->st == PState::Treating, "emergency patient dragged into ER is admitted and treated");
    CHECK(stats().correct == 1, "counted as a correct placement");
    dragTo(G, 1.4f, -0.4f);                       // general -> consult
    secs(3);
    CHECK(findPatient(G)->zone == Zone::Consult && findPatient(G)->st == PState::Treating, "general patient dragged into consultation is assigned");
    CHECK(stats().correct == 2, "second correct placement counted");

    clearWorld();
    testSpawnPatient(Kind::Emergency, Cond::Bleeding, 3); E = last()->id;
    testSpawnPatient(Kind::General, Cond::Cough, 1); G = last()->id;
    secs(6); seat(E); seat(G);
    int sevBefore = findPatient(E)->severity;
    dragTo(E, 1.4f, -0.4f);                       // emergency -> general consult = WRONG
    secs(9);
    CHECK(findPatient(E)->zone == Zone::Waiting && findPatient(E)->st == PState::Seated, "emergency patient in consultation bounces back to a seat");
    CHECK(stats().wrong == 1 && findPatient(E)->severity == sevBefore + 1, "counted as wrong, and the condition worsens");
    dragTo(G, 0.5f, -0.3f);                       // general -> ER = WRONG
    secs(9);
    CHECK(findPatient(G)->zone == Zone::Waiting && stats().wrong == 2, "general patient dragged into the ER is refused");
    std::printf("  last message: %s\n", lastLog().c_str());

    std::printf("== 3. ER cooldown ==\n");
    clearWorld();
    testSpawnPatient(Kind::Emergency, Cond::Surgery, 4); E = last()->id;
    secs(6); seat(E);
    dragTo(E, 0.5f, -0.3f); secs(3);
    CHECK(findPatient(E)->st == PState::Treating, "patient is in the ER");
    float leftBefore = findPatient(E)->treatLeft;
    dragTo(E, -0.5f, -0.3f); secs(2);
    CHECK(findPatient(E)->zone == Zone::Waiting && findPatient(E)->cooldown > 8.0f, "moved to waiting: ER lock-out starts");
    dragTo(E, 0.5f, -0.3f); secs(2);
    CHECK(findPatient(E)->zone == Zone::Waiting && stats().rejected >= 1, "dragging back to the ER during the lock-out is rejected");
    std::printf("  message: %s\n", lastLog().c_str());
    secs(11);                                      // lock-out (12 s) now over
    CHECK(findPatient(E)->cooldown <= 0.01f, "lock-out expires after ER_COOLDOWN seconds");
    dragTo(E, 0.5f, -0.3f); secs(3);
    CHECK(findPatient(E)->zone == Zone::Emergency && findPatient(E)->st == PState::Treating, "after the lock-out the patient can enter the ER again");
    CHECK(findPatient(E)->treatLeft < leftBefore + 0.01f && findPatient(E)->treatLeft > 0, "treatment progress was kept (not reset)");

    std::printf("== 4. ER beds, ER queue, bed handed to the queue head ==\n");
    clearWorld();
    int ids[5];
    const int sev[5] = {3, 3, 2, 5, 4};
    for (int i = 0; i < 5; i++) { testSpawnPatient(Kind::Emergency, Cond::Surgery, sev[i]); ids[i] = last()->id; }
    secs(12);
    for (int i = 0; i < 5; i++) seat(ids[i]);
    for (int i = 0; i < 4; i++) { dragTo(ids[i], 0.5f, -0.3f); secs(0.5f); }
    secs(5);
    CHECK(erBedsFree() == 0 && erQueueCount() == 2, "2 beds full, the other 2 patients wait in the ER queue");
    dragTo(ids[4], 0.5f, -0.3f); secs(5);
    CHECK(erQueueCount() == 3, "ER queue takes a third patient (queue capacity 3)");
    // free one bed: pull patient 0 out -> highest priority in queue (#4 sev 5) must get the bed
    dragTo(ids[0], -0.5f, -0.3f); secs(6);
    CHECK(findPatient(ids[3])->slot < ER_BEDS && findPatient(ids[3])->zone == Zone::Emergency, "freed bed goes to the highest-priority queued patient (severity 5)");

    std::printf("== 5. treatment, discharge, released counter ==\n");
    clearWorld();
    testSpawnPatient(Kind::General, Cond::Checkup, 1); G = last()->id;
    secs(6); seat(G);
    dragTo(G, 1.4f, -0.4f);
    CHECK(stats().released == 0, "released starts at 0");
    secs(25);
    CHECK(stats().released == 1 && patients().empty(), "after treatment the patient is discharged and released++");
    CHECK(pedestrians().size() == 1 && pedestrians()[0].symptom < 0.1f, "released patient leaves as a healthy pedestrian outside");

    std::printf("== 6. nurse auto-triage when the user does nothing ==\n");
    clearWorld();
    testSpawnPatient(Kind::Emergency, Cond::BrokenArm, 3); E = last()->id;
    testSpawnPatient(Kind::General, Cond::Fever, 1); G = last()->id;
    secs(hospitalcfg::AUTO_TRIAGE_DELAY + 14);
    CHECK(stats().autoAssigned >= 2, "nurse placed both waiting patients using the priority queue");
    CHECK(findPatient(E) == nullptr || findPatient(E)->zone == Zone::Emergency || stats().released >= 1, "emergency patient went to the ER");
    secs(60);
    CHECK(stats().released == 2, "both patients finished treatment and were released");

    std::printf("== 7. emergency patients worsen while they cannot be placed ==\n");
    clearWorld();
    for (int i = 0; i < 6; i++) testSpawnPatient(Kind::Emergency, Cond::Surgery, 2);   // 2 beds + 3 queue slots = 5 places
    secs(hospitalcfg::AUTO_TRIAGE_DELAY + 12);
    int worse = 0; for (const Patient& p : patients()) if (p.severity > 2) worse++;
    CHECK(worse >= 1, "the patient left without a place gets worse (severity rises)");
    std::printf("  message: %s\n", lastLog().c_str());

    std::printf("== 8. ambulances ==\n");
    reset(); testClearPedestrians(); setPedestriansEnabled(false);
    int a0 = stats().admitted; bool sawAmb = false; int maxAmb = 0;
    for (int i = 0; i < 60 * 130; i++) { step(1, View::Outside); if (!ambulances().empty()) sawAmb = true; maxAmb = std::max(maxAmb, (int)ambulances().size()); }
    std::printf("  ambulances: %d, patients admitted: %d\n", stats().ambulances, stats().admitted - a0);
    CHECK(sawAmb && stats().ambulances >= 4, "ambulances arrive periodically (every ~22 s)");
    CHECK(maxAmb == 1, "only one ambulance at the bay at a time (FIFO)");
    CHECK(stats().admitted - a0 >= stats().ambulances - 1, "every ambulance delivered patients and the admission count rose");
    CHECK(stats().admittedEmergency > stats().admittedGeneral, "most ambulance patients are emergencies");

    std::printf("== 9. dragging pedestrians to the hospital ==\n");
    reset(); setAmbulancesEnabled(false); setPedestriansEnabled(false); testClearPedestrians();
    testSpawnPedestrian(-0.8f, 0.95f, false);  int pSick = pedestrians().back().id;
    auto dragPed = [&](int id, float x, float y) {
        const Pedestrian* p = findPedestrian(id);
        IN = Input(); IN.valid = true; IN.x = p->x; IN.y = p->y + 0.14f; IN.down = true; IN.pressed = true; step(1, View::Outside);
        IN.pressed = false; IN.x = x; IN.y = y; step(2, View::Outside);
        IN.down = false; IN.released = true; step(1, View::Outside); IN = Input();
    };
    dragPed(pSick, 0.0f, 0.1f); secs(8, View::Outside);
    CHECK(stats().admitted == 1 && patients().size() == 1, "sick pedestrian dragged onto the hospital is admitted");
    int adm = stats().admitted;
    testSpawnPedestrian(0.8f, 0.10f, false);   int pWell = pedestrians().back().id;
    testSpawnPedestrian(0.5f, 0.40f, false);   int pMild = pedestrians().back().id;
    dragPed(pWell, 0.2f, 0.1f); secs(3, View::Outside);
    CHECK(stats().admitted == adm && stats().turnedAway == 1, "healthy pedestrian is NOT admitted (turned away)");
    dragPed(pMild, 0.3f, 0.1f); secs(3, View::Outside);
    CHECK(stats().admitted == adm && stats().turnedAway == 2, "mild symptoms (below threshold) are NOT admitted either");
    CHECK(findPedestrian(pWell) != nullptr && findPedestrian(pWell)->st == Pedestrian::Walk, "rejected person walks on");
    const int turned = stats().turnedAway;
    testSpawnPedestrian(-0.8f, 0.95f, false); int pS2 = pedestrians().back().id;
    dragPed(pS2, -1.6f, -0.65f); secs(3, View::Outside);
    CHECK(stats().admitted == adm && stats().turnedAway == turned, "dropping a sick person away from the hospital does nothing");

    std::printf("== 10. walk-in patients ==\n");
    reset(); setAmbulancesEnabled(false); setPedestriansEnabled(false); testClearPedestrians();
    testSpawnPedestrian(-0.6f, 0.9f, true);
    secs(20, View::Outside);
    CHECK(stats().admitted == 1, "a sick pedestrian who seeks care walks to the door and is admitted");

    std::printf("== 11. 10 minutes with the nurse only: stability ==\n");
    reset();
    for (int i = 0; i < 60 * 600; i++) step(1, View::Outside);
    std::printf("  admitted %d, released %d, inside now %d, ambulances %d, auto-assigned %d\n", stats().admitted, stats().released, insideCount(), stats().ambulances, stats().autoAssigned);
    CHECK(stats().released > 5 && insideCount() <= hospitalcfg::MAX_INSIDE, "hospital keeps working and never overfills");


    std::printf("== 12. score: right room +, wrong room - ==\n");
    clearWorld(); game::init();
    testSpawnPatient(Kind::General, Cond::Fever, 1);
    const int g = last()->id; seat(g);
    int sc0 = game::score();
    dragTo(g, 0.56f, -0.40f);                                  // fever into the ER: wrong
    CHECK(game::score() == sc0 + hospitalcfg::SCORE_WRONG_ROOM, "wrong room costs points");
    seat(g); sc0 = game::score();
    dragTo(g, 1.38f, -0.40f);                                  // consultation: right
    CHECK(game::score() == sc0 + hospitalcfg::SCORE_CORRECT_ROOM, "right room adds points");

    std::printf("== 13. injured playground child arrives, is healed and goes back ==\n");
    clearWorld(); game::init();
    game::Injury inj; inj.kidId = 7; inj.cond = (int)Cond::BrokenArm; inj.severity = 3;
    game::sendToHospital(inj);
    step(2);
    const Patient* kp = last();
    CHECK(kp && kp->kidId == 7 && kp->kind == Kind::Emergency, "child admitted as an emergency patient");
    const int kid = kp ? kp->id : -1; seat(kid);
    dragTo(kid, 0.40f, -0.30f);
    secs(60);
    int healed = -1;
    CHECK(stats().kidsHealed == 1 && game::takeHealed(healed) && healed == 7, "treated, released, and sent back to the playground");

    std::printf("\n%s (%d failure%s)\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
