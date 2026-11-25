#include "scenes.h"

#include "gfx.h"
#include "game.h"
#include "gardensim.h"
#include "hospital.h"
#include "playground.h"
#include "station.h"
#include "theme.h"
#include "ui.h"

#include <cmath>
#include <cstdio>

using namespace gfx;

SceneId scenes::current = SceneId::Intro;
bool    scenes::mapOpen = false;

namespace scenes
{
namespace
{

struct Node { SceneId id; float x, y; const char* label; };
const Node NODES[4] = {
    {SceneId::Garden,          -0.88f, -0.22f, "GARDEN"},
    {SceneId::Playground,      -0.30f,  0.28f, "PLAYGROUND"},
    {SceneId::Station,          0.50f, -0.28f, "TRAIN STATION"},
    {SceneId::HospitalOutside,  1.00f,  0.30f, hospitalcfg::NAME},
};

const float PANEL_X = 1.46f, PANEL_Y = 0.80f;

bool isHere(SceneId id)
{
    if (id == SceneId::Garden)          return current == SceneId::Garden || current == SceneId::Intro;
    if (id == SceneId::HospitalOutside) return current == SceneId::HospitalOutside || current == SceneId::HospitalInside;
    return current == id;
}

// ---------------------------------------------------------------------------
// Map artwork
// ---------------------------------------------------------------------------
void mapIcon(SceneId id, float x, float y, float s)
{
    switch (id)
    {
    case SceneId::Garden:
        setColor(rgb(0.45f, 0.28f, 0.14f)); rect(x - 0.012f * s, y - 0.06f * s, x + 0.012f * s, y + 0.01f * s);
        setColor(rgb(0.16f, 0.58f, 0.26f)); circle(x, y + 0.045f * s, 0.058f * s, 20);
        setColor(rgb(0.30f, 0.72f, 0.34f)); circle(x - 0.02f * s, y + 0.06f * s, 0.032f * s, 16);
        setColor(rgb(0.90f, 0.20f, 0.25f)); circle(x + 0.02f * s, y + 0.04f * s, 0.010f * s, 10);
        setColor(rgb(0.95f, 0.45f, 0.70f)); circle(x - 0.075f * s, y - 0.055f * s, 0.016f * s, 10);
        setColor(rgb(1.00f, 0.80f, 0.20f)); circle(x + 0.075f * s, y - 0.055f * s, 0.016f * s, 10);
        break;
    case SceneId::Playground:
        setColor(rgb(0.55f, 0.35f, 0.20f));                                           // swing frame
        thickLine(x + 0.01f * s, y - 0.06f * s, x + 0.04f * s, y + 0.055f * s, 0.011f * s);
        thickLine(x + 0.09f * s, y - 0.06f * s, x + 0.06f * s, y + 0.055f * s, 0.011f * s);
        thickLine(x + 0.035f * s, y + 0.055f * s, x + 0.065f * s, y + 0.055f * s, 0.012f * s);
        setColor(rgb(0.9f, 0.9f, 0.9f));
        thickLine(x + 0.05f * s, y + 0.055f * s, x + 0.05f * s, y - 0.02f * s, 0.005f * s);
        setColor(rgb(0.95f, 0.30f, 0.25f)); rect(x + 0.035f * s, y - 0.03f * s, x + 0.065f * s, y - 0.018f * s);
        setColor(rgb(0.25f, 0.55f, 0.90f));                                           // slide
        quad(x - 0.09f * s, y - 0.06f * s, x - 0.08f * s, y + 0.05f * s, x - 0.065f * s, y + 0.05f * s, x - 0.07f * s, y - 0.06f * s);
        thickLine(x - 0.075f * s, y + 0.045f * s, x - 0.015f * s, y - 0.055f * s, 0.02f * s);
        break;
    case SceneId::Station:
        setColor(rgb(0.42f, 0.42f, 0.48f)); rect(x - 0.09f * s, y - 0.06f * s, x + 0.09f * s, y - 0.045f * s);   // track
        setColor(rgb(0.93f, 0.88f, 0.76f)); rect(x - 0.08f * s, y - 0.045f * s, x + 0.02f * s, y + 0.025f * s);  // station
        setColor(rgb(0.75f, 0.22f, 0.20f)); tri(x - 0.09f * s, y + 0.025f * s, x + 0.03f * s, y + 0.025f * s, x - 0.03f * s, y + 0.065f * s);
        setColor(rgb(0.30f, 0.50f, 0.85f)); rect(x + 0.025f * s, y - 0.040f * s, x + 0.095f * s, y - 0.005f * s); // train
        setColor(rgb(0.85f, 0.92f, 0.98f)); rect(x + 0.035f * s, y - 0.028f * s, x + 0.055f * s, y - 0.012f * s);
        rect(x + 0.065f * s, y - 0.028f * s, x + 0.085f * s, y - 0.012f * s);
        break;
    default:   // hospital
        setColor(rgb(0.95f, 0.95f, 0.97f)); rect(x - 0.07f * s, y - 0.06f * s, x + 0.07f * s, y + 0.04f * s);
        setColor(rgb(0.70f, 0.78f, 0.88f)); rect(x - 0.07f * s, y + 0.04f * s, x + 0.07f * s, y + 0.05f * s);
        setColor(rgb(0.85f, 0.12f, 0.15f));
        rect(x - 0.012f * s, y - 0.025f * s, x + 0.012f * s, y + 0.035f * s);
        rect(x - 0.040f * s, y - 0.003f * s, x + 0.040f * s, y + 0.021f * s);
        setColor(rgb(0.55f, 0.70f, 0.85f));
        rect(x - 0.060f * s, y - 0.045f * s, x - 0.035f * s, y - 0.025f * s);
        rect(x + 0.035f * s, y - 0.045f * s, x + 0.060f * s, y - 0.025f * s);
        break;
    }
}

void drawDashed(float x0, float y0, float x1, float y1)
{
    const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    const int   n  = (int)(len / 0.05f);
    for (int i = 0; i < n; i += 2)
    {
        const float a = (float)i / n, b = (float)(i + 1) / n;
        const float bow0 = 0.06f * std::sin(a * PI), bow1 = 0.06f * std::sin(b * PI);
        thickLine(x0 + dx * a, y0 + dy * a + bow0, x0 + dx * b, y0 + dy * b + bow1, 0.012f);
    }
}

void drawMapPanel(float t)
{
    // dim the scene behind
    glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
    rect(-WORLD_W, -1.0f, WORLD_W, 1.0f);

    // paper + border
    setColor(rgb(0.0f, 0.0f, 0.0f, 0.35f)); rect(-PANEL_X + 0.02f, -PANEL_Y - 0.03f, PANEL_X + 0.02f, PANEL_Y - 0.03f);
    setColor(rgb(0.45f, 0.30f, 0.16f));      rect(-PANEL_X - 0.025f, -PANEL_Y - 0.025f, PANEL_X + 0.025f, PANEL_Y + 0.025f);
    rectV(-PANEL_X, -PANEL_Y, PANEL_X, PANEL_Y, rgb(0.90f, 0.82f, 0.62f), rgb(0.97f, 0.92f, 0.76f));

    // land masses
    setColor(rgb(0.66f, 0.80f, 0.50f, 0.85f));
    ellipse(-0.55f, -0.10f, 0.95f, 0.50f, 40);
    ellipse(0.30f, 0.05f, 0.80f, 0.55f, 40);
    ellipse(1.00f, 0.05f, 0.45f, 0.62f, 40);
    setColor(rgb(0.58f, 0.74f, 0.44f, 0.85f));
    ellipse(-0.20f, 0.25f, 0.55f, 0.30f, 36);
    ellipse(0.65f, -0.35f, 0.60f, 0.28f, 36);

    // river: sweeps from the top to the bottom between the locations
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= 40; i++)
    {
        const float v = i / 40.0f, y = lerpf(PANEL_Y, -PANEL_Y, v);
        const float x = 0.18f + 0.16f * std::sin(v * 7.0f) - 0.10f * v;
        const float w = 0.028f + 0.02f * v;
        vtx(rgb(0.42f, 0.70f, 0.88f), x - w, y);
        vtx(rgb(0.34f, 0.60f, 0.82f), x + w, y);
    }
    glEnd();

    // mountains (top-left) and forest dots
    for (int i = 0; i < 5; i++)
    {
        const float x = -1.28f + i * 0.17f, h = 0.20f + 0.09f * hash1(i * 3.3f);
        setColor(rgb(0.52f, 0.55f, 0.58f)); tri(x - 0.12f, 0.42f, x + 0.12f, 0.42f, x, 0.42f + h);
        setColor(rgb(0.38f, 0.40f, 0.44f)); tri(x, 0.42f, x + 0.12f, 0.42f, x, 0.42f + h);
        setColor(rgb(1.0f, 1.0f, 1.0f));    tri(x - 0.035f, 0.42f + h * 0.72f, x + 0.035f, 0.42f + h * 0.72f, x, 0.42f + h);
    }
    for (int i = 0; i < 26; i++)
    {
        const float x = lerpf(-1.35f, 1.35f, hash1(i * 4.7f)), y = lerpf(-0.70f, 0.55f, hash1(i * 9.1f + 2.0f));
        bool near = false;
        for (const Node& n : NODES) if (std::fabs(x - n.x) < 0.27f && std::fabs(y - n.y) < 0.27f) near = true;
        if (near || (x > -1.4f && x < -0.7f && y > 0.38f)) continue;
        setColor(rgb(0.18f, 0.48f, 0.26f)); tri(x - 0.025f, y - 0.02f, x + 0.025f, y - 0.02f, x, y + 0.045f);
    }

    // dashed roads
    setColor(rgb(0.60f, 0.40f, 0.20f, 0.90f));
    for (int i = 0; i < 3; i++) drawDashed(NODES[i].x, NODES[i].y, NODES[i + 1].x, NODES[i + 1].y);

    // title ribbon
    setColor(rgb(0.70f, 0.16f, 0.16f));
    quad(-0.40f, 0.68f, 0.40f, 0.68f, 0.44f, 0.80f + 0.02f, -0.44f, 0.80f + 0.02f);
    ui::drawText(0.0f, 0.715f, 0.018f, rgb(1.0f, 0.95f, 0.85f), "MAP", true);

    // compass
    const float cx = 1.25f, cy = -0.55f;
    setColor(rgb(0.45f, 0.30f, 0.16f)); ring(cx, cy, 0.09f, 0.09f, 32);
    setColor(rgb(0.80f, 0.15f, 0.15f)); tri(cx - 0.025f, cy, cx + 0.025f, cy, cx, cy + 0.085f);
    setColor(rgb(0.30f, 0.25f, 0.20f)); tri(cx - 0.025f, cy, cx + 0.025f, cy, cx, cy - 0.085f);
    ui::drawText(cx, cy + 0.105f, 0.008f, rgb(0.30f, 0.20f, 0.10f), "N", true);
    (void)t;
}

// returns the clicked scene (or Intro when nothing was clicked)
bool drawMapNodes(float t, SceneId& picked)
{
    bool clicked = false;
    for (const Node& n : NODES)
    {
        const bool  hov = ui::hover(n.x, n.y, 0.17f);
        const float s   = hov ? 1.15f : 1.0f;
        const bool  here = isHere(n.id);

        if (hov) { setColor(rgb(1.0f, 0.95f, 0.5f, 0.55f)); circle(n.x, n.y, 0.22f, 32); }
        setColor(rgb(0.0f, 0.0f, 0.0f, 0.25f)); circle(n.x + 0.008f, n.y - 0.010f, 0.155f * s, 28);
        setColor(here ? rgb(0.85f, 0.20f, 0.20f) : rgb(0.35f, 0.25f, 0.15f)); circle(n.x, n.y, 0.155f * s, 28);
        setColor(rgb(0.97f, 0.95f, 0.88f));                                       circle(n.x, n.y, 0.138f * s, 28);
        mapIcon(n.id, n.x, n.y, s * 1.25f);

        // label plate
        const float px = 0.0105f, w = ui::textWidth(n.label, px) * 0.5f + 0.03f;
        setColor(rgb(0.30f, 0.20f, 0.10f, 0.88f)); rect(n.x - w, n.y - 0.255f, n.x + w, n.y - 0.185f);
        ui::drawText(n.x, n.y - 0.238f, px, rgb(1.0f, 0.97f, 0.88f), n.label, true);

        // points earned at this place (gold badge, top-right of the disc)
        {
            const game::Where w = n.id == SceneId::Playground ? game::Where::Playground
                                : n.id == SceneId::Station    ? game::Where::Station
                                : (n.id == SceneId::HospitalOutside || n.id == SceneId::HospitalInside) ? game::Where::Hospital
                                : game::Where::Garden;
            char b[16]; std::snprintf(b, sizeof(b), "%+d", game::scoreAt(w));
            const float bx = n.x + 0.13f, by = n.y + 0.12f, bw = ui::textWidth(b, 0.0060f) * 0.5f + 0.025f;
            setColor(rgb(0, 0, 0, 0.25f)); rect(bx - bw + 0.006f, by - 0.032f, bx + bw + 0.006f, by + 0.030f);
            setColor(game::scoreAt(w) < 0 ? rgb(0.80f, 0.25f, 0.22f) : rgb(0.96f, 0.74f, 0.18f)); rect(bx - bw, by - 0.026f, bx + bw, by + 0.036f);
            ui::drawText(bx, by - 0.012f, 0.0060f, rgb(1, 1, 1), b, true);
        }

        if (here)    // "you are here" pin
        {
            const float by = 0.012f * std::sin(t * 4.0f);
            setColor(rgb(0.90f, 0.15f, 0.15f)); circle(n.x, n.y + 0.30f + by, 0.028f, 16);
            tri(n.x - 0.020f, n.y + 0.293f + by, n.x + 0.020f, n.y + 0.293f + by, n.x, n.y + 0.245f + by);
            setColor(rgb(1.0f, 1.0f, 1.0f));    circle(n.x, n.y + 0.30f + by, 0.010f, 10);
        }
        if (hov && ui::mouse.pressed) { picked = n.id; clicked = true; }
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

        const float cx = PANEL_X - 0.07f, cy = PANEL_Y - 0.07f;             // close button
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
        game::drawFeed();
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
