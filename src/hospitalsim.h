// hospitalsim.h - the hospital SIMULATION (no drawing, no OpenGL). hospital.cpp draws it.
//
// Algorithms (explain these to the supervisor):
//   Triage        : max-priority queue (binary heap, std::priority_queue) with AGING.
//                   priority = kind weight + severity * 50 + seconds waited
//                   -> emergencies first, sicker first, nobody starves.
//   Queue mgmt    : ER bed list + bounded ER waiting queue; a freed bed is given to the
//                   head of the priority queue automatically.
//   Assignment    : rule-based placement check (patient kind x treatment area x capacity x
//                   cooldown). Wrong or impossible placements bounce the patient back to a seat.
//   Cooldown      : a patient pulled out of the ER is locked out for ER_COOLDOWN seconds.
//   Auto triage   : if the user leaves someone waiting AUTO_TRIAGE_DELAY seconds the nurse
//                   takes the top of the priority queue and places it (same rules as the user).
//   Ambulances    : finite state machine (Approach, Unload, Hold, Leave) started by a
//                   seeded random timer ("controlled randomness"), one bay, FIFO.
//   Admission     : hidden symptom score vs ADMIT_THRESHOLD decides if a dragged
//                   pedestrian really needs care.

#ifndef HOSPITALSIM_H
#define HOSPITALSIM_H

#include "hospital_config.h"

#include <string>
#include <vector>

namespace hospital
{

enum class View   { Outside, Inside };
enum class Kind   { General, Emergency };
enum class Cond   { Checkup, Fever, Cough, Bleeding, BrokenArm, BrokenLeg, Surgery };
enum class Zone   { None, Reception, Waiting, Emergency, Consult };
enum class PState { CheckIn, Seated, Queued, Walking, Treating, Leaving, Dragged };
enum class Action { None, GoInside, GoOutside };

struct Input { float x = 0, y = 0; bool valid = false, down = false, pressed = false, released = false; };

// ---- geometry shared by simulation and drawing (world units, y up) ---------------
constexpr int SEATS = 12, ER_BEDS = 2, ER_QUEUE = 3, ER_SLOTS = ER_BEDS + ER_QUEUE, CONSULT_SLOTS = 2;

constexpr float ROOM_TOP = 0.78f;                     // interior: rooms span y -1 .. ROOM_TOP
constexpr float ZONE_X[5] = {-1.78f, -0.95f, 0.15f, 0.98f, 1.78f};   // Reception | Waiting | ER | Consult
constexpr float ENTRY_X = -1.74f, ENTRY_Y = -0.58f;   // interior door (patients enter and leave here)
constexpr float DESK_X = -1.30f, DESK_Y = -0.58f;     // reception check-in spot

// outside
constexpr float DOOR_X = 0.0f, DOOR_Y = -0.44f;       // front door (feet position in front of it)
constexpr float SIDEWALK_Y0 = -0.64f, SIDEWALK_Y1 = -0.52f;
constexpr float ROAD_Y = -0.80f;
constexpr float BUILD_BASE_Y = -0.36f;
constexpr float HOSP_X0 = -1.05f, HOSP_X1 = 1.05f, HOSP_Y0 = -0.46f, HOSP_Y1 = 0.80f;   // "dropped on the hospital"

// EXIT button (interior) - drawn and clicked with these numbers
constexpr float EXIT_X0 = -1.52f, EXIT_Y0 = 0.83f, EXIT_X1 = -1.20f, EXIT_Y1 = 0.94f;

struct Patient
{
    int   id = 0;
    Kind  kind = Kind::General;
    Cond  cond = Cond::Checkup;
    int   severity = 1;                  // 1..5
    float x = 0, y = 0, tx = 0, ty = 0;  // position / walking target
    PState st = PState::CheckIn;
    Zone  zone = Zone::None;             // reserved slot (valid when slot >= 0)
    int   slot = -1;
    float waited = 0;                    // seconds spent waiting (feeds the priority)
    float cooldown = 0;                  // ER lock-out remaining
    float treatTotal = 1, treatLeft = 1; // treatment progress is kept when moved
    float checkT = 0, walkPhase = 0, flash = 0, worsenT = 0;
    int   skin = 0, hair = 0, shirt = 0;
    bool  fromAmbulance = false;
};

struct Pedestrian
{
    enum St { Walk, Dragged, ToDoor, Entering };
    int   id = 0;
    float x = 0, y = 0, dir = 1, speed = 0.15f, lane = -0.64f;
    float symptom = 0;                   // HIDDEN: 0 healthy .. 1 critical
    St    st = Walk;
    float alpha = 1, phase = 0;
    int   skin = 0, hair = 0, shirt = 0;
    bool  fromAmbulance = false, seeksCare = false, rejectedFlash = false;
    float flash = 0;
};

struct Ambulance
{
    enum St { Approach, Unload, Hold, Leave };
    float x = -2.4f, dir = 1, stopX = 0.45f, t = 0, wheel = 0, siren = 0;
    St    st = Approach;
    int   cargo = 0, unloaded = 0;
    bool  active = true;
};

struct Popup { float x = 0, y = 0, age = 0; char text[48] = ""; int color = 0; View view = View::Outside; };   // color: 0 green 1 red 2 yellow 3 white

struct LogLine { char text[120] = ""; int color = 0; };

struct Stats
{
    int admitted = 0, admittedEmergency = 0, admittedGeneral = 0;
    int released = 0, turnedAway = 0;
    int correct = 0, wrong = 0, rejected = 0, autoAssigned = 0;
    int ambulances = 0;
};

void init(unsigned seed = 12345u);
void reset();
Action update(double dt, View view, const Input& in);

// ---- read-only state for drawing / HUD / tests ----
const std::vector<Patient>&    patients();
const std::vector<Pedestrian>& pedestrians();
const std::vector<Ambulance>&  ambulances();
const std::vector<Popup>&      popups();
const std::vector<LogLine>&    log();          // newest last
const Stats&                   stats();
bool  seatPos(int i, float& x, float& y);
void  slotPos(Zone z, int slot, float& x, float& y);
int   insideCount();
int   waitingCount(Kind k);                    // seated + queued
int   treatingCount(Kind k);
int   erQueueCount();
int   erBedsFree();
int   consultFree();
int   hoverPatientId();                        // id under the cursor (-1 none)
int   hoverPedestrianId();
const Patient*    findPatient(int id);
const Pedestrian* findPedestrian(int id);
std::vector<int>  triageOrder();               // waiting patient ids, highest priority first
float priorityOf(const Patient& p);
const char* condName(Cond c);
const char* kindName(Kind k);
bool  draggingSomething();
bool  overHospital(float x, float y);          // outside: is this point "on the hospital"?
Zone  zoneAt(float x, float y);                // inside: which area is under this point?

// test helpers
void  testSpawnPatient(Kind k, Cond c, int severity);
void  testSpawnPedestrian(float x, float symptom, bool seeksCare);
void  testSpawnAmbulance();
void  setAmbulancesEnabled(bool on);
void  setPedestriansEnabled(bool on);
void  testClearPedestrians();

}   // namespace hospital

#endif
