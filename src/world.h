// world.h - ONE layout of the whole world, shared by the opening film (the village on its
//           plateau) and the world map, so every place on the map is where it really is.
//
// Coordinates are plateau-layer units of the opening film (mountain.cpp, layer f = 0.55):
// x grows to the right, y grows "away" from the viewer (up the screen). The village is seen
// from an elevated aerial angle, so objects further away (higher y) are drawn smaller.

#ifndef WORLD_H
#define WORLD_H

#include "cgmath.h"

namespace world
{
// the visible part of the plateau when the village is revealed
constexpr float VIEW_X0 = -0.95f, VIEW_X1 = 0.95f, VIEW_Y0 = -0.84f, VIEW_Y1 = 0.05f;
constexpr float HORIZON_Y = 0.0f;                     // where the back hills meet the meadow

// water: mountain stream -> rock ledge waterfall -> the big pond -> river to the bottom edge
constexpr float LAKE_X = -0.43f, LAKE_Y = -0.31f, LAKE_RX = 0.29f, LAKE_RY = 0.085f;
constexpr float STREAM_X0 = -0.822f, STREAM_Y0 = 0.050f;   // hilltop, right under the mountain cascade
constexpr float FALL_TOP_X = -0.715f, FALL_TOP_Y = -0.115f; // lip of the rock ledge
constexpr float FALL_BOT_X = -0.645f, FALL_BOT_Y = -0.275f; // plunge pool (inside the pond)

// the places you can visit (ground point in front of each)
constexpr float COTTAGE_X = -0.03f, COTTAGE_Y = -0.335f;   // the Garden: the cottage beside the pond
constexpr float PLAY_X = 0.06f, PLAY_Y = -0.66f;           // playground (front of the village)
constexpr float STATION_X = 0.56f, STATION_Y = -0.085f;    // railway station (back of the village)
constexpr float RAIL_Y = -0.105f;                          // the railway line runs left-right
constexpr float HOSP_X = 0.76f, HOSP_Y = -0.48f;           // hospital (right side)
constexpr float FAIR_X = 0.40f, FAIR_Y = -0.33f, FAIR_RX = 0.31f, FAIR_RY = 0.135f;  // the village fair (centre)
constexpr float MARKET_X = 0.04f, MARKET_Y = -0.49f;       // marketplace (stalls + shops)
constexpr float HALL_X = 0.68f, HALL_Y = -0.19f;           // the great clock tower (village landmark)

// every establishment of the village; the non-visitable ones are future scenes
enum class PlaceKind { Garden, Playground, Station, Hospital, ClockTower, Fair, Market, Police, Detective, FireStation, School, Library, PostOffice };
struct Place { PlaceKind kind; const char* name; float x, y; bool visitable; };
constexpr Place PLACES[] = {
    {PlaceKind::Garden,      "The Garden",        COTTAGE_X, COTTAGE_Y, true},
    {PlaceKind::Playground,  "Playground",        PLAY_X,    PLAY_Y,    true},
    {PlaceKind::Station,     "Railway Station",   STATION_X, STATION_Y, true},
    {PlaceKind::Hospital,    "Hospital",          HOSP_X,    HOSP_Y,    true},
    {PlaceKind::ClockTower,  "Clock Tower",       HALL_X,    HALL_Y,    false},
    {PlaceKind::Fair,        "Village Fair",      FAIR_X,    FAIR_Y,    false},
    {PlaceKind::Market,      "Marketplace",       MARKET_X,  MARKET_Y,  false},
    {PlaceKind::Police,      "Police Station",    0.88f,     -0.28f,    false},
    {PlaceKind::Detective,   "Detective Agency",  0.56f,     -0.67f,    false},
    {PlaceKind::FireStation, "Fire Station",      0.90f,     -0.11f,    false},
    {PlaceKind::School,      "School",            -0.24f,    -0.60f,    false},
    {PlaceKind::Library,     "Library",           0.10f,     -0.19f,    false},
    {PlaceKind::PostOffice,  "Post Office",       0.23f,     -0.13f,    false},
};
constexpr int PLACE_COUNT = (int)(sizeof(PLACES) / sizeof(PLACES[0]));
inline const Place& place(PlaceKind k) { for (const Place& p : PLACES) if (p.kind == k) return p; return PLACES[0]; }

// aerial perspective: objects closer to the viewer (lower y) are larger
inline float depthScale(float y) { return 0.62f - 0.80f * y; }

// the stream from the hills to the ledge (cubic Bezier) and the river below the pond
constexpr float STREAM_C1X = -0.835f, STREAM_C1Y = -0.030f, STREAM_C2X = -0.70f, STREAM_C2Y = -0.055f;
inline cg::V2 streamAt(float u) { return cg::cubicBezier(cg::V2(STREAM_X0, STREAM_Y0), cg::V2(STREAM_C1X, STREAM_C1Y), cg::V2(STREAM_C2X, STREAM_C2Y), cg::V2(FALL_TOP_X, FALL_TOP_Y), u); }
inline cg::V2 riverAt(float u)  { return cg::cubicBezier(cg::V2(-0.52f, -0.37f), cg::V2(-0.60f, -0.50f), cg::V2(-0.84f, -0.62f), cg::V2(-0.90f, -1.05f), u); }
inline float  riverW(float u)   { return 0.022f + 0.075f * u * u; }

}   // namespace world

#endif
