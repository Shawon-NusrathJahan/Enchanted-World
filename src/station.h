// station.h - drawing of the train station scene (the rules live in stationsim.cpp)

#ifndef STATION_H
#define STATION_H

#include "stationsim.h"

namespace station
{

void draw(float time);       // the scene itself (follows the time-of-day tint)
void drawOverlay();          // statistics, departure board, routes, event feed, tooltips (never tinted)

}   // namespace station

#endif
