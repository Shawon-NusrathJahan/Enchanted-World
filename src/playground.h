// playground.h - drawing of the playground scene (the rules and physics live in playgroundsim.cpp)

#ifndef PLAYGROUND_H
#define PLAYGROUND_H

#include "playgroundsim.h"

namespace playground
{

void draw(float time);       // the scene itself (follows the time-of-day tint)
void drawOverlay();          // statistics, physics board, event feed, tooltips, pop-ups (never tinted)

}   // namespace playground

#endif
