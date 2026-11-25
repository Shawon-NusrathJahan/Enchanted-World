// Headless test of the playground simulation (no window, no OpenGL).
#include "playgroundsim.h"
#include <cmath>
#include <cstdio>
#include <vector>
using namespace playground;
static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { std::printf("  FAIL: %s\n", m); fails++; } else std::printf("  ok:   %s\n", m); } while (0)
static const float DT = 1.0f / 60.0f;
static Input IN;
static void step(int n) { for (int i = 0; i < n; i++) { update(DT, IN); IN.pressed = false; IN.released = false; } }
static void secs(float s) { step((int)(s * 60)); }
static void clean() { reset(); testFreeze(true); testClearRides(); }
static bool riding(int id) { const Kid* k = findKid(id); return k && k->st == KState::Riding; }

int main()
{
    using namespace playgroundcfg;
    std::printf("== 1. pendulum period (damped, empty swing) ==\n");
    clean();
    testPushSwing(0);
    std::vector<float> crossings; float prev = swing(0).theta, t = 0;
    for (int i = 0; i < 60 * 8; i++) { step(1); t += DT; const float th = swing(0).theta; if ((prev < 0) != (th < 0)) crossings.push_back(t); prev = th; }
    float half = 0; for (size_t i = 1; i < crossings.size(); i++) half += crossings[i] - crossings[i - 1];
    half /= std::max<size_t>(1, crossings.size() - 1);
    std::printf("  measured period %.3fs, theory 2*pi*sqrt(L/G) = %.3fs\n", 2 * half, swingPeriod());
    CHECK(crossings.size() >= 4 && std::fabs(2 * half - swingPeriod()) / swingPeriod() < 0.06f, "period matches 2*pi*sqrt(L/G) within 6%");
    clean(); testPushSwing(0); secs(1); float a1 = swingAmplitude(0); secs(4); float a2 = swingAmplitude(0);
    std::printf("  amplitude %.1f deg -> %.1f deg after 4 s\n", a1 * 57.3f, a2 * 57.3f);
    CHECK(a2 < a1 && a2 > 0.0f, "damping makes the amplitude decay");

    std::printf("== 2. rider pumps the swing up, round-robin quantum ends the turn ==\n");
    clean();
    testSendKid(1, Ride::Swing);
    for (int i = 0; i < 600 && !riding(1); i++) step(1);
    CHECK(riding(1), "kid #1 walked to a swing and sat down");
    secs(9);
    std::printf("  amplitude while pumping: %.1f deg\n", swingAmplitude(findKid(1)->seat) * 57.3f);
    CHECK(swingAmplitude(findKid(1)->seat) > 0.5f, "pumping raises the amplitude above 28 deg");
    secs(RIDE_TIME + 8);
    CHECK(!riding(1) && stats().rides[0] == 1, "after RIDE_TIME the child slows down and leaves (1 swing ride)");

    std::printf("== 3. two swings share one FIFO queue ==\n");
    clean();
    for (int id = 1; id <= 4; id++) testSendKid(id, Ride::Swing);
    secs(8);
    CHECK(riding(1) && riding(2) && !riding(3) && !riding(4), "kids #1 and #2 (first in line) get the two swings");
    CHECK(queue(Ride::Swing).size() == 2 && queue(Ride::Swing)[0] == 3 && queue(Ride::Swing)[1] == 4, "#3 then #4 wait in FIFO order");
    bool r3 = false, r4 = false, early = false;
    for (int i = 0; i < 60 * 45; i++) { step(1); if (riding(3)) r3 = true; if (riding(4)) r4 = true; if ((r3 || r4) && riding(1) && riding(2)) early = true; }
    CHECK(r3 && r4 && !early, "#3 and #4 ride only after a swing is freed");

    std::printf("== 4. see-saw: torque and the lever law ==\n");
    clean();
    int light = 1, heavy = 1;
    for (const Kid& k : kids()) { if (k.mass < findKid(light)->mass) light = k.id; if (k.mass > findKid(heavy)->mass) heavy = k.id; }
    testSendKid(light, Ride::Seesaw); testSendKid(heavy, Ride::Seesaw);
    for (int i = 0; i < 60 * 15 && !(seesaw().mounted[0] && seesaw().mounted[1]); i++) step(1);
    const Seesaw& q = seesaw();
    const int ls = q.seat[0] == light ? 0 : 1, hs = 1 - ls;
    const float ml = findKid(light)->mass, mh = findKid(heavy)->mass;
    std::printf("  light %.0f kg at %.3f, heavy %.0f kg at %.3f  ->  moments %.2f vs %.2f\n", ml, q.arm[ls], mh, q.arm[hs], ml * q.arm[ls], mh * q.arm[hs]);
    CHECK(q.mounted[0] && q.mounted[1], "both children are seated");
    CHECK(std::fabs(ml * q.arm[ls] - mh * q.arm[hs]) < 0.01f, "balance ON: m_light*d_light = m_heavy*d_heavy");
    CHECK(q.arm[hs] < q.arm[ls], "the heavier child sits closer to the pivot");
    float onHeavy = 0, onLight = 0;
    for (int i = 0; i < 60 * 5; i++) { step(1); const float sg = hs == 0 ? 1.0f : -1.0f; if (sg * seesaw().phi > 0.05f) onHeavy += DT; if (-sg * seesaw().phi > 0.05f) onLight += DT; }
    std::printf("  balance ON : %.1fs heavy side down, %.1fs light side down\n", onHeavy, onLight);
    CHECK(onLight > 1.0f && onHeavy > 1.0f, "balanced see-saw goes up and down on both sides");
    testSetSeesawBalance(false);
    onHeavy = onLight = 0;
    float tq = 0;
    for (int i = 0; i < 60 * 5; i++)
    {
        step(1); tq = std::max(tq, std::fabs(seesaw().torque));
        const float sg = hs == 0 ? 1.0f : -1.0f;
        if (sg * seesaw().phi > 0.05f) onHeavy += DT;
        if (-sg * seesaw().phi > 0.05f) onLight += DT;
    }
    std::printf("  balance OFF: torque %.1f, %.1fs heavy side down, %.1fs light side down\n", tq, onHeavy, onLight);
    CHECK(tq > 1.0f && onHeavy > onLight, "balance OFF: unbalanced torque keeps the heavy side down longer");

    std::printf("== 5. slide: inclined plane with friction ==\n");
    clean();
    testSendKid(5, Ride::Slide);
    float vmax = 0;
    for (int i = 0; i < 60 * 25; i++) { step(1); const Kid* k = findKid(5); if (k->st == KState::Riding && k->slideStage == 2) vmax = std::max(vmax, k->slideV); }
    const float a = G * (std::sin(slideAngle()) - SLIDE_FRICTION * std::cos(slideAngle()));
    const float vth = std::sqrt(2 * a * slideLength());
    std::printf("  speed at the bottom %.3f, theory sqrt(2 a L) = %.3f\n", stats().maxSlideSpeed, vth);
    CHECK(std::fabs(stats().maxSlideSpeed - vth) / vth < 0.04f, "speed matches the inclined-plane formula within 4%");
    CHECK(stats().rides[2] == 1 && findKid(5)->st == KState::Wander, "the child lands and walks off (1 slide ride)");

    std::printf("== 6. ball: projectile height and bounce restitution ==\n");
    clean();
    testKickBall(0.0f, 1.0f);
    float h1 = 0; bool fell = false; float h2 = 0;
    for (int i = 0; i < 60 * 4; i++)
    {
        step(1); const float h = ball().h;
        if (!fell) { h1 = std::max(h1, h); if (h <= 0.0f && h1 > 0.05f) fell = true; }
        else h2 = std::max(h2, h);
    }
    const float hth = 1.0f / (2 * G), e2 = BALL_RESTITUTION * BALL_RESTITUTION;
    std::printf("  first apex %.4f (theory v^2/2G = %.4f), second apex %.4f (theory e^2 h1 = %.4f)\n", h1, hth, h2, e2 * h1);
    CHECK(std::fabs(h1 - hth) / hth < 0.03f, "apex height = v^2 / 2G within 3%");
    CHECK(std::fabs(h2 - e2 * h1) / (e2 * h1) < 0.08f, "bounce height = e^2 x previous within 8%");

    std::printf("== 7. load balancing: nobody joins a queue longer than PATIENCE ==\n");
    clean();
    for (int id = 1; id <= 7; id++) testSendKid(id, Ride::Swing);
    secs(1);
    std::printf("  expected wait: swings %.0fs, see-saw %.0fs, slide %.0fs, ball %.0fs\n", expectedWait(Ride::Swing), expectedWait(Ride::Seesaw), expectedWait(Ride::Slide), expectedWait(Ride::Ball));
    CHECK(expectedWait(Ride::Swing) > PATIENCE && expectedWait(Ride::Slide) < expectedWait(Ride::Swing), "a long swing queue has the biggest expected wait");
    const size_t q0 = queue(Ride::Swing).size();
    testFreeze(false);
    bool grew = false; size_t maxQ = q0;
    for (int i = 0; i < 60 * 15; i++) { step(1); if (queue(Ride::Swing).size() > q0) grew = true; maxQ = std::max(maxQ, queue(Ride::Swing).size()); }
    CHECK(!grew, "free children chose shorter queues instead of the swings");

    std::printf("== 8. dragging a child onto the slide ==\n");
    clean();
    const Kid* k = findKid(6);
    IN = Input(); IN.valid = true; IN.x = k->x; IN.y = k->y + 0.12f; IN.down = true; IN.pressed = true; step(1);
    CHECK(!dragging(), "a press alone does not pick anyone up (drag threshold)");
    IN.x += 0.05f; step(1);
    CHECK(dragging() && findKid(6)->st == KState::Dragged, "kid #6 picked up once the mouse moves");
    for (int i = 1; i <= 10; i++) { IN.x += (0.55f - IN.x) * 0.3f; IN.y += (-0.1f - IN.y) * 0.3f; step(1); }
    IN.x = 0.55f; IN.y = -0.10f; IN.down = false; IN.released = true; step(1); IN = Input();
    CHECK(!dragging() && findKid(6)->ride == Ride::Slide, "dropped on the slide: joins the slide queue");

    std::printf("== 9. a plain click on a queued child keeps its place ==\n");
    clean();
    for (int id = 1; id <= 5; id++) testSendKid(id, Ride::Swing);
    secs(8);
    const Kid* q3 = findKid(queue(Ride::Swing).back());
    const int qid = q3->id; const size_t qlen = queue(Ride::Swing).size();
    IN = Input(); IN.valid = true; IN.x = q3->x; IN.y = q3->y + 0.12f; IN.down = true; IN.pressed = true; step(1);
    IN.down = false; IN.released = true; step(1); IN = Input();
    CHECK(queue(Ride::Swing).size() == qlen && queue(Ride::Swing).back() == qid, "clicking without moving leaves the queue unchanged");

    std::printf("== 10. 10-minute stability run ==\n");
    reset();
    bool ok = true;
    for (int i = 0; i < 60 * 600; i++)
    {
        step(1);
        for (const Kid& c : kids()) if (!std::isfinite(c.x) || !std::isfinite(c.y) || std::fabs(c.x) > 1.8f || c.y < -0.9f || c.y > 0.6f) ok = false;
        if (!std::isfinite(swing(0).theta) || !std::isfinite(seesaw().phi) || !std::isfinite(ball().x)) ok = false;
    }
    const Stats& s = stats();
    std::printf("  rides: swings %d, see-saw %d, slide %d; kicks %d, bounces %d\n", s.rides[0], s.rides[1], s.rides[2], s.kicks, s.bounces);
    CHECK(ok, "all positions finite and inside the playground");
    CHECK(s.rides[0] > 5 && s.rides[1] > 2 && s.rides[2] > 5 && s.kicks > 5, "every ride and the ball game were used");

    std::printf("\n%s (%d failure%s)\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
