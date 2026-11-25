#ifndef MOUNTAIN_H
#define MOUNTAIN_H

#include "animation.h"

void initMountain();                          // build ridges / cliffs once (object reuse)
void drawMountainScene(const AnimState& s);   // sky, ranges, valley, village, foreground mountain
const char* villageHubHover();                // village hub: name of the place under the mouse (last frame), or nullptr

#endif
