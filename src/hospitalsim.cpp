#include "hospitalsim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <queue>

namespace hospital
{
namespace
{

using namespace hospitalcfg;

struct Rng
{
    unsigned s = 1;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFFu) / 16777216.0f; }
    float range(float a, float b) { return a + (b - a) * next(); }
    int   pick(int n) { return (int)(next() * n) % n; }
};

Rng                     rng;
std::vector<Patient>    pts;
std::vector<Pedestrian> peds;
std::vector<Ambulance>  ambs;
std::vector<Popup>      pops;
std::vector<LogLine>    logv;
Stats                   st;
int    seatOcc[SEATS], erOcc[ER_SLOTS], consOcc[CONSULT_SLOTS];
int    nextId = 1, nextPedId = 1, hoverP = -1, hoverPed = -1, dragP = -1, dragPed = -1;
Zone   dragFrom = Zone::None;
int    dragFromSlot = -1;
double simTime = 0.0, ambTimer = 8.0, pedTimer = 0.0, triageTimer = 0.0;
bool   ambOn = true, pedOn = true;
unsigned seed0 = 12345u;
int    ambCount = 0;

const float SEAT_X[SEATS] = {-0.84f, -0.64f, -0.44f, -0.24f, -0.04f, -0.84f, -0.64f, -0.44f, -0.24f, -0.04f, -0.55f, -0.25f};
const float SEAT_Y[SEATS] = {-0.26f, -0.26f, -0.26f, -0.26f, -0.26f, -0.50f, -0.50f, -0.50f, -0.50f, -0.50f, -0.69f, -0.69f};
const float ER_X[ER_SLOTS] = {0.36f, 0.74f, 0.30f, 0.52f, 0.74f};
const float ER_Y[ER_SLOTS] = {-0.30f, -0.30f, -0.66f, -0.66f, -0.66f};
const float CONS_X[CONSULT_SLOTS] = {1.20f, 1.56f};
const float CONS_Y[CONSULT_SLOTS] = {-0.46f, -0.46f};

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------
void push(const LogLine& l)
{
    logv.push_back(l);
    if (logv.size() > 40) logv.erase(logv.begin());
}
void sayD(const char* fmt, int color, int a = 0)              // message with one number
{
    LogLine l;
    std::snprintf(l.text, sizeof(l.text), fmt, a);
    l.color = color;
    push(l);
}
void sayS(const char* fmt, int color, const char* s)          // message with one string
{
    LogLine l;
    std::snprintf(l.text, sizeof(l.text), fmt, s);
    l.color = color;
    push(l);
}

void popup(View v, float x, float y, int color, const char* text)
{
    Popup p;
    p.view = v; p.x = x; p.y = y; p.color = color;
    std::snprintf(p.text, sizeof(p.text), "%s", text);
    pops.push_back(p);
    if (pops.size() > 24) pops.erase(pops.begin());
}

Patient* byId(int id)
{
    for (Patient& p : pts) if (p.id == id) return &p;
    return nullptr;
}

bool moveToward(float& x, float& y, float tx, float ty, float speed, float dt)
{
    const float dx = tx - x, dy = ty - y, d = std::sqrt(dx * dx + dy * dy), step = speed * dt;
    if (d <= step) { x = tx; y = ty; return true; }
    x += dx / d * step; y += dy / d * step;
    return false;
}

float treatTimeOf(Cond c)
{
    switch (c)
    {
    case Cond::Checkup:   return 6.0f;
    case Cond::Fever:     return 7.0f;
    case Cond::Cough:     return 6.0f;
    case Cond::Bleeding:  return 10.0f;
    case Cond::BrokenArm: return 9.0f;
    case Cond::BrokenLeg: return 11.0f;
    default:              return 18.0f;       // surgery
    }
}

int* occ(Zone z, int& n)
{
    switch (z)
    {
    case Zone::Waiting:   n = SEATS;          return seatOcc;
    case Zone::Emergency: n = ER_SLOTS;       return erOcc;
    case Zone::Consult:   n = CONSULT_SLOTS;  return consOcc;
    default:              n = 0;              return nullptr;
    }
}

void freeSlot(Patient& p)
{
    int n = 0;
    int* o = occ(p.zone, n);
    if (o && p.slot >= 0 && p.slot < n && o[p.slot] == p.id) o[p.slot] = -1;
    p.slot = -1;
}

void slotXY(Zone z, int slot, float& x, float& y)
{
    switch (z)
    {
    case Zone::Waiting:   x = SEAT_X[slot]; y = SEAT_Y[slot]; break;
    case Zone::Emergency: x = ER_X[slot];   y = ER_Y[slot];   break;
    case Zone::Consult:   x = CONS_X[slot]; y = CONS_Y[slot]; break;
    default:              x = DESK_X;       y = DESK_Y;       break;
    }
}

// reserve a slot and start walking to it
void sendTo(Patient& p, Zone z, int slot)
{
    freeSlot(p);
    int n = 0;
    int* o = occ(z, n);
    if (o) o[slot] = p.id;
    p.zone = z; p.slot = slot;
    slotXY(z, slot, p.tx, p.ty);
    p.st = PState::Walking;
}

int freeSeat()
{
    for (int i = 0; i < SEATS; i++) if (seatOcc[i] < 0) return i;
    return -1;
}
int freeBed()      { for (int i = 0; i < ER_BEDS; i++) if (erOcc[i] < 0) return i; return -1; }
int freeErQueue()  { for (int i = ER_BEDS; i < ER_SLOTS; i++) if (erOcc[i] < 0) return i; return -1; }
int freeConsult()  { for (int i = 0; i < CONSULT_SLOTS; i++) if (consOcc[i] < 0) return i; return -1; }

void backToSeat(Patient& p)
{
    int s = freeSeat();
    if (s < 0) s = SEATS - 1;                 // cannot happen below MAX_INSIDE; keeps the sim safe
    sendTo(p, Zone::Waiting, s);
}

// ---------------------------------------------------------------------------
// Triage: max-priority queue with aging
// ---------------------------------------------------------------------------
std::vector<int> triageIds()
{
    typedef std::pair<float, int> Item;                       // (priority, id)
    std::priority_queue<Item> heap;                           // max-heap: highest priority on top
    for (const Patient& p : pts)
        if (p.st == PState::Seated || p.st == PState::Queued) heap.push(Item(priorityOf(p), p.id));
    std::vector<int> out;
    while (!heap.empty()) { out.push_back(heap.top().second); heap.pop(); }
    return out;
}

// Placement rules - the same function is used for the user's drops and for the nurse.
// Returns true if the patient was placed in the requested area.
enum class Verdict { Ok, WrongArea, Cooldown, Full, QueuedErFull };

Verdict tryPlace(Patient& p, Zone z, bool byUser, char* why, size_t whySize)
{
    why[0] = 0;
    if (z == Zone::Emergency)
    {
        if (p.kind == Kind::General)
        {
            std::snprintf(why, whySize, "#%d IS A GENERAL PATIENT - ER IS FOR EMERGENCIES", p.id);
            return Verdict::WrongArea;
        }
        if (p.cooldown > 0.0f)
        {
            std::snprintf(why, whySize, "#%d MUST WAIT %.0fS BEFORE RETURNING TO ER", p.id, std::ceil(p.cooldown));
            return Verdict::Cooldown;
        }
        int s = freeBed();
        if (s >= 0) { sendTo(p, Zone::Emergency, s); std::snprintf(why, whySize, "#%d -> ER BED (%s)", p.id, condName(p.cond)); return Verdict::Ok; }
        s = freeErQueue();
        if (s >= 0) { sendTo(p, Zone::Emergency, s); std::snprintf(why, whySize, "#%d -> ER QUEUE (BEDS FULL)", p.id); return Verdict::Ok; }
        std::snprintf(why, whySize, "ER AND ER QUEUE ARE FULL");
        return Verdict::Full;
    }
    if (z == Zone::Consult)
    {
        if (p.kind == Kind::Emergency)
        {
            if (p.severity < 5) p.severity++;
            p.flash = 1.0f;
            std::snprintf(why, whySize, "#%d NEEDS THE ER - CONDITION WORSENED", p.id);
            return Verdict::WrongArea;
        }
        const int s = freeConsult();
        if (s < 0) { std::snprintf(why, whySize, "CONSULTATION ROOMS ARE FULL"); return Verdict::Full; }
        sendTo(p, Zone::Consult, s);
        std::snprintf(why, whySize, "#%d -> CONSULTATION (%s)", p.id, condName(p.cond));
        return Verdict::Ok;
    }
    (void)byUser;
    return Verdict::Ok;
}

void promoteErQueue()
{
    const int bed = freeBed();
    if (bed < 0) return;
    typedef std::pair<float, int> Item;
    std::priority_queue<Item> heap;
    for (const Patient& p : pts)
        if (p.zone == Zone::Emergency && p.slot >= ER_BEDS && p.st == PState::Queued) heap.push(Item(priorityOf(p), p.id));
    if (heap.empty()) return;
    Patient* p = byId(heap.top().second);
    if (!p) return;
    sendTo(*p, Zone::Emergency, bed);
    sayD("ER BED FREE: #%d TAKEN FROM QUEUE (HEAP)", 0, p->id);
}

void createPatient(float symptom, bool amb, float x, float y, View announce)
{
    Patient p;
    p.id = nextId++;
    p.kind = symptom >= EMERGENCY_THRESHOLD ? Kind::Emergency : Kind::General;
    if (p.kind == Kind::Emergency)
    {
        static const Cond e[4] = {Cond::Bleeding, Cond::BrokenArm, Cond::BrokenLeg, Cond::Surgery};
        p.cond = e[rng.pick(4)];
        p.severity = p.cond == Cond::Surgery ? 4 + rng.pick(2) : 2 + rng.pick(3);
    }
    else
    {
        static const Cond g[3] = {Cond::Checkup, Cond::Fever, Cond::Cough};
        p.cond = g[rng.pick(3)];
        p.severity = 1 + rng.pick(2);
    }
    p.treatTotal = p.treatLeft = treatTimeOf(p.cond);
    p.skin = rng.pick(4); p.hair = rng.pick(4); p.shirt = rng.pick(6);
    p.fromAmbulance = amb;
    p.x = ENTRY_X; p.y = ENTRY_Y; p.tx = DESK_X; p.ty = DESK_Y;
    p.st = PState::CheckIn;
    p.zone = Zone::Reception;
    pts.push_back(p);
    st.admitted++;
    if (p.kind == Kind::Emergency) st.admittedEmergency++; else st.admittedGeneral++;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "+1 ADMITTED: %s", kindName(p.kind));
    popup(announce, x, y, p.kind == Kind::Emergency ? 1 : 0, buf);
    char line[120];
    std::snprintf(line, sizeof(line), "ADMITTED #%d %s - %s (%s)", p.id, kindName(p.kind), condName(p.cond), amb ? "AMBULANCE" : "WALK-IN");
    LogLine l; std::snprintf(l.text, sizeof(l.text), "%s", line); l.color = p.kind == Kind::Emergency ? 1 : 0;
    push(l);
}

// ---------------------------------------------------------------------------
// Pedestrians and ambulances (outside)
// ---------------------------------------------------------------------------
float rollSymptom()
{
    const float r = rng.next();
    if (r < 0.55f) return rng.range(0.00f, 0.28f);     // healthy
    if (r < 0.70f) return rng.range(0.30f, 0.48f);     // mild: looks a bit off, does NOT need admission
    if (r < 0.88f) return rng.range(0.52f, 0.80f);     // needs general care
    return rng.range(0.84f, 1.00f);                    // needs emergency care
}

void spawnPed(float x, float symptom, float dir, bool amb)
{
    Pedestrian p;
    p.id = nextPedId++;
    p.x = x; p.dir = dir;
    p.lane = rng.range(-0.62f, -0.55f);
    p.y = p.lane;
    p.symptom = symptom;
    p.speed = symptom >= 0.5f ? rng.range(0.08f, 0.12f) : rng.range(0.13f, 0.20f);
    p.skin = rng.pick(4); p.hair = rng.pick(4); p.shirt = rng.pick(6);
    p.phase = rng.next() * 6.0f;
    p.fromAmbulance = amb;
    p.seeksCare = !amb && symptom >= ADMIT_THRESHOLD && rng.next() < SELF_ARRIVAL_SHARE;
    peds.push_back(p);
}

int roamingCount()
{
    int n = 0;
    for (const Pedestrian& p : peds) if (!p.fromAmbulance && p.st == Pedestrian::Walk) n++;
    return n;
}

void admitPedestrian(Pedestrian& p)       // called when a person walks through the door
{
    createPatient(p.symptom, p.fromAmbulance, DOOR_X, DOOR_Y + 0.25f, View::Outside);
}

void startAmbulance()
{
    Ambulance a;
    a.dir = (ambCount % 2 == 0) ? 1.0f : -1.0f;
    a.x = -2.5f * a.dir;
    a.stopX = 0.45f * a.dir;
    a.cargo = rng.next() < 0.55f ? 1 : 2;
    ambs.push_back(a);
    ambCount++;
    st.ambulances++;
    sayD("AMBULANCE ARRIVING WITH %d PATIENT(S)", 2, a.cargo);
}

void updateAmbulances(float dt)
{
    for (Ambulance& a : ambs)
    {
        a.siren += dt * 6.0f;
        const float rear = a.stopX - 0.25f * a.dir;
        switch (a.st)
        {
        case Ambulance::Approach:
        {
            const float remaining = std::fabs(a.stopX - a.x);
            const float sp = std::max(0.12f, std::min(0.95f, remaining * 1.1f));   // eases to a stop
            a.x += a.dir * sp * dt; a.wheel += sp * dt * 14.0f;
            if (remaining < 0.015f) { a.x = a.stopX; a.st = Ambulance::Unload; a.t = 0; popup(View::Outside, a.stopX, -0.35f, 2, "AMBULANCE ARRIVED"); }
            break;
        }
        case Ambulance::Unload:
            a.t += dt;
            if (a.unloaded < a.cargo && a.t > 0.6f + 0.9f * a.unloaded)
            {
                const bool emerg = rng.next() < AMBULANCE_EMERGENCY_SHARE;
                const float sym = emerg ? rng.range(0.85f, 1.0f) : rng.range(0.55f, 0.80f);
                spawnPed(rear, sym, -a.dir, true);
                Pedestrian& p = peds.back();
                p.st = Pedestrian::ToDoor; p.y = ROAD_Y + 0.10f; p.speed = 0.22f;
                a.unloaded++;
            }
            if (a.unloaded >= a.cargo && a.t > 0.6f + 0.9f * a.cargo) { a.st = Ambulance::Hold; a.t = 0; }
            break;
        case Ambulance::Hold:
            a.t += dt;
            if (a.t > 2.2f) { a.st = Ambulance::Leave; a.t = 0; }
            break;
        case Ambulance::Leave:
        {
            a.t += dt;
            const float sp = std::min(1.0f, 0.2f + a.t * 0.5f);
            a.x += a.dir * sp * dt; a.wheel += sp * dt * 14.0f;
            if (std::fabs(a.x) > 2.7f) a.active = false;
            break;
        }
        }
    }
    ambs.erase(std::remove_if(ambs.begin(), ambs.end(), [](const Ambulance& a) { return !a.active; }), ambs.end());
}

void updatePeds(float dt)
{
    for (Pedestrian& p : peds)
    {
        p.phase += dt * (p.symptom >= 0.5f ? 6.0f : 9.0f);
        p.flash = std::max(0.0f, p.flash - dt);
        switch (p.st)
        {
        case Pedestrian::Walk:
            p.x += p.dir * p.speed * dt;
            p.y += (p.lane - p.y) * std::min(1.0f, 3.0f * dt);
            if (p.seeksCare && std::fabs(p.x - DOOR_X) < 0.04f && insideCount() < MAX_INSIDE) p.st = Pedestrian::ToDoor;
            break;
        case Pedestrian::ToDoor:
            if (p.fromAmbulance) { p.y += (SIDEWALK_Y1 - p.y) * std::min(1.0f, 1.5f * dt); }
            if (moveToward(p.x, p.y, DOOR_X, DOOR_Y, p.speed + 0.05f, dt)) { p.st = Pedestrian::Entering; p.alpha = 1.0f; }
            p.dir = (DOOR_X - p.x) >= 0 ? 1.0f : -1.0f;
            break;
        case Pedestrian::Entering:
            p.alpha -= dt * 2.5f;
            break;
        case Pedestrian::Dragged:
            break;
        }
    }
    for (Pedestrian& p : peds)
        if (p.st == Pedestrian::Entering && p.alpha <= 0.0f) { admitPedestrian(p); p.alpha = -1.0f; }
    peds.erase(std::remove_if(peds.begin(), peds.end(), [](const Pedestrian& p) {
        return p.alpha < 0.0f || (p.st == Pedestrian::Walk && std::fabs(p.x) > 2.15f); }), peds.end());

    pedTimer -= dt;
    if (pedOn && pedTimer <= 0.0f && roamingCount() < PEDESTRIANS)
    {
        const float dir = rng.next() < 0.5f ? 1.0f : -1.0f;
        spawnPed(-2.05f * dir, rollSymptom(), dir, false);
        pedTimer = rng.range(1.5f, 4.0f);
    }
}

// ---------------------------------------------------------------------------
// Patients (inside)
// ---------------------------------------------------------------------------
void dischargeDone(Patient& p)
{
    freeSlot(p);
    p.zone = Zone::None;
    p.st = PState::Leaving;
    p.tx = ENTRY_X; p.ty = ENTRY_Y;
    popup(View::Inside, p.x, p.y + 0.35f, 0, "TREATMENT COMPLETE");
    sayD("#%d TREATMENT COMPLETE - DISCHARGING", 0, p.id);
}

void updatePatients(float dt)
{
    for (Patient& p : pts)
    {
        p.cooldown = std::max(0.0f, p.cooldown - dt);
        p.flash = std::max(0.0f, p.flash - dt);
        p.walkPhase += dt * 8.0f;
        switch (p.st)
        {
        case PState::CheckIn:
            if (std::fabs(p.x - DESK_X) < 0.001f)
            {
                p.checkT += dt;
                if (p.checkT > 1.2f) { if (freeSeat() < 0) { p.checkT = 1.0f; break; } backToSeat(p); }
            }
            else moveToward(p.x, p.y, p.tx, p.ty, 0.35f, dt);
            break;
        case PState::Walking:
            if (moveToward(p.x, p.y, p.tx, p.ty, 0.38f, dt))
            {
                if (p.zone == Zone::Waiting) p.st = PState::Seated;
                else if (p.zone == Zone::Emergency) p.st = p.slot < ER_BEDS ? PState::Treating : PState::Queued;
                else if (p.zone == Zone::Consult) p.st = PState::Treating;
            }
            break;
        case PState::Seated:
        case PState::Queued:
            p.waited += dt;
            if (p.kind == Kind::Emergency && p.severity < 5)
            {
                p.worsenT += dt;
                if (p.worsenT > WORSEN_AFTER)
                {
                    p.worsenT = 0; p.severity++; p.flash = 1.0f;
                    LogLine l;
                    std::snprintf(l.text, sizeof(l.text), "#%d CONDITION WORSENING (SEVERITY %d)", p.id, p.severity);
                    l.color = 1;
                    push(l);
                }
            }
            break;
        case PState::Treating:
            p.treatLeft -= dt * TREAT_SPEED;
            if (p.treatLeft <= 0.0f) dischargeDone(p);
            break;
        case PState::Leaving:
            if (moveToward(p.x, p.y, p.tx, p.ty, 0.42f, dt)) p.id = -p.id;      // marked for removal
            break;
        case PState::Dragged:
            break;
        }
    }
    for (Patient& p : pts)
        if (p.id < 0)
        {
            p.id = -p.id;
            st.released++;
            const int idv = p.id;
            popup(View::Inside, ENTRY_X + 0.25f, ENTRY_Y + 0.35f, 0, "+1 RELEASED");
            sayD("#%d RELEASED - GOING HOME", 0, idv);
            spawnPed(DOOR_X, 0.0f, rng.next() < 0.5f ? 1.0f : -1.0f, false);       // leaves the building as a healthy pedestrian
            Pedestrian& q = peds.back();
            q.x = DOOR_X; q.y = DOOR_Y; q.seeksCare = false;
            p.id = 0;                                                               // remove below
        }
    pts.erase(std::remove_if(pts.begin(), pts.end(), [](const Patient& p) { return p.id == 0; }), pts.end());
}

void autoTriage()
{
    const std::vector<int> order = triageIds();
    for (int id : order)
    {
        Patient* p = byId(id);
        if (!p || p->st == PState::Dragged || p->waited < AUTO_TRIAGE_DELAY) continue;
        char why[64];
        const Zone target = p->kind == Kind::Emergency ? Zone::Emergency : Zone::Consult;
        if (target == Zone::Emergency && p->cooldown > 0.0f) continue;
        if (p->st == PState::Queued && target == Zone::Emergency) continue;          // already in the ER queue
        const Verdict v = tryPlace(*p, target, false, why, sizeof(why));
        if (v == Verdict::Ok)
        {
            st.autoAssigned++;
            char line[120];
            std::snprintf(line, sizeof(line), "NURSE TRIAGE (HEAP TOP): %s", why);
            LogLine l; std::snprintf(l.text, sizeof(l.text), "%s", line); l.color = 2;
            push(l);
            popup(View::Inside, p->x, p->y + 0.35f, 2, "NURSE TRIAGE");
            return;                                                                  // one assignment per check
        }
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
Patient* pickPatient(float x, float y)
{
    Patient* best = nullptr;
    float bd = 1e9f;
    for (Patient& p : pts)
    {
        if (p.st == PState::Leaving || p.st == PState::CheckIn) continue;
        const float cy = p.st == PState::Treating && p.zone == Zone::Emergency ? p.y + 0.10f : p.y + 0.15f;
        const float dx = x - p.x, dy = y - cy;
        const float d = dx * dx + dy * dy;
        if (std::fabs(dx) < 0.11f && std::fabs(dy) < 0.20f && d < bd) { bd = d; best = &p; }
    }
    return best;
}

Pedestrian* pickPed(float x, float y)
{
    Pedestrian* best = nullptr;
    float bd = 1e9f;
    for (Pedestrian& p : peds)
    {
        if (p.st == Pedestrian::Entering) continue;
        const float dx = x - p.x, dy = y - (p.y + 0.14f);
        const float d = dx * dx + dy * dy;
        if (std::fabs(dx) < 0.10f && std::fabs(dy) < 0.20f && d < bd) { bd = d; best = &p; }
    }
    return best;
}

void dropPatient(Patient& p, Zone z)
{
    const bool fromEr = dragFrom == Zone::Emergency;
    char why[64];
    if (z == Zone::Waiting || z == Zone::Reception || z == Zone::None)
    {
        if (fromEr)
        {
            p.cooldown = ER_COOLDOWN;
            st.correct += 0;
            std::snprintf(why, sizeof(why), "#%d MOVED TO WAITING - ER LOCKED %.0fS", p.id, ER_COOLDOWN);
            sayS("%s", 2, why);
            popup(View::Inside, p.x, p.y + 0.35f, 2, "MOVED OUT OF ER");
        }
        else if (z != Zone::Waiting)
        {
            std::snprintf(why, sizeof(why), "#%d RETURNED TO A SEAT", p.id);
            sayS("%s", 3, why);
        }
        else
        {
            std::snprintf(why, sizeof(why), "#%d MOVED TO WAITING AREA", p.id);
            sayS("%s", 3, why);
        }
        backToSeat(p);
        return;
    }
    const Verdict v = tryPlace(p, z, true, why, sizeof(why));
    if (v == Verdict::Ok)
    {
        st.correct++;
        sayS("CORRECT: %s", 0, why);
        popup(View::Inside, p.x, p.y + 0.35f, 0, p.kind == Kind::Emergency ? "ASSIGNED TO EMERGENCY" : "ASSIGNED TO CONSULT");
        if (z == Zone::Emergency && fromEr == false) { /* fresh assignment */ }
        return;
    }
    if (v == Verdict::WrongArea) { st.wrong++; sayS("WRONG: %s", 1, why); popup(View::Inside, p.x, p.y + 0.35f, 1, "WRONG AREA - BACK TO SEAT"); }
    else                         { st.rejected++; sayS("REJECTED: %s", 2, why); popup(View::Inside, p.x, p.y + 0.35f, 2, v == Verdict::Cooldown ? "ER LOCKED - WAIT" : "NO SPACE"); }
    if (fromEr) p.cooldown = std::max(p.cooldown, ER_COOLDOWN);                 // it already left the ER
    backToSeat(p);
}

Action handleInput(View view, const Input& in)
{
    hoverP = -1; hoverPed = -1;
    if (in.valid)
    {
        if (view == View::Inside) { const Patient* p = pickPatient(in.x, in.y); if (p) hoverP = p->id; }
        else                      { const Pedestrian* p = pickPed(in.x, in.y);  if (p) hoverPed = p->id; }
    }

    if (view == View::Inside)
    {
        if (in.pressed && in.valid && dragP < 0)
        {
            if (in.x >= EXIT_X0 && in.x <= EXIT_X1 && in.y >= EXIT_Y0 && in.y <= EXIT_Y1) return Action::GoOutside;
            Patient* p = pickPatient(in.x, in.y);
            if (p)
            {
                dragP = p->id;
                dragFrom = p->zone;
                dragFromSlot = p->slot;
                freeSlot(*p);                       // leaves its bed / seat immediately
                p->st = PState::Dragged;
            }
        }
        if (dragP >= 0)
        {
            Patient* p = byId(dragP);
            if (!p) { dragP = -1; return Action::None; }
            p->x = in.x; p->y = in.y - 0.14f;
            if (!in.down || in.released)
            {
                dragP = -1;
                dropPatient(*p, zoneAt(in.x, in.y));
            }
        }
        return Action::None;
    }

    // ---- outside ----
    if (in.pressed && in.valid && dragPed < 0)
    {
        Pedestrian* p = pickPed(in.x, in.y);
        if (p && p->st != Pedestrian::ToDoor)
        {
            dragPed = p->id;
            p->st = Pedestrian::Dragged;
        }
        else if (!p && std::fabs(in.x - DOOR_X) < 0.14f && in.y > DOOR_Y - 0.12f && in.y < DOOR_Y + 0.40f)
            return Action::GoInside;
    }
    if (dragPed >= 0)
    {
        Pedestrian* d = nullptr;
        for (Pedestrian& p : peds) if (p.id == dragPed) d = &p;
        if (!d) { dragPed = -1; return Action::None; }
        d->x = in.x; d->y = in.y - 0.12;
        if (!in.down || in.released)
        {
            dragPed = -1;
            if (overHospital(in.x, in.y))
            {
                if (d->symptom >= ADMIT_THRESHOLD && insideCount() < MAX_INSIDE)
                {
                    d->st = Pedestrian::ToDoor;
                    d->seeksCare = false;
                    d->speed = 0.3f;
                    char buf[72];
                    std::snprintf(buf, sizeof(buf), "PERSON NEEDS CARE - ADMITTING");
                    sayS("%s", 0, buf);
                    popup(View::Outside, d->x, d->y + 0.40f, 0, "NEEDS CARE - ADMITTED");
                }
                else if (d->symptom >= ADMIT_THRESHOLD)
                {
                    st.turnedAway++;
                    sayD("HOSPITAL FULL - ADMISSION DELAYED", 2);
                    popup(View::Outside, d->x, d->y + 0.40f, 2, "HOSPITAL FULL");
                    d->st = Pedestrian::Walk; d->flash = 1.0f;
                }
                else
                {
                    st.turnedAway++;
                    sayD("CHECKED: NO MEDICAL NEED - NOT ADMITTED", 1);
                    popup(View::Outside, d->x, d->y + 0.40f, 1, "HEALTHY - NOT ADMITTED");
                    d->st = Pedestrian::Walk; d->flash = 1.0f;
                    d->dir = d->x < 0 ? -1.0f : 1.0f;
                }
            }
            else
            {
                d->st = Pedestrian::Walk;               // dropped elsewhere: carries on walking
                d->dir = d->x < 0 ? -1.0f : 1.0f;
                if (std::fabs(d->x) < 0.6f) d->dir = d->dir;
            }
        }
    }
    return Action::None;
}

}   // namespace

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------
float priorityOf(const Patient& p)
{
    return (p.kind == Kind::Emergency ? 300.0f : 0.0f) + p.severity * 50.0f + std::min(400.0f, p.waited);
}

const char* condName(Cond c)
{
    switch (c)
    {
    case Cond::Checkup:   return "CHECKUP";
    case Cond::Fever:     return "FEVER";
    case Cond::Cough:     return "COUGH";
    case Cond::Bleeding:  return "BLEEDING";
    case Cond::BrokenArm: return "BROKEN ARM";
    case Cond::BrokenLeg: return "BROKEN LEG";
    default:              return "NEEDS SURGERY";
    }
}
const char* kindName(Kind k) { return k == Kind::Emergency ? "EMERGENCY" : "GENERAL"; }

bool overHospital(float x, float y) { return x >= HOSP_X0 && x <= HOSP_X1 && y >= HOSP_Y0 && y <= HOSP_Y1; }

Zone zoneAt(float x, float y)
{
    if (y < -0.97f || y > ROOM_TOP) return Zone::None;
    if (x < ZONE_X[1]) return Zone::Reception;
    if (x < ZONE_X[2]) return Zone::Waiting;
    if (x < ZONE_X[3]) return Zone::Emergency;
    return Zone::Consult;
}

void init(unsigned seed) { seed0 = seed; reset(); }

void reset()
{
    rng.s = seed0 ? seed0 : 1u;
    pts.clear(); peds.clear(); ambs.clear(); pops.clear(); logv.clear();
    st = Stats();
    for (int& v : seatOcc) v = -1;
    for (int& v : erOcc) v = -1;
    for (int& v : consOcc) v = -1;
    nextId = 1; nextPedId = 1; hoverP = hoverPed = dragP = dragPed = -1;
    simTime = 0; ambTimer = 6.0; pedTimer = 0; triageTimer = 0; ambCount = 0;
    ambOn = true; pedOn = true;
    for (int i = 0; i < PEDESTRIANS; i++)                              // start with people already on the street
    {
        const float dir = (i & 1) ? 1.0f : -1.0f;
        spawnPed(rng.range(-1.7f, 1.7f), rollSymptom(), dir, false);
    }
    sayS("%s OPEN - AMBULANCES AND WALK-INS EXPECTED", 3, NAME);
}

Action update(double dtIn, View view, const Input& in)
{
    float dt = (float)dtIn;
    if (dt > 0.05f) dt = 0.05f;
    if (dt < 0.0f) dt = 0.0f;
    simTime += dt;

    Action act = handleInput(view, in);

    updatePatients(dt);
    promoteErQueue();
    updatePeds(dt);
    updateAmbulances(dt);

    ambTimer -= dt;
    if (ambOn && ambTimer <= 0.0f && ambs.empty() && insideCount() < MAX_INSIDE - 2)      // one bay: FIFO
    {
        startAmbulance();
        ambTimer = AMBULANCE_INTERVAL + rng.range(-AMBULANCE_JITTER, AMBULANCE_JITTER);
    }
    triageTimer += dt;
    if (triageTimer >= 1.0f) { triageTimer = 0; autoTriage(); }

    for (Popup& p : pops) p.age += dt;
    pops.erase(std::remove_if(pops.begin(), pops.end(), [](const Popup& p) { return p.age > 2.6f; }), pops.end());
    return act;
}

const std::vector<Patient>&    patients()    { return pts; }
const std::vector<Pedestrian>& pedestrians() { return peds; }
const std::vector<Ambulance>&  ambulances()  { return ambs; }
const std::vector<Popup>&      popups()      { return pops; }
const std::vector<LogLine>&    log()         { return logv; }
const Stats&                   stats()       { return st; }
int  hoverPatientId()     { return hoverP; }
int  hoverPedestrianId()  { return hoverPed; }
bool draggingSomething()  { return dragP >= 0 || dragPed >= 0; }
const Patient* findPatient(int id) { for (const Patient& p : pts) if (p.id == id) return &p; return nullptr; }
const Pedestrian* findPedestrian(int id) { for (const Pedestrian& p : peds) if (p.id == id) return &p; return nullptr; }
std::vector<int> triageOrder() { return triageIds(); }

bool seatPos(int i, float& x, float& y) { if (i < 0 || i >= SEATS) return false; x = SEAT_X[i]; y = SEAT_Y[i]; return true; }
void slotPos(Zone z, int slot, float& x, float& y) { slotXY(z, slot, x, y); }

int insideCount() { return (int)pts.size(); }
int waitingCount(Kind k) { int n = 0; for (const Patient& p : pts) if (p.kind == k && (p.st == PState::Seated || p.st == PState::Queued)) n++; return n; }
int treatingCount(Kind k) { int n = 0; for (const Patient& p : pts) if (p.kind == k && p.st == PState::Treating) n++; return n; }
int erQueueCount() { int n = 0; for (int i = ER_BEDS; i < ER_SLOTS; i++) if (erOcc[i] >= 0) n++; return n; }
int erBedsFree() { int n = 0; for (int i = 0; i < ER_BEDS; i++) if (erOcc[i] < 0) n++; return n; }
int consultFree() { int n = 0; for (int i = 0; i < CONSULT_SLOTS; i++) if (consOcc[i] < 0) n++; return n; }

void testSpawnPatient(Kind k, Cond c, int severity)
{
    createPatient(k == Kind::Emergency ? 0.95f : 0.6f, false, 0, 0, View::Inside);
    Patient& p = pts.back();
    p.kind = k; p.cond = c; p.severity = severity;
    p.treatTotal = p.treatLeft = treatTimeOf(c);
}
void testSpawnPedestrian(float x, float symptom, bool seeks) { spawnPed(x, symptom, 1.0f, false); peds.back().seeksCare = seeks; }
void testSpawnAmbulance() { startAmbulance(); }
void setAmbulancesEnabled(bool on) { ambOn = on; }
void setPedestriansEnabled(bool on) { pedOn = on; }
void testClearPedestrians() { peds.clear(); }

}   // namespace hospital
