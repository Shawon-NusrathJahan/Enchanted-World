# Enchanted World — Computer-Graphics Algorithms

CSE-4742 Computer Graphics Lab · C++17 · OpenGL 2.1 (immediate mode) · GLFW + GLEW

This file maps every computer-graphics algorithm in the project to the code that uses it.
Use it as the source for the report. Reusable maths is in `src/cgmath.h` (namespace `cg`, no OpenGL).

---

## 1. Geometric transformations

| Algorithm | Where | What it does |
|---|---|---|
| 2D homogeneous matrices (`cg::Mat3`: translate, scale, rotate, compose, inverse) | `cgmath.h` | All 2D transforms as 3×3 matrices, p' = M·[x y 1]ᵀ. |
| Reflection matrix `Mat3::reflectY(y0)` = T(0,y0)·S(1,−1)·T(0,−y0) | `garden.cpp` (pond) | Mirrors the cliff, waterfall and bridge about the water line to make the pond reflection. Loaded with `glMultMatrixf`. |
| Layered parallax camera: screen = Z^f · (p − f·C) | `mountain.cpp` (`pushLayer`, `cameraAt`) | Each layer has a depth f in 0..1. Far layers move and zoom less than near ones, which gives depth while the camera zooms. |
| Zoom-to-point camera: C = (T − S / Z^f) / f | `mountain.cpp` | Keeps a chosen world point (a peak, then the village) fixed at a moving screen position while zooming. |
| Mountains part left/right | `mountain.cpp` (`drawMountainScene`) | Translates the two giant ranges outward with an eased offset to reveal the plateau. |
| Elevated aerial view (pseudo-perspective) | `world.h` (`depthScale`), `village.cpp` | Ground objects are scaled by s(y) = 0.62 − 0.80·y. Nearer objects (lower on screen) are bigger, so the village reads as seen from above at an angle. Roads widen toward the viewer, and so does the river. |
| Skew reflection y' = 2(ax + b) − y | `garden.cpp` (bridge reflection) | Water is horizontal, so each deck point is mirrored vertically about the water line under the bridge. That line is slanted, so the transform is the affine matrix [1 0 0; 2a −1 2b]. |
| Gate swing as a projected rotation | `garden.cpp` (fence gate) | A leaf turning by θ about its vertical hinge projects to width w·cos θ. The opening is eased by the simulation. |
| One shared world layout + map transform | `world.h`, `scenes.cpp` (`mapX`, `mapY`) | The film's village and the world map read the same coordinates (`world::PLACES`). The map uses one uniform scale about the centre of the visitable places, M(p) = k·(p − c). The places therefore sit in the middle, and every distance and road between them keeps its real proportions. |
| Fair rides (village) | `village.cpp` (`drawFerrisWheel`, `drawCarousel`, `drawChairSwing`) | Ferris wheel: spokes rotate, but each gondola is placed with the rotation and stays upright (it does not inherit the rotation). Carousel and chair swing: riders move on an ellipse (x = r cos a, depth = r sin a). They are drawn in two passes, back half then front half, which is a painter's order for a rotating ring. |
| Additive light bulbs + stateless fireworks | `village.cpp` (`bulb`, fireworks) | In the evening, ride bulbs get additive glows. Each firework's rockets and sparks are a closed-form function of time (launch, burst radius ∝ √t, gravity sag), so no particle state is stored. |
| Hierarchical transforms (push/pop matrix) | `garden.cpp` (windmill sails, butterflies, gardener), `playground.cpp` (swing pendulum, see-saw) | Child parts rotate about their parent's pivot. |

## 2. Curves and animation

| Algorithm | Where | What it does |
|---|---|---|
| Kochanek–Bartels (TCB) Hermite spline in log-zoom space | `mountain.cpp` (`buildKeys`, `splineAt`), `cg::hermite`, `cg::kbTangent` | Smooth camera path through 3–5 peak close-ups, the reveal and the village. Interpolating log(zoom) makes the zoom speed feel uniform. Tension slows the camera near each key. |
| Smootherstep easing (6t⁵ − 15t⁴ + 10t³) | `animation.cpp` | C²-continuous fades and transitions (title fade, cross-fades, garden blend). |
| Two clocks: film clock + ambient clock | `animation.cpp` | The camera follows the film clock, which stops at the village until the user clicks. Water, people, smoke and the train follow an always-running clock, so the paused village stays alive. |
| Monotone zoom path | `mountain.cpp` (`buildKeys`) | The camera's log-zoom only ever increases (no zoom-out before the reveal). The village frame keeps the last peak's zoom and tilts down onto the plateau. |
| easeOutBack | `font.cpp` (`drawLogo`) | Per-letter "pop" entrance of the title logo. |
| Quadratic / cubic Bézier curves | `cgmath.h`; `font.cpp` (logo flourishes); `game.cpp` + `game_hud.cpp` (flying "+N" score tokens); `mountain.cpp`, `garden.cpp` (water ribbons) | Curved shapes and motion paths. |
| Arc-length re-parameterisation (`cg::ArcLength`) | `mountain.cpp` (mountain cascade, stream, ledge waterfall) | Converts a curve parameter to distance, so water streaks flow at constant speed along the Bézier ribbon. The water course is continuous: cascade → stream → waterfall → pond → river off the bottom edge. |
| Frame-time smoothing (clamp + exponential moving average) | `main.cpp` | Filters frame-time spikes so every simulation moves evenly. |

## 3. Procedural modelling

| Algorithm | Where | What it does |
|---|---|---|
| Midpoint displacement (Fournier–Fussell–Carpenter) | `cg::midpointDisplacement`, `mountain.cpp` (`makeRange`) | Fractal mountain ridgelines. The random range halves (by 2^−roughness) at each subdivision. |
| Value noise + fBm (fractal Brownian motion) | `cg::valueNoise`, `cg::fbm`; `mountain.cpp` | Ridge roughness, snow lines, rock streaks, cloud-bank edges. |
| Integer hash (`cg::hashU`) | everywhere | Deterministic "random" placement, so the same scene renders every frame without stored data. |
| Painterly light/shadow spines, snow caps, aerial perspective | `mountain.cpp` (`buildRangeGeometry`, `hazed`) | Each peak gets a lit side and a shadow side. Colour is mixed toward the sky with distance, which gives the bluish-but-not-all-blue depth layering. |

## 4. Rasterisation and text (no font library)

| Algorithm | Where | What it does |
|---|---|---|
| Outline extraction from TrueType fonts (offline) | `tools/gen_fontdata.py` → `src/fontdata.h` | Stores glyph outlines (move / line / quadratic Bézier) as data. Fonts: Cinzel Decorative, Fredoka (SIL OFL, see `assets/fonts`). |
| Adaptive de Casteljau subdivision | `font.cpp` (`flattenQuad`) | Flattens Bézier contours into line edges until they are flat to a tolerance. |
| Scan-line polygon fill with Edge Table / Active Edge Table, non-zero winding rule | `font.cpp` (`rasterize`) | Fills glyph outlines, including the holes in "o", "e" and "a". |
| Anti-aliasing: 4 sub-scanlines + exact horizontal span coverage | `font.cpp` (`rasterize`) | Smooth edges without a GPU multisample buffer. |
| Shelf packing of a texture atlas | `font.cpp` (`buildUiAtlas`) | All UI glyphs live in one texture. |
| Software mip-maps (2×2 box filter) | `font.cpp` | Clean small text. |
| Morphological dilation, separable box blur ×3 (≈ Gaussian), offset bevel mask | `font.cpp` (`buildLogo`: `dilate`, `boxBlur`) | Outline, glow and bevel highlight of the "Enchanted World" logo. |
| Additive light sweep | `font.cpp` (`drawLogo`) | The shine that runs across the logo. |

## 5. Clipping, masking and visibility

| Algorithm | Where | What it does |
|---|---|---|
| Sutherland–Hodgman polygon clipping | `cg::clipPolygonSH`; `mountain.cpp` (sun light shafts) | Each 2.6-unit light-shaft wedge is clipped to the view rectangle before drawing, and per-vertex alpha is recomputed from distance to the sun. Same image, less fill. |
| Cohen–Sutherland line clipping | `cg::clipLineCS`; `station.cpp` (passenger route line) | Route segments are clipped to the play area so the line never crosses the score bar. |
| Point in polygon (ray casting, even-odd) | `cg::pointInPolygon`; `hospitalsim.cpp` (`overHospital`); `gardensim.cpp` (`inLake`) | "Dropped on the hospital" is tested against the building's real stepped silhouette. The garden lake is a free-form shore polygon that runs off the screen edges; clicks, ripples and the gardener are tested against it. |
| Nearest point on a polyline (projection onto segments) | `gardensim.cpp` (`pushOutOfPond`) | A gardener dropped into the water moves to the closest shore point, plus a small step onto land. |
| Star-shaped polygon fill (triangle fan from a kernel point) | `garden.cpp` (lake) | The shore is star-shaped as seen from an off-screen corner, so one triangle fan fills it. The same fan, grown outward, draws the sand and mud banks. |
| Arc-length sampling with minimum spacing | `garden.cpp` (shore stones) | Stones are placed every 0.11 units of shore length, so they can never overlap. Gaps are left at the path, the bridge feet and the filling spot. |
| Scissor test | `scenes.cpp` (world map) | Everything painted on the map is clipped to the parchment rectangle with `glScissor`. |
| Inverse layer transform (picking) | `mountain.cpp` (`drawPlateau`) | The mouse position is mapped back into plateau coordinates (inverse scale and pan), so hovering a building in the zoomed village shows its name. |
| Stencil buffer masking | `garden.cpp` (pond) | Pass 1 writes the pond shape into the stencil (value 1). Reflections and ripples are then drawn with `GL_EQUAL 1`, so they appear only inside the water. |
| Painter's algorithm (depth sort by y) | `hospital.cpp`, `playground.cpp`, `station.cpp`, `village.cpp` | People, buildings, trees and the train are sorted back-to-front each frame. |
| Catmull–Rom splines | `village.cpp` (roads), `garden.cpp` (stepping-stone path) | Smooth roads and paths through a few control points. |
| Display lists (geometry caching) | `mountain.cpp` (ranges per tint, plateau), `garden.cpp` (static scenery) | Static geometry is compiled once with `glNewList` and replayed with `glCallList`. |

## 6. Lighting and effects

| Algorithm | Where | What it does |
|---|---|---|
| Additive blending glows (`GL_SRC_ALPHA, GL_ONE`) | `gfx.h` (`glow`), sun, lamps, windows | Light sources and halos. |
| Time-of-day colour grading (global tint) | `theme.cpp` | Sky gradients and a scene tint follow the device clock (dawn / day / evening / night). |
| Stateless analytic particle systems | `mountain.cpp` (`spray`), `garden.cpp` (splash) | Each particle's position is a closed-form function of time and its index, so no particle state is stored and the film can be scrubbed to any time. |
| Gradient fills | `gfx.h` (`vGradient`, `rectV`) | Sky, water and walls. |

## 7. Collision detection and physics (animation)

| Algorithm | Where | What it does |
|---|---|---|
| Circle–capsule collision + impulse response | `cg::circleCapsule`; `playgroundsim.cpp` (`ballHitsKids`) | The ball against each child's body. The ball reflects about the contact normal with restitution. |
| Projectile motion | `playgroundsim.cpp` (ball, falling child), `startFall` | Children thrown off a swing or a waxed slide follow a parabola. A safety-mat test decides the outcome. |
| Pendulum (semi-implicit Euler, damping, pumping) | `playgroundsim.cpp` (`updateSwings`) | Swing physics. The test checks the period against 2π√(L/g). |
| Lever law / torque | `playgroundsim.cpp` (`updateSeesaw`) | See-saw balance. |
| Inclined plane with friction | `playgroundsim.cpp` (slide) | Slide speed. Wax lowers the friction. |
| Bounce with restitution | `playgroundsim.cpp` (`updateBall`) | Apex height falls by e² per bounce. |
| Train kinematics: exact stopping v² = 2ad | `stationsim.cpp` (`drive`) | Trains brake to stop exactly at a target. |
| Interlocked dragging along a rail | `stationsim.cpp` (`Train::manual`) | A train pulled by the mouse keeps a collision gap to the train ahead and cannot pass a red signal. |
| Reynolds steering (containment, escape planning, turn-rate and acceleration limits) | `gardensim.cpp` (`updateButterfly`) | Butterflies flee the cursor smoothly: 16 test headings, limited angular speed, wall collision response. |

## 8. Supporting algorithms (game logic)

A* on a waypoint graph (`pathfind.h`, gardener: door → gate → paths → bridge → far bank) · Dijkstra on the station walkway graph (passengers re-route when the bridge or crossing closes) · priority queue with ageing (hospital triage) · greedy interval partitioning (platform planning) · FIFO queues (rides, doors) · finite-state machines (ambulances, signals, children, trains) · Poisson arrivals (station passengers).

---

## Tests

`cmake -DBUILD_TESTS=ON` builds 4 headless tests (no window). All pass under AddressSanitizer and UBSan.
The tests check the physics against closed-form results (pendulum period, v²/2g apex, e² bounce, inclined plane), the consequences (falls, mat saves, injured child → hospital → back), scoring, and the dispatcher rules (hold, depart now, red-signal interlock while dragging).
