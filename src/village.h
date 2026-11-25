#ifndef VILLAGE_H
#define VILLAGE_H

#include "cgmath.h"
#include "world.h"
#include <vector>

void initVillage();

// The village seen from an elevated aerial angle (layout in world.h, plateau-layer coordinates;
// the caller has already applied the layer/camera transform).
void drawVillageGround();                                           // static: roads, square, fields, railway
void drawVillageLive(double time, bool still, float mouseX, float mouseY);   // buildings, people, train (sorted)
const char* villageHover(float x, float y);                         // name of the place under (x, y) or nullptr

// the same layout for the world map
struct VillageMap
{
    std::vector<cg::V2> houses, shops, trees;
    std::vector<std::vector<cg::V2>> roads;
};
const VillageMap& villageMap();

// a small illustrated scene of one place for the world map (same art as the village)
// x, y = centre of its ground; s = scale (1 = about 0.30 wide); full = rich vignette, else a small icon
void drawPlaceVignette(world::PlaceKind k, float x, float y, float s, float t, bool full);

// One cottage. x,y = centre of the ground line, w = width, style = 0..3.
// Reused by the garden so the village house "becomes" the garden cottage.
void drawHouse(float x, float y, float w, int style, bool smoke, double time);

#endif
