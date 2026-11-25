// scenes.h - which location is on screen, the illustrated map, the corner buttons
//
// Intro  = mountain -> village -> garden animation (driven by animation.cpp)
// Garden = the garden after the intro (default location)
// the other locations are reached through the map.

#ifndef SCENES_H
#define SCENES_H

#include "animation.h"

enum class SceneId { Intro, Garden, Playground, Station, HospitalOutside, HospitalInside };

namespace scenes
{

extern SceneId current;
extern bool    mapOpen;

bool        isIntroOrGarden();                         // drawn by the original mountain/garden pipeline
void        restartFromBeginning(double now);          // back button: whole film from t = 0
void        updateLocation(double dt);                 // advances the active location's simulation (playground, station, hospital)
void        drawLocation(const AnimState& s);          // every scene except Intro / Garden
void        drawUI(const AnimState& s, double now);    // buttons, map, clock label (also handles clicks)
const char* name(SceneId id);

}   // namespace scenes

#endif
