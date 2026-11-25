// station_config.h - EVERYTHING you may want to tune about the train station lives here.
//
// Units: distances are world units (the screen is 3.56 x 2.0), time is seconds.
// The station name uses the built-in pixel font (A-Z, 0-9 and . , : - + / % ! ? ( ) = < > ' #).

#ifndef STATION_CONFIG_H
#define STATION_CONFIG_H

namespace stationcfg
{

constexpr const char* NAME = "Enchanted Central";   // <-- station name (building sign + stats bar)

// ---- timetable ---------------------------------------------------------------
constexpr float HEADWAY        = 15.0f;    // average seconds between planned arrivals
constexpr float HEADWAY_JITTER = 4.0f;     // +/- seconds (seeded, controlled randomness)
constexpr float DWELL          = 11.0f;    // planned seconds at the platform
constexpr float DWELL_JITTER   = 3.0f;
constexpr float MAX_EXTRA_DWELL = 6.0f;    // a train waits at most this long for late boarders
constexpr float CLEARANCE      = 3.0f;     // seconds between one train leaving and the next arriving
constexpr float CONFIRM_AHEAD  = 25.0f;    // a platform is confirmed this long before arrival (then only delays)

// ---- trains (kinematics) -----------------------------------------------------
constexpr float V_MAX          = 0.90f;    // cruise speed (u/s)
constexpr float ACCEL          = 0.35f;    // u/s^2
constexpr float BRAKE          = 0.45f;    // service braking (u/s^2); stops use v^2 = 2 a d
constexpr int   CAPACITY       = 60;       // passengers per train

// ---- passengers ----------------------------------------------------------------
constexpr float ARRIVAL_MEAN   = 2.4f;     // mean seconds between passengers (Poisson process)
constexpr int   MAX_PASSENGERS = 38;       // on the station at the same time
constexpr float WALK_SPEED     = 0.20f;    // on the flat; stairs cost STAIR_FACTOR times longer
constexpr float STAIR_FACTOR   = 1.8f;
constexpr float BRIDGE_FACTOR  = 1.1f;
constexpr float BOARD_TIME     = 0.45f;    // seconds per passenger per door

// ---- dispatcher controls + score (shared game score, see game.h) ----------------
constexpr float HOLD_STEP      = 10.0f;    // HOLD adds this many seconds to the stop
constexpr float DRAG_SPEED     = 1.4f;     // u/s: fastest you can pull a train along the track
constexpr int SCORE_DEPART_OK    = +3;     // left on time with everyone on board
constexpr int SCORE_DEPART_LATE  = +1;     // everyone on board, but late
constexpr int SCORE_LEFT_BEHIND  = -1;     // per passenger left on the platform (max 4)
constexpr int SCORE_STUCK_ABOARD = -1;     // per passenger who could not get off (max 3)
constexpr int SCORE_HELP_BOARD   = +2;     // you put a passenger on the right train
constexpr int SCORE_WRONG_TRAIN  = -2;     // ... on the wrong train
constexpr int SCORE_LATE_ARRIVAL = -1;     // a train arrived more than 10 s late
constexpr int SCORE_EARLY_ARRIVAL = +1;    // you brought a train in early

}   // namespace stationcfg

#endif
