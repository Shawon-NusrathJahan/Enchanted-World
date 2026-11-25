#include "scenes.h"

#include "gfx.h"
#include "game.h"
#include "gardensim.h"
#include "hospital.h"
#include "playground.h"
#include "station.h"
#include "theme.h"
#include "ui.h"
#include "village.h"
#include "world.h"

#include <cmath>
#include <cstdio>

using namespace gfx;

SceneId scenes::current = SceneId::Intro;
bool    scenes::mapOpen = false;

namespace scenes
{
namespace
{

// ---- the world map: an illustrated, top-down view of the SAME layout the film shows ----
// Map transform (world.h plateau coordinates -> map panel): one uniform scale about the centre
// of the visitable places, so they sit in the middle of the map and every distance and road
// between them keeps its real proportions:  M(p) = k * (p - c)
const float PANEL_X = 1.50f, PANEL_Y = 0.84f;
const float MAP_K = 1.45f, MAP_CX = 0.365f, MAP_CY = -0.37f;
inline float mapX(float x) { return MAP_K * (x - MAP_CX); }
inline float mapY(float y) { return MAP_K * (y - MAP_CY); }

struct Node { SceneId id; world::PlaceKind kind; float wx, wy; const char* label; float x() const { return mapX(wx); } float y() const { return mapY(wy); } };
const Node NODES[4] = {
    {SceneId::Garden,          world::PlaceKind::Garden,     world::COTTAGE_X, world::COTTAGE_Y, "THE GARDEN"},
    {SceneId::Playground,      world::PlaceKind::Playground, world::PLAY_X,    world::PLAY_Y,    "PLAYGROUND"},
    {SceneId::Station,         world::PlaceKind::Station,    world::STATION_X, world::STATION_Y, "TRAIN STATION"},
    {SceneId::HospitalOutside, world::PlaceKind::Hospital,   world::HOSP_X,    world::HOSP_Y,    hospitalcfg::NAME},
};
const float VIGNETTE = 1.15f;                       // size of the visitable places' illustrations
const world::Place* hoverFuture = nullptr;          // future place under the mouse (label drawn last)

bool isHere(SceneId id)
{
    if (id == SceneId::Garden)          return current == SceneId::Garden || current == SceneId::Intro;
    if (id == SceneId::HospitalOutside) return current == SceneId::HospitalOutside || current == SceneId::HospitalInside;
    return current == id;
}

// ---------------------------------------------------------------------------
// Map artwork
// ---------------------------------------------------------------------------
// a little map house: wall + roof (one of three roof colours)
void mapHouse(float x, float y, float s, int k)
{
    const Color roofs[3] = {rgb(0.78f, 0.26f, 0.20f), rgb(0.86f, 0.46f, 0.22f), rgb(0.42f, 0.36f, 0.62f)};
    setColor(rgb(0, 0, 0, 0.18f)); ellipse(x + 0.006f, y - 0.004f, 0.026f * s, 0.008f * s, 10);
    setColor(rgb(0.98f, 0.92f, 0.80f)); rect(x - 0.020f * s, y, x + 0.020f * s, y + 0.024f * s);
    setColor(roofs[k % 3]); tri(x - 0.027f * s, y + 0.022f * s, x + 0.027f * s, y + 0.022f * s, x, y + 0.046f * s);
    setColor(rgb(0.40f, 0.26f, 0.16f)); rect(x - 0.004f * s, y, x + 0.004f * s, y + 0.012f * s);
}

void mapTree(float x, float y, float s, bool pine)
{
    setColor(rgb(0, 0, 0, 0.15f)); ellipse(x + 0.006f, y - 0.004f, 0.020f * s, 0.006f * s, 8);
    if (pine) { setColor(rgb(0.16f, 0.42f, 0.28f)); tri(x - 0.018f * s, y, x + 0.018f * s, y, x, y + 0.05f * s); }
    else { setColor(rgb(0.40f, 0.26f, 0.16f)); rect(x - 0.003f * s, y, x + 0.003f * s, y + 0.014f * s);
           setColor(rgb(0.24f, 0.56f, 0.28f)); circle(x, y + 0.024f * s, 0.016f * s, 12); setColor(rgb(0.40f, 0.70f, 0.34f)); circle(x - 0.005f * s, y + 0.029f * s, 0.008f * s, 8); }
}

void drawMapPanel(float t)
{
    hoverFuture = nullptr;
    // dim the scene behind
    glColor4f(0.0f, 0.0f, 0.0f, 0.60f);
    rect(-WORLD_W, -1.0f, WORLD_W, 1.0f);

    // parchment with a wooden frame
    setColor(rgb(0.0f, 0.0f, 0.0f, 0.35f)); rect(-PANEL_X + 0.03f, -PANEL_Y - 0.04f, PANEL_X + 0.03f, PANEL_Y - 0.04f);
    setColor(rgb(0.36f, 0.22f, 0.12f));      rect(-PANEL_X - 0.035f, -PANEL_Y - 0.035f, PANEL_X + 0.035f, PANEL_Y + 0.035f);
    setColor(rgb(0.52f, 0.34f, 0.18f));      rect(-PANEL_X - 0.020f, -PANEL_Y - 0.020f, PANEL_X + 0.020f, PANEL_Y + 0.020f);
    rectV(-PANEL_X, -PANEL_Y, PANEL_X, PANEL_Y, rgb(0.90f, 0.83f, 0.64f), rgb(0.97f, 0.92f, 0.77f));

    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    const float sx = vp[2] / (2.0f * WORLD_W), sy = vp[3] / 2.0f;
    glEnable(GL_SCISSOR_TEST);
    glScissor(vp[0] + (int)((-PANEL_X + WORLD_W) * sx), vp[1] + (int)((-PANEL_Y + 1.0f) * sy), (int)(2 * PANEL_X * sx), (int)(2 * PANEL_Y * sy));

    // land: soft meadow with rolling light patches
    const float horizon = mapY(0.04f);
    rectV(-PANEL_X, -PANEL_Y, PANEL_X, horizon, rgb(0.62f, 0.80f, 0.46f), rgb(0.74f, 0.86f, 0.56f));
    for (int k = 0; k < 46; k++)
    {
        setColor(k & 1 ? rgb(0.80f, 0.91f, 0.62f, 0.55f) : rgb(0.56f, 0.74f, 0.42f, 0.45f));
        ellipse(-1.5f + 3.0f * hash1(k * 5.3f), horizon - 0.05f - 1.5f * hash1(k * 8.9f), 0.08f + 0.12f * hash1(k * 2.2f), 0.03f + 0.04f * hash1(k * 3.3f), 16);
    }
    // mountains along the top
    for (int k = 0; k < 15; k++)
    {
        const float x = -1.62f + k * 0.235f + 0.05f * hash1(k * 4.4f), h = 0.16f + 0.12f * hash1(k * 2.7f), w = 0.15f + 0.05f * hash1(k * 9.9f);
        const float b = horizon - 0.01f * hash1(k * 1.3f);
        setColor(rgb(0.60f, 0.66f, 0.84f)); tri(x - w, b, x, b, x, b + h);
        setColor(rgb(0.44f, 0.50f, 0.72f)); tri(x, b, x + w, b, x, b + h);
        setColor(rgb(1.0f, 1.0f, 1.0f));     tri(x - w * 0.30f, b + h * 0.70f, x + w * 0.30f, b + h * 0.70f, x, b + h);
    }
    // forests at the sides (the edges of the world)
    auto forest = [&](float cx, float cy, float rx, float ry, int n, uint32_t seed)
    {
        for (int k = 0; k < n; k++)
        {
            const float a = 6.2832f * cg::hashU(seed + k * 3u), r = std::sqrt(cg::hashU(seed + k * 7u));
            const float x = cx + std::cos(a) * rx * r, y = cy + std::sin(a) * ry * r, h = 0.035f + 0.02f * cg::hashU(seed + k);
            setColor(rgb(0, 0, 0, 0.12f)); ellipse(x + 0.006f, y - 0.004f, h * 0.45f, h * 0.12f, 8);
            setColor(k & 1 ? rgb(0.20f, 0.46f, 0.30f) : rgb(0.26f, 0.54f, 0.32f)); tri(x - h * 0.42f, y, x + h * 0.42f, y, x, y + h * 1.25f);
        }
    };
    forest(1.32f, 0.10f, 0.20f, 0.30f, 50, 11u);
    forest(1.28f, -0.62f, 0.24f, 0.18f, 40, 23u);
    forest(-0.95f, -0.72f, 0.30f, 0.12f, 34, 37u);
    forest(0.00f, horizon - 0.04f, 1.6f, 0.03f, 70, 41u);

    // water at the left side: named, but kept simple (not a destination yet)
    {
        glBegin(GL_TRIANGLE_FAN);                                       // the pond: soft flat shape
        vtx(rgb(0.52f, 0.76f, 0.94f), mapX(world::LAKE_X), mapY(world::LAKE_Y));
        for (int i = 0; i <= 40; i++) { const float a = 6.2832f * i / 40.0f; vtx(rgb(0.46f, 0.72f, 0.92f), mapX(world::LAKE_X) + std::cos(a) * MAP_K * world::LAKE_RX, mapY(world::LAKE_Y) + std::sin(a) * MAP_K * world::LAKE_RY * 1.5f); }
        glEnd();
        glBegin(GL_TRIANGLE_STRIP);                                     // the river: a plain ribbon to the edge
        for (int i = 0; i <= 24; i++) { const float u = i / 24.0f; const cg::V2 p = world::riverAt(u); const float w = 0.6f * MAP_K * world::riverW(u);
            vtx(rgb(0.46f, 0.72f, 0.92f), mapX(p.x) - w, mapY(p.y)); vtx(rgb(0.46f, 0.72f, 0.92f), mapX(p.x) + w, mapY(p.y)); }
        glEnd();
        glBegin(GL_TRIANGLE_STRIP);                                     // the mountain stream
        for (int i = 0; i <= 16; i++) { const float u = i / 16.0f; const cg::V2 p = world::streamAt(u);
            vtx(rgb(0.46f, 0.72f, 0.92f), mapX(p.x) - 0.008f, mapY(p.y)); vtx(rgb(0.46f, 0.72f, 0.92f), mapX(p.x) + 0.008f, mapY(p.y)); }
        glEnd();
        ui::drawText(mapX(world::LAKE_X) - 0.02f, mapY(world::LAKE_Y) - 0.01f, 0.0058f, rgb(0.16f, 0.36f, 0.62f), "Mirror Pond", true);
        ui::drawText(-1.30f, -0.58f, 0.0052f, rgb(0.16f, 0.36f, 0.62f), "Silver River", true);
    }
    // tulip fields (bottom right, subtle)
    {
        const Color fc[4] = {rgb(1.0f, 0.55f, 0.60f, 0.6f), rgb(1.0f, 0.88f, 0.45f, 0.6f), rgb(0.82f, 0.62f, 0.95f, 0.6f), rgb(1.0f, 0.70f, 0.42f, 0.6f)};
        for (int i = 0; i < 6; i++) { setColor(fc[i % 4]); rect(mapX(0.72f), mapY(-0.70f - i * 0.03f - 0.02f), mapX(0.99f), mapY(-0.70f - i * 0.03f)); }
    }
    // the real roads between the places (same curves as the village), drawn as sandy paths
    const VillageMap& vm = villageMap();
    for (const auto& r : vm.roads)
    {
        setColor(rgb(0.72f, 0.58f, 0.38f)); for (size_t i = 1; i < r.size(); i++) thickLine(mapX(r[i - 1].x), mapY(r[i - 1].y), mapX(r[i].x), mapY(r[i].y), 0.030f);
        setColor(rgb(0.92f, 0.82f, 0.62f)); for (size_t i = 1; i < r.size(); i++) thickLine(mapX(r[i - 1].x), mapY(r[i - 1].y), mapX(r[i].x), mapY(r[i].y), 0.020f);
        for (size_t i = 0; i < r.size(); i++) { setColor(rgb(0.92f, 0.82f, 0.62f)); circle(mapX(r[i].x), mapY(r[i].y), 0.010f, 10); }
    }
    {   // the railway line (station track) from the tunnel to the edge, with a little train
        const float ry = mapY(world::RAIL_Y);
        setColor(rgb(0.40f, 0.36f, 0.34f)); rect(mapX(-0.30f), ry - 0.007f, PANEL_X, ry + 0.007f);
        setColor(rgb(0.92f, 0.86f, 0.72f)); for (float x = mapX(-0.30f); x < PANEL_X; x += 0.04f) rect(x, ry - 0.003f, x + 0.02f, ry + 0.003f);
        setColor(rgb(0.25f, 0.22f, 0.25f)); ellipse(mapX(-0.30f), ry + 0.01f, 0.03f, 0.026f, 16);
    }
    // the rest of the village: small, faded homes and trees exactly where they stand
    {
        const float oldA = gfx::alphaMul;
        gfx::alphaMul = 0.55f;
        for (const cg::V2& p : vm.trees) mapTree(mapX(p.x), mapY(p.y), 1.0f, p.x < -0.5f);
        int k = 0;
        for (const cg::V2& p : vm.houses) mapHouse(mapX(p.x), mapY(p.y), 1.0f, k++);
        for (const cg::V2& p : vm.shops)  mapHouse(mapX(p.x), mapY(p.y), 1.0f, 1);
        gfx::alphaMul = oldA;
    }
    // future places: small icons + names, marked "soon" (they will become scenes later)
    for (const world::Place& p : world::PLACES)
    {
        if (p.visitable) continue;
        const float x = mapX(p.x), y = mapY(p.y);
        const float oldA = gfx::alphaMul;
        gfx::alphaMul = 0.80f;
        const float big = p.kind == world::PlaceKind::Fair ? 1.7f : p.kind == world::PlaceKind::ClockTower ? 1.4f : 1.0f;   // landmarks a bit larger
        drawPlaceVignette(p.kind, x, y, big, t, false);
        gfx::alphaMul = oldA;
        if (std::fabs(ui::mouse.x - x) < 0.06f * big && ui::mouse.y > y - 0.03f && ui::mouse.y < y + 0.12f * big) hoverFuture = &p;
    }

    glDisable(GL_SCISSOR_TEST);

    // title cartouche
    setColor(rgb(0.0f, 0.0f, 0.0f, 0.25f)); rect(-0.52f, PANEL_Y - 0.06f, 0.56f, PANEL_Y + 0.06f);
    setColor(rgb(0.70f, 0.16f, 0.16f)); quad(-0.54f, PANEL_Y - 0.05f, 0.54f, PANEL_Y - 0.05f, 0.58f, PANEL_Y + 0.07f, -0.58f, PANEL_Y + 0.07f);
    tri(-0.58f, PANEL_Y + 0.07f, -0.66f, PANEL_Y + 0.01f, -0.54f, PANEL_Y - 0.05f); tri(0.58f, PANEL_Y + 0.07f, 0.66f, PANEL_Y + 0.01f, 0.54f, PANEL_Y - 0.05f);
    ui::drawText(0.0f, PANEL_Y - 0.020f, 0.0090f, rgb(1.0f, 0.95f, 0.80f), "THE ENCHANTED WORLD", true);

    // compass rose (top right)
    const float cx = 1.32f, cy = 0.54f;
    setColor(rgb(0.97f, 0.92f, 0.78f)); circle(cx, cy, 0.10f, 32);
    setColor(rgb(0.45f, 0.30f, 0.16f)); ring(cx, cy, 0.10f, 0.10f, 32); ring(cx, cy, 0.085f, 0.085f, 32);
    for (int k = 0; k < 4; k++)
    {
        const float a = k * PI * 0.5f, c = std::cos(a), s2 = std::sin(a);
        setColor(k == 1 ? rgb(0.80f, 0.15f, 0.15f) : rgb(0.35f, 0.25f, 0.18f));
        tri(cx - s2 * 0.018f, cy + c * 0.018f, cx + s2 * 0.018f, cy - c * 0.018f, cx + c * 0.085f, cy + s2 * 0.085f);
    }
    ui::drawText(cx, cy + 0.11f, 0.0070f, rgb(0.30f, 0.20f, 0.10f), "N", true);
    // legend
    ui::drawText(0.0f, -PANEL_Y + 0.025f, 0.0055f, rgb(0.40f, 0.26f, 0.14f), "Click a place to travel there   -   point at the small places to see what is coming soon", true);
}

// returns the clicked scene (or Intro when nothing was clicked)
bool drawMapNodes(float t, SceneId& picked)
{
    bool clicked = false;
    for (const Node& n : NODES)
    {
        const float nx = n.x(), ny = n.y();
        const bool  hov = std::fabs(ui::mouse.x - nx) < 0.20f * VIGNETTE && ui::mouse.y > ny - 0.07f && ui::mouse.y < ny + 0.22f;
        const bool  here = isHere(n.id);
        const float s = VIGNETTE * (hov ? 1.08f : 1.0f);

        if (hov || here)                                                  // glow under the place
        {
            setColor(here ? rgb(1.0f, 0.45f, 0.35f, 0.35f) : rgb(1.0f, 0.95f, 0.5f, 0.45f));
            ellipse(nx, ny, 0.26f * s, 0.085f * s, 36);
        }
        drawPlaceVignette(n.kind, nx, ny, s, t, true);

        // name banner under the scene
        const float px = 0.0068f, w = ui::textWidth(n.label, px) * 0.5f + 0.03f;
        const float by = n.id == SceneId::Station ? ny + 0.155f : ny - 0.115f;   // the station sits near the top: banner above it
        setColor(rgb(0, 0, 0, 0.25f)); rect(nx - w + 0.006f, by - 0.008f, nx + w + 0.006f, by + 0.046f);
        setColor(here ? rgb(0.80f, 0.20f, 0.20f) : rgb(0.36f, 0.22f, 0.12f)); rect(nx - w, by, nx + w, by + 0.052f);
        tri(nx - w, by, nx - w - 0.025f, by + 0.026f, nx - w, by + 0.052f); tri(nx + w, by, nx + w + 0.025f, by + 0.026f, nx + w, by + 0.052f);
        ui::drawText(nx, by + 0.013f, px, rgb(1.0f, 0.97f, 0.88f), n.label, true);

        // points earned at this place (gold badge)
        {
            const game::Where w2 = n.id == SceneId::Playground ? game::Where::Playground
                                 : n.id == SceneId::Station    ? game::Where::Station
                                 : (n.id == SceneId::HospitalOutside || n.id == SceneId::HospitalInside) ? game::Where::Hospital
                                 : game::Where::Garden;
            char b[16]; std::snprintf(b, sizeof(b), "%+d", game::scoreAt(w2));
            const float bx = nx + w + 0.07f, by2 = by + 0.026f, bw = ui::textWidth(b, 0.0055f) * 0.5f + 0.022f;
            setColor(rgb(0, 0, 0, 0.25f)); rect(bx - bw + 0.006f, by2 - 0.030f, bx + bw + 0.006f, by2 + 0.028f);
            setColor(game::scoreAt(w2) < 0 ? rgb(0.80f, 0.25f, 0.22f) : rgb(0.96f, 0.74f, 0.18f)); rect(bx - bw, by2 - 0.024f, bx + bw, by2 + 0.034f);
            ui::drawText(bx, by2 - 0.011f, 0.0055f, rgb(1, 1, 1), b, true);
        }
        if (here)    // "you are here" pin, bobbing above the place
        {
            const float bob = 0.012f * std::sin(t * 4.0f), py = (n.id == SceneId::Station ? ny + 0.30f : ny + 0.24f * VIGNETTE) + bob;
            const float nx0 = nx; (void)nx0;
            setColor(rgb(0.90f, 0.15f, 0.15f)); circle(nx, py, 0.030f, 16);
            tri(nx - 0.022f, py - 0.008f, nx + 0.022f, py - 0.008f, nx, py - 0.060f);
            setColor(rgb(1.0f, 1.0f, 1.0f)); circle(nx, py, 0.011f, 10);
        }
        if (hov && ui::mouse.pressed) { picked = n.id; clicked = true; }
    }
    if (hoverFuture)                                                     // "coming soon" label, on top of everything
    {
        const float x = mapX(hoverFuture->x), y = mapY(hoverFuture->y);
        char b[64]; std::snprintf(b, sizeof(b), "%s - coming soon", hoverFuture->name);
        const float px = 0.0055f, w = ui::textWidth(b, px) * 0.5f + 0.02f;
        setColor(rgb(0.30f, 0.22f, 0.14f, 0.92f)); rect(x - w, y - 0.065f, x + w, y - 0.015f);
        ui::drawText(x, y - 0.053f, px, rgb(1.0f, 0.95f, 0.85f), b, true);
    }
    return clicked;
}

}   // namespace

bool isIntroOrGarden() { return current == SceneId::Intro || current == SceneId::Garden; }

const char* name(SceneId id)
{
    switch (id)
    {
    case SceneId::Intro:           return "Intro";
    case SceneId::Garden:          return "Garden";
    case SceneId::Playground:      return "Playground";
    case SceneId::Station:         return "Train Station";
    case SceneId::HospitalOutside: return "Hospital (outside)";
    default:                       return "Hospital (inside)";
    }
}

// Restart = replay the opening film from the title screen. The GAME SESSION is kept:
// score, events and every location's state (patients, children, trains, flowers) stay
// exactly as they are. Only closing the application starts a new session.
void restartFromBeginning(double now)
{
    resetAnimation(now);          // the ONE shared clock goes back to the title screen
    current = SceneId::Intro;
    mapOpen = false;
    game::countRestart();
}

void updateLocation(double dt)
{
    const bool  blocked = mapOpen;
    const float mx = ui::mouse.x, my = ui::mouse.y;
    const bool  valid = ui::mouse.valid && !blocked, down = ui::mouse.down && !blocked;
    const bool  pressed = ui::mouse.pressed && !blocked, released = ui::mouse.released && !blocked;

    if (current == SceneId::Playground)            // each location only runs while it is on screen
    {
        playground::Input in;
        in.x = mx; in.y = my; in.valid = valid; in.down = down; in.pressed = pressed; in.released = released;
        playground::update(dt, in);
        return;
    }
    if (current == SceneId::Station)
    {
        station::Input in;
        in.x = mx; in.y = my; in.valid = valid; in.down = down; in.pressed = pressed; in.released = released;
        station::update(dt, in);
        return;
    }
    if (current != SceneId::HospitalOutside && current != SceneId::HospitalInside) return;
    hospital::Input in;
    in.x = ui::mouse.x; in.y = ui::mouse.y; in.valid = ui::mouse.valid && !mapOpen;
    in.down = ui::mouse.down && !mapOpen; in.pressed = ui::mouse.pressed && !mapOpen; in.released = ui::mouse.released && !mapOpen;
    const hospital::View v = current == SceneId::HospitalInside ? hospital::View::Inside : hospital::View::Outside;
    const hospital::Action a = hospital::update(dt, v, in);
    if (a == hospital::Action::GoInside)  current = SceneId::HospitalInside;
    if (a == hospital::Action::GoOutside) current = SceneId::HospitalOutside;
}

void drawLocation(const AnimState& s)
{
    const float t = (float)s.time;
    switch (current)
    {
    case SceneId::Playground:      playground::draw(t); break;
    case SceneId::Station:         station::draw(t);    break;
    case SceneId::HospitalOutside: hospital::draw(hospital::View::Outside, t); break;
    case SceneId::HospitalInside:  hospital::draw(hospital::View::Inside, t);  break;
    default: break;                                    // Intro / Garden are drawn by main.cpp
    }
}

void drawUI(const AnimState& s, double now)
{
    gfx::useTint = false;
    const bool mapAllowed = current != SceneId::Intro || s.phase == PHASE_LIVING_GARDEN;
    const float t = (float)now;

    if (mapOpen)
    {
        drawMapPanel(t);
        SceneId picked = SceneId::Intro;
        const bool pickedNode = drawMapNodes(t, picked);

        const float cx = PANEL_X - 0.06f, cy = PANEL_Y - 0.06f;             // close button
        const bool  hc = ui::hover(cx, cy, 0.05f);
        setColor(hc ? rgb(0.85f, 0.20f, 0.20f) : rgb(0.60f, 0.15f, 0.15f)); circle(cx, cy, 0.05f, 20);
        setColor(rgb(1, 1, 1));
        thickLine(cx - 0.022f, cy - 0.022f, cx + 0.022f, cy + 0.022f, 0.011f);
        thickLine(cx - 0.022f, cy + 0.022f, cx + 0.022f, cy - 0.022f, 0.011f);

        if (pickedNode)  { current = picked; mapOpen = false; }
        else if (ui::mouse.pressed && (hc || std::fabs(ui::mouse.x) > PANEL_X || std::fabs(ui::mouse.y) > PANEL_Y))
            mapOpen = false;
    }

    if (!mapOpen && (current == SceneId::Garden || (current == SceneId::Intro && s.phase == PHASE_LIVING_GARDEN)))
    {
        char stat[96];
        const gardensim::Gardener& g = gardensim::gardener();
        const char* doing = g.state == gardensim::GState::InHouse ? "resting" : g.state == gardensim::GState::Watering ? "watering" :
                            g.state == gardensim::GState::Filling ? "filling the can" : "on the way";
        std::snprintf(stat, sizeof(stat), "Blooming %d   Thirsty %d   Gardener: %s", gardensim::bloomCount(), gardensim::thirstyCount(), doing);
        game::drawHud(game::Where::Garden, "The Garden", stat);
        game::drawFeed(-WORLD_W + 0.05f, 0.74f, true);            // garden: top-left, keeps the pond path clear
    }

    if (!mapOpen && (current == SceneId::HospitalOutside || current == SceneId::HospitalInside))
        hospital::drawOverlay(current == SceneId::HospitalInside ? hospital::View::Inside : hospital::View::Outside);
    if (!mapOpen && current == SceneId::Playground) playground::drawOverlay();
    if (!mapOpen && current == SceneId::Station)    station::drawOverlay();

    if (s.waiting) { gfx::useTint = true; return; }       // title screen: no buttons, any click starts the film
    if (ui::backButton()) restartFromBeginning(now);
    if (!mapOpen && mapAllowed && ui::mapButton()) mapOpen = true;

    // clock + mode label (shows the real-time sync)
    const int   hh = (int)theme::localHour(), mm = (int)((theme::localHour() - hh) * 60.0f);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s  %02d:%02d%s", theme::modeName(theme::mode()), hh, mm, theme::isOverridden() ? " (TEST)" : "");
    ui::drawText(-WORLD_W + 0.04f, -0.96f, 0.0075f, rgb(1.0f, 1.0f, 1.0f, 0.85f), buf);
    gfx::useTint = true;
}

}   // namespace scenes
