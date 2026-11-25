// playground_config.h - EVERYTHING you may want to tune about the playground lives here.
//
// Units: distances are world units (the screen is 3.56 x 2.0), time is seconds,
// masses are kilograms. G is the "world gravity" chosen so a 0.52-long swing
// has a natural period of about 2.4 s (T = 2*pi*sqrt(L/G)).

#ifndef PLAYGROUND_CONFIG_H
#define PLAYGROUND_CONFIG_H

namespace playgroundcfg
{

constexpr const char* NAME = "Sunny Playground";   // <-- shown in the stats bar

constexpr int   KIDS          = 9;        // children in the playground
constexpr float KID_MASS_MIN  = 20.0f;    // kg (seeded random per child)
constexpr float KID_MASS_MAX  = 45.0f;
constexpr float WALK_SPEED    = 0.22f;
constexpr float RUN_SPEED     = 0.40f;

constexpr float G             = 3.6f;     // world gravity (world units / s^2)

// ---- scheduling -------------------------------------------------------------
constexpr float RIDE_TIME     = 12.0f;    // round-robin time quantum on a swing / see-saw
constexpr float PATIENCE      = 40.0f;    // a child will not join a queue with a longer expected wait
constexpr float REPEAT_PENALTY = 15.0f;   // extra "seconds" added to the ride a child just used

// ---- swings: damped driven pendulum  theta'' = -(G/L) sin(theta) - c theta' + pump ----
constexpr float SWING_LENGTH  = 0.52f;
constexpr float SWING_DAMPING = 0.10f;    // c (1/s)
constexpr float SWING_PUMP    = 0.90f;    // rider's leg pumping (rad/s^2), only below PUMP_LIMIT
constexpr float SWING_PUMP_LIMIT = 0.70f; // rad: a child pumps up to ~40 degrees
constexpr float SWING_PUSH    = 1.10f;    // angular-velocity impulse of one user push (rad/s)
constexpr float SWING_DANGER_AMP = 1.05f; // rad (60 deg): the child gets scared (red warning)
constexpr float SWING_FALL_AMP   = 1.31f; // rad (75 deg): the child loses grip at the top and FALLS

// ---- see-saw: rigid beam, torque balance  I*phi'' = G cos(phi) (mL dL - mR dR) ----
constexpr float SEESAW_HALF        = 0.36f;
constexpr float SEESAW_BEAM_I      = 1.0f;     // beam moment of inertia (kg * u^2)
constexpr float SEESAW_DAMPING     = 0.35f;
constexpr float SEESAW_RESTITUTION = 0.35f;    // bounce when an end hits the ground
constexpr float SEESAW_LEG_PUSH    = 1.30f;    // a child pushes off the ground: the beam leaves at this speed (rad/s)
constexpr float SEESAW_USER_PUSH   = 0.90f;
constexpr float SEESAW_MIN_ARM     = 0.14f;    // closest seat to the pivot

// ---- slide: a = G (sin a - mu cos a) ------------------------------------------
constexpr float SLIDE_FRICTION  = 0.25f;  // mu on the slide
constexpr float GROUND_FRICTION = 0.90f;  // mu when sliding out on the ground
constexpr float CLIMB_SPEED     = 0.20f;

// ---- consequences ---------------------------------------------------------------
constexpr float SEESAW_SLAM      = 2.3f;   // rad/s: hitting the ground faster than this bumps the child
constexpr float WAX_TIME         = 14.0f;  // seconds the slide stays waxed after a click
constexpr float WAX_FRICTION     = 0.03f;  // mu of a waxed slide
constexpr float SLIDE_SAFE_SPEED = 1.45f;  // faster than this at the bottom -> the child flies off the end
constexpr float BALL_BONK_SPEED  = 1.5f;   // a ball faster than this makes a child cry ...
constexpr float BALL_HURT_SPEED  = 2.3f;   // ... and faster than this injures them
constexpr float HOSPITAL_DELAY   = 3.0f;   // seconds an injured child cries before being taken away

// score (session score, see game.h)
constexpr int SCORE_RIDE   = 2;            // a child enjoyed a ride
constexpr int SCORE_GOAL   = 10;           // you scored
constexpr int SCORE_KIDGOAL= 3;            // the children scored
constexpr int SCORE_SAVED  = 4;            // the safety mat caught a fall
constexpr int SCORE_SUPER  = 6;            // a super-fast slide landed safely
constexpr int SCORE_RETURN = 5;            // a healed child came back
constexpr int SCORE_FALL   = -15;          // a child fell off a swing and got hurt
constexpr int SCORE_CRASH  = -12;          // a child flew off the slide / was hit hard by the ball
constexpr int SCORE_BONK   = -6;           // ball hit a child, it cries
constexpr int SCORE_BUMP   = -4;           // see-saw slammed into the ground

// ---- ball: projectile + bounces ------------------------------------------------
constexpr float BALL_RESTITUTION = 0.62f;
constexpr float BALL_ROLL_FRICTION = 0.55f;
constexpr float BALL_KICK        = 1.20f;     // user kick speed
constexpr float BALL_MAX_THROW   = 3.2f;      // fastest throw (drag and release)
constexpr float GOAL_H           = 0.13f;     // goal mouth height
constexpr int   BALL_MAX_PLAYERS = 3;
constexpr float BALL_TIME        = 14.0f;     // how long a child plays ball

}   // namespace playgroundcfg

#endif
