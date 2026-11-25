// scenes.h - which location is on screen, the village hub, the corner buttons
//
// Intro  = mountain -> village -> garden animation (driven by animation.cpp)
// Garden = the garden after the intro
// The VILLAGE itself is the navigation hub: after the mountains part the film waits there and
// you click a place to visit it. The MAP button brings you back to the village from anywhere.

#ifndef SCENES_H
#define SCENES_H

#include "animation.h"

enum class SceneId { Intro, Garden, Playground, Station, HospitalOutside, HospitalInside };

namespace scenes
{

extern SceneId current;

bool        isIntroOrGarden();                         // drawn by the original mountain/garden pipeline
void        restartFromBeginning(double now);          // back button: whole film from t = 0
void        goToVillage(double now);                   // MAP button: back to the village hub
bool        atVillageHub(const AnimState& s);          // the village hub is on screen
bool        hubClick(const char* placeName, double now);   // a place was clicked in the hub (travel or "under construction")
void        updateLocation(double dt);                 // advances the active location's simulation (playground, station, hospital)
void        drawLocation(const AnimState& s);          // every scene except Intro / Garden
void        drawUI(const AnimState& s, double now);    // buttons, pop-up, clock label (also handles clicks)
const char* name(SceneId id);

}   // namespace scenes

#endif
