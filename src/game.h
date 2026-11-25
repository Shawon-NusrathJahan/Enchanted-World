// game.h - the GAME SESSION: score, gameplay events and cross-scene consequences
//
// Everything here lives in memory for as long as the program runs. Changing
// scene, or pressing the restart button (which only replays the opening film),
// never resets it. Only closing and starting the application again does.

#ifndef GAME_H
#define GAME_H

#include "gfx.h"

#include <vector>

namespace game
{

enum class Where { Garden = 0, Playground, Station, Hospital, COUNT };

struct Event
{
    char  text[96] = "";
    int   delta = 0;          // score change (0 = information only)
    Where where = Where::Garden;
    float age = 0.0f;         // seconds since it happened
};

// a child hurt in the playground, waiting to be treated in the hospital
struct Injury
{
    int  kidId = 0;
    int  cond = 0;            // hospital::Cond value (BrokenArm, BrokenLeg, Bleeding ...)
    int  severity = 3;
    char what[32] = "";
};

void init();                                                     // once, when the program starts
void update(double dt);                                          // every frame (animations only)

int  score();
int  scoreAt(Where w);                                           // what one location contributed
void add(Where w, int delta, const char* fmt, ...);               // gameplay event (+/- score)
void addAt(Where w, int delta, float x, float y, const char* fmt, ...);   // same + flying "+N" from (x, y)
const std::vector<Event>& events();                              // oldest first, newest last
int  restarts();
void countRestart();

// cross-scene consequences
void sendToHospital(const Injury& inj);                          // playground -> hospital
bool takeInjured(Injury& out);                                   // hospital pulls the next one
int  injuredWaiting();
void kidHealed(int kidId);                                       // hospital -> playground
bool takeHealed(int& kidId);

// ---- state used by the HUD (game_hud.cpp) ----
struct Token { float x0, y0, cx, cy, t; int delta; };          // flying "+N": quadratic Bezier to the score
constexpr float HUD_X = 0.0f, HUD_Y = 0.905f;
float shownScore();
float pulse();
int   lastDelta();
float lastDeltaAge();
const std::vector<Token>& tokens();

// HUD: a compact score panel at the top centre + the latest events at the bottom.
// `stat` is one short line of the most important numbers of the current scene.
void drawHud(Where w, const char* title, const char* stat);
void drawFeed();
void scorePos(float& x, float& y);                               // where the score number sits

}   // namespace game

#endif
