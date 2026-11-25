// Enchanted Garden - cinematic mountain -> village -> garden animation
// C++17, OpenGL 2.1 (immediate mode), GLFW + GLEW
//
// Mouse:  top-left arrow = restart the whole film      top-right map = go to a location
//         Playground: click swings / see-saw / ball, drag children onto rides
//         Station:    click signals / points / footbridge / entrance / a train, drag passengers
// Keys:   SPACE  pause / resume          R        restart (back to the title screen, score is kept)
//         0..5   jump to a stage (0 = title)        <- / ->  skip -2 s / +2 s
//         O      view culling on / off   T        TEST: cycle day/afternoon/evening/night
//         ESC    close map / quit
//
// Test options (single frame, then exit):
//   EnchantedGarden --shot <seconds> <file.ppm> [--hour H] [--scene garden|playground|station|hospital|inside]
//                   [--map] [--click X Y]

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "animation.h"
#include "garden.h"
#include "gfx.h"
#include "mountain.h"
#include "gardensim.h"
#include "hospital.h"
#include "playground.h"
#include "station.h"
#include "scenes.h"
#include "theme.h"
#include "ui.h"
#include "font.h"
#include "game.h"

using namespace std;

static const int   START_W = 1280;
static const int   START_H = 720;
static const float ASPECT  = 16.0f / 9.0f;

static GLuint captureTex = 0;

// ---------------------------------------------------------------------------
// Village -> garden dissolve: copy the finished village frame into a texture,
// draw the garden, then lay the village texture over it with fading alpha.
// ---------------------------------------------------------------------------
static void captureFrame(int vx, int vy, int vw, int vh)
{
    if (!captureTex) glGenTextures(1, &captureTex);
    glBindTexture(GL_TEXTURE_2D, captureTex);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, vx, vy, vw, vh, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void drawCapture(float alpha)
{
    const float W = gfx::WORLD_W;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, captureTex);
    glColor4f(1.0f, 1.0f, 1.0f, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-W, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(W, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(W, 1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-W, 1.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

// Warm bloom of light that peaks in the middle of the dissolve. It hides the
// moment where giant village walls and the garden overlap and gives the
// transition an "enchanted" feel.
static void drawBloom(float blend)
{
    const float a = 0.92f * std::pow(std::sin(gfx::PI * blend), 1.5f);
    if (a <= 0.001f) return;
    const float W = gfx::WORLD_W;
    glColor4f(1.0f, 0.97f, 0.86f, 0.55f * a);
    gfx::rect(-W, -1.0f, W, 1.0f);
    gfx::glow(0.0f, 0.0f, 3.0f, gfx::rgb(1.0f, 0.97f, 0.86f, a), 48);
}

// ---------------------------------------------------------------------------
// One frame
// ---------------------------------------------------------------------------
static int g_vx = 0, g_vy = 0, g_vw = 1, g_vh = 1;       // picture rectangle inside the window (pixels)

static void computeViewport(int fbw, int fbh)
{
    g_vw = fbw; g_vh = (int)(fbw / ASPECT);
    if (g_vh > fbh) { g_vh = fbh; g_vw = (int)(fbh * ASPECT); }
    g_vx = (fbw - g_vw) / 2;
    g_vy = (fbh - g_vh) / 2;
}

// cursor (window pixels) -> world coordinates
static void updateMouse(GLFWwindow* window)
{
    double cx = 0, cy = 0;
    glfwGetCursorPos(window, &cx, &cy);
    int ww = 1, wh = 1, fbw = 1, fbh = 1;
    glfwGetWindowSize(window, &ww, &wh);
    glfwGetFramebufferSize(window, &fbw, &fbh);
    if (ww <= 0 || wh <= 0 || fbw <= 0 || fbh <= 0) { ui::mouse.valid = false; return; }
    computeViewport(fbw, fbh);
    const double fx = cx * fbw / ww, fy = fbh - cy * fbh / wh;
    const double nx = (fx - g_vx) / g_vw, ny = (fy - g_vy) / g_vh;
    ui::mouse.valid = nx >= 0.0 && nx <= 1.0 && ny >= 0.0 && ny <= 1.0;
    ui::mouse.x = (float)((nx * 2.0 - 1.0) * gfx::WORLD_W);
    ui::mouse.y = (float)(ny * 2.0 - 1.0);
}

static void mouseButtonCallback(GLFWwindow*, int button, int action, int)
{
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action == GLFW_PRESS)   { ui::mouse.down = true;  ui::mouse.pressed  = true; }
    if (action == GLFW_RELEASE) { ui::mouse.down = false; ui::mouse.released = true; }
}

static void renderFrame(GLFWwindow* window, double now)
{
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    if (fbw <= 0 || fbh <= 0) return;                           // minimised

    // keep a fixed 16:9 picture, black bars if the window has another shape
    computeViewport(fbw, fbh);
    const int vx = g_vx, vy = g_vy, vw = g_vw, vh = g_vh;

    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, fbw, fbh);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glViewport(vx, vy, vw, vh);
    glScissor(vx, vy, vw, vh);
    glEnable(GL_SCISSOR_TEST);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-gfx::WORLD_W, gfx::WORLD_W, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    gfx::statDrawn  = 0;
    gfx::statCulled = 0;

    const AnimState& s = getAnimState();

    // time-of-day tint for everything that is "in the world"
    gfx::tintColor = theme::cur().tint;
    gfx::useTint   = true;

    if (scenes::isIntroOrGarden())
    {
        if (s.gardenBlend < 1.0f) drawMountainScene(s);         // stages 1-4 (village side)

        if (s.gardenBlend > 0.0f)                               // stages 4-6 (garden side)
        {
            const bool fading = s.gardenBlend < 1.0f;
            if (fading)
            {
                captureFrame(vx, vy, vw, vh);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            gfx::useTint = true;
            drawGarden(s);
            gfx::useTint = false;                               // the captured frame is already tinted
            if (fading)
            {
                drawCapture(1.0f - s.gardenBlend);
                drawBloom(s.gardenBlend);
            }
        }
    }
    else
    {
        scenes::drawLocation(s);                                // playground / station / hospital
    }

    gfx::useTint = false;
    scenes::drawUI(s, now);                                     // back button, map button, map, clock label
}

static void savePPM(const char* path, int w, int h)
{
    vector<unsigned char> px((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) fwrite(&px[(size_t)y * w * 3], 1, (size_t)w * 3, f);
    fclose(f);
}

static void keyCallback(GLFWwindow* window, int key, int, int action, int)
{
    if (action != GLFW_PRESS) return;
    const double now = glfwGetTime();

    switch (key)
    {
    case GLFW_KEY_ESCAPE:
        if (scenes::mapOpen) scenes::mapOpen = false;
        else glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;
    case GLFW_KEY_SPACE:  toggleAnimation(); break;
    case GLFW_KEY_R:      scenes::restartFromBeginning(now); break;
    case GLFW_KEY_0:      scenes::current = SceneId::Intro; scenes::mapOpen = false; jumpToPhase(PHASE_TITLE, now); break;
    case GLFW_KEY_1:      scenes::current = SceneId::Intro; scenes::mapOpen = false; jumpToPhase(PHASE_OPENING, now); break;
    case GLFW_KEY_2:      scenes::current = SceneId::Intro; scenes::mapOpen = false; jumpToPhase(PHASE_REVEAL, now); break;
    case GLFW_KEY_3:      scenes::current = SceneId::Intro; scenes::mapOpen = false; jumpToPhase(PHASE_JOURNEY, now); break;
    case GLFW_KEY_4:      scenes::current = SceneId::Intro; scenes::mapOpen = false; jumpToPhase(PHASE_GARDEN_REVEAL, now); break;
    case GLFW_KEY_5:      scenes::current = SceneId::Intro; scenes::mapOpen = false; jumpToPhase(PHASE_LIVING_GARDEN, now); break;
    case GLFW_KEY_LEFT:   seekAnimation(-2.0, now); break;
    case GLFW_KEY_RIGHT:  seekAnimation(2.0, now); break;
    case GLFW_KEY_O:      gfx::cullingEnabled = !gfx::cullingEnabled; break;
    case GLFW_KEY_T:      theme::cycleOverride(); break;
    case GLFW_KEY_H:      gardensim::toggleHud(); break;
    default: break;
    }
}

int main(int argc, char** argv)
{
    // optional test options (see top of file)
    bool        shotMode = false, openMap = false, doClick = false;
    double      shotTime = 0.0;
    const char* shotFile = "shot.ppm";
    float       testHour = -1.0f, clickX = 0.0f, clickY = 0.0f;
    const char* testScene = nullptr;
    double      simSeconds = 0.0, afterSeconds = 0.0;
    int         repeat = 1;
    bool        haveCursor = false;
    float       curX = 0.0f, curY = 0.0f;
    for (int i = 1; i < argc; i++)
    {
        const string a = argv[i];
        if (a == "--shot" && i + 2 < argc)   { shotMode = true; shotTime = atof(argv[i + 1]); shotFile = argv[i + 2]; i += 2; }
        else if (a == "--hour" && i + 1 < argc)  { testHour = (float)atof(argv[++i]); }
        else if (a == "--scene" && i + 1 < argc) { testScene = argv[++i]; }
        else if (a == "--simulate" && i + 1 < argc) { simSeconds = atof(argv[++i]); }
        else if (a == "--after" && i + 1 < argc)    { afterSeconds = atof(argv[++i]); }
        else if (a == "--repeat" && i + 1 < argc)   { repeat = atoi(argv[++i]); }
        else if (a == "--cursor" && i + 2 < argc) { haveCursor = true; curX = (float)atof(argv[i + 1]); curY = (float)atof(argv[i + 2]); i += 2; }
        else if (a == "--map")                   { openMap = true; }
        else if (a == "--click" && i + 2 < argc) { doClick = true; clickX = (float)atof(argv[i + 1]); clickY = (float)atof(argv[i + 2]); i += 2; }
    }

    if (!glfwInit())
    {
        cout << "GLFW initialization failed!" << endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_SAMPLES, 4);                            // smooth edges (MSAA)
    glfwWindowHint(GLFW_STENCIL_BITS, 8);                       // stencil masks (pond reflections)

    GLFWwindow* window = glfwCreateWindow(START_W, START_H, "Enchanted Garden", NULL, NULL);
    if (window == NULL)
    {
        cout << "Window creation failed!" << endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);                                        // vsync
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        cout << "GLEW initialization failed!" << endl;
        glfwTerminate();
        return -1;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);

    font::init();
    game::init();
    theme::init();
    hospital::init();
    playground::init();
    station::init();

    if (testHour >= 0.0f) theme::setOverrideHour(testHour);
    initMountain();
    initGarden();
    initializeAnimation(glfwGetTime());

    if (shotMode)
    {
        if (shotTime >= 0.0) seekAnimation(shotTime, glfwGetTime());   // --shot -1 = title screen (logo fully shown)
        else { for (int i = 0; i < 300; i++) updateAnimation(glfwGetTime() + i * 0.02); }
        theme::snap();
        if (testScene)
        {
            const string sc = testScene;
            if (sc == "garden")          scenes::current = SceneId::Garden;
            else if (sc == "playground") scenes::current = SceneId::Playground;
            else if (sc == "station")    scenes::current = SceneId::Station;
            else if (sc == "hospital")   scenes::current = SceneId::HospitalOutside;
            else if (sc == "inside")     scenes::current = SceneId::HospitalInside;
        }
        scenes::mapOpen = openMap;
        if (simSeconds > 0.0)                                   // run the garden simulation forward (for tests)
        {
            ui::mouse.valid = haveCursor; ui::mouse.x = curX; ui::mouse.y = curY;
            const AnimState& as = getAnimState();
            for (double t = 0.0; t < simSeconds; t += 1.0 / 60.0)
            {
                gardensim::update(1.0 / 60.0, as, ui::mouse, scenes::isIntroOrGarden() && as.gardenBlend >= 1.0f);
                scenes::updateLocation(1.0 / 60.0);
                game::update(1.0 / 60.0);
            }
        }
        if (doClick)                                            // simulate one left click at world position X,Y
        {
            for (int r = 0; r < repeat; r++)                    // --repeat N: N clicks, 0.5 s apart
            {
                ui::mouse.valid = true; ui::mouse.x = clickX; ui::mouse.y = clickY; ui::mouse.pressed = true;
                const AnimState& as = getAnimState();
                gardensim::update(1.0 / 60.0, as, ui::mouse, scenes::isIntroOrGarden() && as.gardenBlend >= 1.0f && !scenes::mapOpen);
                scenes::updateLocation(1.0 / 60.0);
                game::update(1.0 / 60.0);
                ui::mouse.pressed = false;
                if (r + 1 < repeat)
                    for (double t = 0.0; t < 0.5; t += 1.0 / 60.0)
                    {
                        scenes::updateLocation(1.0 / 60.0);
                        game::update(1.0 / 60.0);
                    }
            }
            renderFrame(window, shotTime);
            ui::endFrame();
            const AnimState& as2 = getAnimState();
            for (double t = 0.0; t < afterSeconds; t += 1.0 / 60.0)      // let the click play out
            {
                gardensim::update(1.0 / 60.0, as2, ui::mouse, scenes::isIntroOrGarden() && as2.gardenBlend >= 1.0f);
                scenes::updateLocation(1.0 / 60.0);
                game::update(1.0 / 60.0);
            }
        }
        renderFrame(window, shotTime);
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        savePPM(shotFile, w, h);
        glfwTerminate();
        return 0;
    }

    double fpsTimer = glfwGetTime(), prev = fpsTimer;
    int    frames   = 0;

    while (!glfwWindowShouldClose(window))
    {
        const double now = glfwGetTime();
        // frame-time smoothing: clamp spikes (window drag, alt-tab) and low-pass filter the
        // step with an exponential moving average, so simulations move evenly (no stutter)
        static double dtSmooth = 1.0 / 60.0;
        const double raw = std::min(0.1, std::max(0.0, now - prev));
        dtSmooth += (raw - dtSmooth) * 0.2;
        const double dt = dtSmooth;
        prev = now;

        theme::update(dt);                                      // follows the device clock
        updateAnimation(now);
        updateMouse(window);
        game::update(dt);
        if (getAnimState().waiting && ui::mouse.pressed && ui::mouse.valid)   // title screen: the first click starts the film
        {
            startFilm(now);
            ui::mouse.pressed = false;                          // this click does nothing else
        }
        {
            const AnimState& as = getAnimState();
            const bool interactive = scenes::isIntroOrGarden() && as.gardenBlend >= 1.0f && !scenes::mapOpen;
            gardensim::update(dt, as, ui::mouse, interactive);
            scenes::updateLocation(dt);
        }
        renderFrame(window, now);
        ui::endFrame();

        glfwSwapBuffers(window);
        glfwPollEvents();

        // window title: location, stage, mode, time, fps, culling statistics
        frames++;
        const double t2 = glfwGetTime();
        if (t2 - fpsTimer >= 0.5)
        {
            char title[320];
            const AnimState& s = getAnimState();
            snprintf(title, sizeof(title),
                     "Enchanted Garden | %s | stage %s%s | %s | t=%.1fs | %.0f FPS | culling %s (drawn %d, skipped %d)",
                     scenes::name(scenes::current), phaseName(s.phase), s.paused ? " [PAUSED]" : "",
                     theme::modeName(theme::mode()), s.time, frames / (t2 - fpsTimer),
                     gfx::cullingEnabled ? "ON" : "OFF", gfx::statDrawn, gfx::statCulled);
            glfwSetWindowTitle(window, title);
            fpsTimer = t2;
            frames   = 0;
        }
    }

    glfwTerminate();
    return 0;
}
