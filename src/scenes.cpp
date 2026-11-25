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
#include <cstring>
#include <algorithm>

using namespace gfx;

SceneId scenes::current = SceneId::Intro;

namespace scenes
{
namespace
{

// ---- "under construction" pop-up (village hub): fades out by itself after a few seconds ----
char   toastText[96] = "";
double toastStart = -100.0;
const double TOAST_TIME = 3.0;

void showToast(const char* placeName, double now)
{
    std::snprintf(toastText, sizeof(toastText), "%s is under construction - coming soon!", placeName);
    toastStart = now;
}

void drawToast(double now)
{
    const float age = (float)(now - toastStart);
    if (age < 0.0f || age > TOAST_TIME) return;
    // pop in (0.2 s), stay, fade out (last 0.6 s)
    const float a  = std::min(1.0f, age / 0.2f) * std::min(1.0f, ((float)TOAST_TIME - age) / 0.6f);
    const float sc = 0.9f + 0.1f * std::min(1.0f, age / 0.2f);
    const float px = 0.0068f * sc, y = -0.78f;
    const float w  = ui::textWidth(toastText, px) * 0.5f + 0.09f;
    setColor(rgb(0, 0, 0, 0.30f * a));            rect(-w + 0.01f, y - 0.065f, w + 0.01f, y + 0.055f);
    setColor(rgb(0.16f, 0.12f, 0.10f, 0.92f * a)); rect(-w, y - 0.055f, w, y + 0.065f);
    // yellow-black "construction" stripe on the left
    for (int i = 0; i < 4; i++)
    {
        setColor(i & 1 ? rgb(0.10f, 0.10f, 0.10f, a) : rgb(1.0f, 0.78f, 0.10f, a));
        quad(-w + i * 0.012f, y - 0.055f, -w + (i + 1) * 0.012f, y - 0.055f, -w + (i + 1) * 0.012f + 0.0f, y + 0.065f, -w + i * 0.012f, y + 0.065f);
    }
    ui::drawText(0.02f, y - 0.012f, px, rgb(1.0f, 0.92f, 0.70f, a), toastText, true);
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
    game::countRestart();
}

void goToVillage(double now)
{
    goToVillageFilm(now);
    current = SceneId::Intro;
}

bool atVillageHub(const AnimState& s) { return current == SceneId::Intro && s.villageWait; }

bool hubClick(const char* placeName, double now)
{
    if (!placeName) return false;
    for (const world::Place& p : world::PLACES)
    {
        if (std::strcmp(p.name, placeName) != 0) continue;
        if (!p.visitable) { showToast(p.name, now); return true; }
        switch (p.kind)
        {
        case world::PlaceKind::Garden:     continueFilm(); break;            // the film zooms into the garden
        case world::PlaceKind::Playground: current = SceneId::Playground; break;
        case world::PlaceKind::Station:    current = SceneId::Station; break;
        case world::PlaceKind::Hospital:   current = SceneId::HospitalOutside; break;
        default: showToast(p.name, now); break;
        }
        return true;
    }
    return false;
}

void updateLocation(double dt)
{
    const bool  blocked = false;
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
    in.x = ui::mouse.x; in.y = ui::mouse.y; in.valid = ui::mouse.valid;
    in.down = ui::mouse.down; in.pressed = ui::mouse.pressed; in.released = ui::mouse.released;
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

    if ((current == SceneId::Garden || (current == SceneId::Intro && s.phase == PHASE_LIVING_GARDEN)))
    {
        char stat[96];
        const gardensim::Gardener& g = gardensim::gardener();
        const char* doing = g.state == gardensim::GState::InHouse ? "resting" : g.state == gardensim::GState::Watering ? "watering" :
                            g.state == gardensim::GState::Filling ? "filling the can" : "on the way";
        std::snprintf(stat, sizeof(stat), "Blooming %d   Thirsty %d   Gardener: %s", gardensim::bloomCount(), gardensim::thirstyCount(), doing);
        game::drawHud(game::Where::Garden, "The Garden", stat);
        game::drawFeed(-WORLD_W + 0.05f, 0.74f, true);            // garden: top-left, keeps the pond path clear
    }

    if ((current == SceneId::HospitalOutside || current == SceneId::HospitalInside))
        hospital::drawOverlay(current == SceneId::HospitalInside ? hospital::View::Inside : hospital::View::Outside);
    if (current == SceneId::Playground) playground::drawOverlay();
    if (current == SceneId::Station)    station::drawOverlay();

    if (s.waiting) { gfx::useTint = true; return; }       // title screen: no buttons, any click starts the film
    if (ui::backButton()) restartFromBeginning(now);
    if (!atVillageHub(s) && ui::mapButton()) goToVillage(now);         // MAP = back to the village hub
    drawToast(now);

    // clock + mode label (shows the real-time sync)
    const int   hh = (int)theme::localHour(), mm = (int)((theme::localHour() - hh) * 60.0f);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s  %02d:%02d%s", theme::modeName(theme::mode()), hh, mm, theme::isOverridden() ? " (TEST)" : "");
    ui::drawText(-WORLD_W + 0.04f, -0.96f, 0.0075f, rgb(1.0f, 1.0f, 1.0f, 0.85f), buf);
    gfx::useTint = true;
}

}   // namespace scenes
