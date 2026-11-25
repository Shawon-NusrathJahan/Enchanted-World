# Enchanted World

CSE-4742 Computer Graphics Lab project (IIUC). A 2D OpenGL animation and mini-game in C++17 with OpenGL 2.1, GLFW and GLEW.

You start on a cinematic mountain title. The film flies over the peaks, then the mountains part to reveal a busy village seen from above. The village has shops, a market, a town hall, a station with a train, a hospital, a playground, villagers, and a river running from the mountain waterfall through the pond to the bottom edge. In the middle of the village a big **fair** is on: Ferris wheel, carousel, circus big top, chair swing, helter-skelter, a band on stage, stalls and crowds, with lights and fireworks at night. The great **clock tower** shows the real time and has a bell that swings on the hour. The village also has a marketplace, a police station, a detective agency, a fire station, a school, a library and a post office; these are future scenes. The film **waits there until you click**, then zooms into the garden. From the garden, the world map takes you to the playground, the station and the hospital. The map shows the same layout as the village. One score is shared by every place.

## Build (Windows, MSYS2 UCRT64)

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,glfw,glew}
cd EnchantedWorld
mkdir build && cd build
cmake .. -G Ninja
ninja
./EnchantedGarden.exe
```

If CMake complains that the cache belongs to another folder, delete `build/` and run the commands again.

Tests (no window needed):

```bash
cmake .. -G Ninja -DBUILD_TESTS=ON
ninja
./garden_sim_test ; ./playground_sim_test ; ./station_sim_test ; ./hospital_sim_test
```

## Controls

| Where | What you can do |
|---|---|
| Title | Nothing moves until you **click**. Then the film starts. |
| Village view | The film pauses here. Hover a building to see its name. **Click anywhere** to continue into the garden. |
| Anywhere | **Restart** (circular arrow, top-left) goes back to the title screen and keeps your score. **Map** (top-right): an illustrated map of the world. The places you can visit are in the middle, drawn with the same art as the village, each with its scene score. They are joined by the real village roads. Mirror Pond and Silver River are on the left. Point at a small place to see what is coming soon; click a big one to travel there. Keys `0` (title) and `1`–`5` jump between film phases. |
| Garden | **Carry the gardener** like the playground children: press and drag him, then let go.<br>• Drop him on the first stepping stone in front of the gate: he goes **into the house**.<br>• Drop him on the house itself: he stays **outside**.<br>• While he is inside, press the house **wall** (not the roof): he comes out into your hand.<br>• Dropped in the lake, he wades to the shore. Dropped on the bridge, he stands on it.<br>He crosses the bridge to water the far bank, and the gate opens when he passes. Hover the butterflies (they flee), click the water (ripples) or the windmill (a gust of wind). |
| Playground | Click a swing to push it. Pushing too high makes the child fall: injured, sent to hospital, −points. Drag the **safety mat** under the swing or slide end to catch falls. Click the slide to wax it (faster; children fly off the end). Drag the ball to throw it (goal = +points; hitting a child hurts them). Drag children onto rides. |
| Train station | Click a train to open **DEPART NOW / HOLD +10 s**. Both work at any time, also during boarding. Drag a train along its track: signals and the train ahead still stop it. Drag a passenger onto the right train (+) or the wrong one (−). Click signals, points, bridge and entrance. The departure board shows **ON TIME / LATE / EARLY / HELD / PLAT CHANGE**. A yellow banner announces every timetable change. |
| Hospital | Outside: drag a person who looks unwell onto the building. Inside: drag patients to **Emergency** (bleeding, broken bones, surgery) or **Consultation** (fever, cough, check-up). Injured playground children arrive here. When healed they return to the playground. |

## Scoring (all values in the `*_config.h` files)

The HUD shows two scores:
- **Total** (gold): everything you earned or lost anywhere. It is the same in every scene.
- **Scene score** (coloured, with the scene's icon): only what happened in the scene you are in. When you change scene, it switches to that scene's own score.

The flying "+N" tokens land on the scene score, and the Total changes with it.

| Event | Points |
|---|---|
| Hospital: right room / wrong room | +5 / −3 |
| Hospital: sick person admitted / healthy person dragged in | +3 / −2 |
| Hospital: patient healed / playground child healed / patient worsened | +2 / +5 / −1 |
| Playground: your goal / children's goal / happy ride / healed child returns | +10 / +3 / +2 / +5 |
| Playground: mat catches a fall / super slide landed on the mat | +4 / +6 |
| Playground: swing fall / slide crash or hard ball hit / ball bonk / see-saw bump | −15 / −12 / −6 / −4 |
| Station: departure on time, everyone aboard / late, everyone aboard | +3 / +1 |
| Station: per passenger left behind / per passenger who could not get off | −1 (max 4) / −1 (max 3) |
| Station: passenger put on right train / wrong train | +2 / −2 |
| Station: early arrival / arrival more than 10 s late | +1 / −1 |

The score, progress and consequences stay through scene changes and restarts. They reset only when the program is closed. The score can go below zero.

## Project layout

```
src/        all source (scene drawing *.cpp; simulations *sim.cpp have no OpenGL)
src/cgmath.h      reusable graphics maths (matrices, splines, clipping, noise, collisions)
src/font.cpp      own vector-font rasteriser + logo effects (no font library)
src/game.*        shared score, event feed, cross-scene injury queue
src/world.h       ONE layout of the world (village, water, places), used by the film AND the map
src/*_config.h    names, timings and score values you can tune
tests/      headless simulation tests
tools/      gen_fontdata.py (font outlines -> src/fontdata.h)
assets/fonts      font licences (SIL OFL)
CG_ALGORITHMS.md  every computer-graphics algorithm and where it is used
```

Screenshot mode, for the report:

```bash
./EnchantedGarden --shot 40 out.ppm --hour 12 --scene station --simulate 10
```

The `--scene` value can be `garden`, `playground`, `station`, `hospital` or `inside`. `--shot -1` renders the title screen.
