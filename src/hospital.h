// hospital.h - drawing of the two hospital scenes (the rules live in hospitalsim.cpp)

#ifndef HOSPITAL_H
#define HOSPITAL_H

#include "hospitalsim.h"

namespace hospital
{

void draw(View view, float time);        // the scene itself (follows the time-of-day tint)
void drawOverlay(View view);             // statistics, event feed, tooltips, pop-ups, EXIT button (never tinted)

}   // namespace hospital

#endif
