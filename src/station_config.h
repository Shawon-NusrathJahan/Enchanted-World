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

}   // namespace stationcfg

#endif
