# IC_2DE

IC_2DE is a Windows-first C++20, 2D-first 2.5D engine. Its gameplay World uses true X/Y/Z positions, while characters and props remain crisp 2D billboards projected through an orthographic oblique camera. Raylib supplies the platform and graphics implementation while engine-facing interfaces remain owned by IC_2DE.

## Current checkpoint

Framebuffer, post-processing, and telemetry checkpoint:

- reproducible CMake build with raylib 6.0 pinned;
- a small application interface that hides raylib;
- fixed 60 Hz simulation with interpolation and bounded catch-up;
- unlocked rendering by default with optional command-line caps;
- camera-relative X/Z keyboard and gamepad movement with normalized diagonals;
- pause, single-step, and reset controls;
- 640 x 360 virtual canvas with integer upscaling and letterboxing;
- an engine-owned ping-pong framebuffer pipeline that hides render targets, texture orientation, shader uniforms, fallback, presentation, and GPU release order;
- a packaged external post-process shader with exposure, saturation, and vignette controls plus F7 and command-line bypass;
- uncapped, active-monitor VSync, and explicit-Hz presentation modes;
- bounded CPU job workers with queued and parallel-range work;
- a grid-free GPU gradient backdrop with a CPU clear fallback;
- an EnTT-backed World hidden behind engine-owned entity and snapshot interfaces;
- an engine-owned X/Z ground plane with +Y elevation;
- a tested orthographic 2.5D projection with independent yaw, pitch, scale, and zoom;
- immutable Render2D frame descriptions with explicit projected-depth ordering;
- feet-anchored 2D billboards, ground shadows, and an elevation demonstration;
- immutable projected ground-quad submissions rendered separately from billboards;
- a single bounded, world-aligned ground grid without the conflicting screen-space box grid;
- engine-owned X/Z walkable bounds, solid footprints, discrete elevation, and trigger queries;
- a deterministic smoke route that climbs, slides around a wall, and enters a trigger;
- Box2D 3.1.1 pinned behind engine-owned generational handles and a 32 pixels-per-metre policy;
- static footprints, a kinematic player, sensor, dynamic crate, layers/masks, and safe body destruction;
- buffered engine-owned contact and trigger events copied after each fixed physics step;
- a typed `EngineEvent` stream for copied contact, trigger, and animation events, with persistent entity UUIDs carried across the runtime seam;
- an owned `LayerStack` with attach/detach lifecycle, regular/overlay ordering, handled-event propagation, fixed updates, and deferred structural changes;
- interpolated World synchronization and Render2D physics-footprint diagnostics;
- strict scene schema 7 for stable entity UUIDs, ground, textures, generated radial assets, Aseprite imports, sprites, prefabs and instances, physics bodies, role bindings, animation clips, frames, and eight-way locomotion maps;
- Hazel-inspired persistent entity identity plus deterministic World snapshot/restore, with EnTT and transient handles remaining private;
- reusable prefab definitions with per-instance identity and field overrides, expanded into ordinary entities in authored order so runtime code learns no new concept;
- a small mutable `SceneDocument` interface with UUID-addressed edits, prefab instantiation and removal, validation-before-replace atomic saves, explicit schema migration, and isolated unsaved runtime copies;
- a `SceneEditor` command seam with bounded undo/redo, candidate-copy atomicity, and unsaved-state reporting;
- an F1 debug-visual master switch over independent collision, trigger, elevation-map, world-grid, and statistics channels;
- an F2 development editor shell with a docked viewport, hierarchy, inspector, command history, statistics, and debug-channel panels, built on a pinned Dear ImGui and an engine-owned raylib backend;
- pre-window scene validation with file/line diagnostics, safe relative texture paths, and cross-reference checks;
- a `RuntimeScene` owner that constructs and synchronizes GroundMap, PhysicsWorld, World, and scene textures;
- player, primary prop, and enemy bodies authored in data rather than identified by the application loop;
- a permanent independent scene fixture proving the format is not tied to the packaged test area;
- an engine-owned integer-tick `AnimationPlayer` with loop, once, ping-pong, pause, reset, clip switching, and frame events;
- a narrow Aseprite JSON-array adapter that translates atlas paths, frame rectangles, millisecond timing, tag ranges, all four directions, and optional frame events into engine-owned clips;
- original transparent cardinal and diagonal player atlases providing sixteen idle/walk frames;
- physics-derived player idle/eight-direction movement with tested 22.5-degree sector boundaries and retained facing on release or obstruction;
- a reusable code-generated radial-alpha texture replacing the player's previous hard rectangular shadow;
- the enemy using the same authored locomotion interface and render-snapshot frame selection;
- animation kept presentation-only so it cannot modify physics or deterministic replay state;
- identical 300-tick replay hashes at 30, 60, 120, monitor-synced, and uncapped presentation rates;
- camera transforms, conservative sprite culling, and submission/batch diagnostics;
- rolling 240-frame p50/p95/p99 frame-time telemetry plus estimated draw, batch, vertex, render-target, shader, and GPU-pass diagnostics in the overlay and editor;
- generational texture handles, path/generated-asset caching, reference counts, and a fallback texture;
- versioned runtime-project manifests with package-relative content paths;
- separate development and shipping executables, a development-tools-off shipping preset, and static MSVC runtime linkage;
- one-command minimal folder/ZIP packaging with a real GPU runtime smoke test;
- headless clock, frame telemetry, flow/layers, input, project, GroundMap, Physics2D, animation, Aseprite, scene, presentation, jobs, World, and Render2D tests through CTest;
- project warnings treated as errors.

## Build on Windows

Visual Studio with the Desktop development with C++ workload is required. The helper locates Visual Studio's bundled CMake and Ninja, prepares the MSVC environment, configures, builds, and optionally tests or launches.

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1 -Configuration Debug -RunTests -Launch
```

The first configure downloads pinned raylib, EnTT, Box2D, and nlohmann JSON sources into the ignored `build` directory. Development presets also download pinned Dear ImGui for the editor; the shipping preset does not.

Each preset writes three executables into its own `build/<preset>` directory:

| Output | Built by | Contains |
|---|---|---|
| `ic2de_testbed.exe` | `windows-debug`, `windows-release` | The game plus development tools. F1 toggles debug visuals, F2 opens the editor. |
| `IC_2DE-Editor.exe` | `windows-debug`, `windows-release` | The same build with the editor already open at startup. |
| `IC_2DE.exe` | `windows-shipping` | The clean shipping runtime with every development tool compiled out. |

The debug and release presets also produce their own `IC_2DE.exe`, which is the shipping entry point compiled at that configuration rather than the shipping preset's tools-off build.

For a deterministic window/render smoke run that closes itself and captures a frame:

```powershell
.\build\windows-debug\ic2de_testbed.exe --smoke-window
```

Rendering is uncapped by default. Temporary caps are available for frame-rate checks:

```powershell
.\build\windows-debug\ic2de_testbed.exe --fps=30
.\build\windows-debug\ic2de_testbed.exe --fps=60
.\build\windows-debug\ic2de_testbed.exe --fps=120
.\build\windows-debug\ic2de_testbed.exe --monitor-hz
```

Testbed controls: W/A/S/D or the arrow keys move across the X/Z ground plane, P pauses, O advances one fixed tick while paused, R resets, F1 toggles the debug visuals, F2 toggles the development editor, F6 cycles render pacing, F7 toggles post-processing, G toggles the GPU backdrop/CPU fallback, and Escape quits. Development builds only: the shipping runtime compiles the editor, debug overlay, and live toggles out.

F1 is a master switch over independent debug channels - collision shapes, trigger volumes, the elevation map, the world grid, and the statistics overlay - so a development build can present exactly what ships. The `lights` channel is listed but unavailable until a lighting module exists. F2 opens the docked editor shell: hierarchy, inspector, command history, statistics, debug channels, and the game rendered into a viewport panel. The character stays drivable while the shell is open; movement keys are withheld only while an editor field is being typed in or dragged. Escape does not quit while the shell is visible, so cancelling a text edit cannot close the window - hide the editor with F2 first, or use the window close button. Every edit it makes goes through `SceneEditor`, so undo, validation, and atomic saving behave identically to any other caller, and "Apply to running scene" swaps in a validated runtime copy without touching the authored file.

```powershell
.\build\windows-debug\ic2de_testbed.exe --editor
.\build\windows-debug\ic2de_testbed.exe --no-debug-visuals
.\build\windows-debug\ic2de_testbed.exe --no-post-process
```

For a deterministic moving-camera and culling smoke run:

```powershell
.\build\windows-debug\ic2de_testbed.exe --monitor-hz --smoke-movement
```

Verify that the same Release build reaches one identical state at every presentation mode:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify-replay.ps1 -Configuration Release
```

Build a minimal movable runtime folder and ZIP, then launch its automated shipped test:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\package.ps1 -RunSmoke
```

Outputs are written to `dist/windows-x64` and `dist/IC_2DE-Windows-x64.zip`.
The combined folder contains `IC_2DE.exe` for the clean shipping runtime,
`IC_2DE-Debug.exe` for the Debug development overlay, and `IC_2DE-Editor.exe`
for the Release development editor. All three use the same adjacent
`IC_2DE.runtime` manifest and `Content` directory and can be double-clicked
without setting a working directory. Scene edits saved from the packaged editor
are written to that folder's `Content` copy, never back to the source checkout.

## Documents

- `docs/ENGINE_CHARTER.md` - the current product and technical assumptions;
- `docs/DATA_FORMATS.md` - authored runtime manifest and scene schema contracts;
- `docs/checkpoints/2026-08-30-foundation.md` - build, test, and runtime evidence for the first checkpoint;
- `docs/checkpoints/2026-08-30-input-presentation.md` - unlocked rendering, input, and virtual-canvas evidence;
- `docs/checkpoints/2026-08-30-world-offload.md` - monitor pacing, CPU jobs, GPU grid, and World evidence;
- `docs/checkpoints/2026-08-30-render2d-assets.md` - frame submission, camera, culling, batching, and texture lifetime evidence;
- `docs/checkpoints/2026-08-30-pokemon-25d-world.md` - XYZ World, oblique projection, depth sorting, and movement evidence;
- `docs/checkpoints/2026-08-30-ground-surfaces-collision.md` - ground quads, corrected grid, footprints, elevation, triggers, and runtime evidence;
- `docs/checkpoints/2026-08-30-shipping-runtime-hazel.md` - Hazel-derived runtime separation, project paths, packaging, and shipped evidence;
- `docs/checkpoints/2026-08-30-physics2d.md` - Box2D seam, buffered events, dynamic crate, replay hashes, and runtime evidence;
- `docs/checkpoints/2026-08-30-authored-scenes.md` - scene schema, runtime ownership, validation, fixtures, and package evidence;
- `docs/checkpoints/2026-08-30-animation-runtime.md` - deterministic clips, locomotion bindings, frame events, replay, and package evidence;
- `docs/checkpoints/2026-08-31-aseprite-content-pipeline.md` - JSON-array adapter, real atlas, schema 4, and runtime evidence;
- `docs/checkpoints/2026-08-31-eight-way-locomotion-shadow.md` - schema 5, eight-direction animation, diagonal art, and the soft-shadow repair;
- `docs/checkpoints/2026-08-31-hazel-identity-scene-copy.md` - schema 6 persistent UUIDs, deterministic World snapshots, and the expanded non-audio Hazel roadmap;
- `docs/checkpoints/2026-08-31-relocatable-debug-combined-package.md` - double-click startup repair and the combined Debug/Shipping folder;
- `docs/checkpoints/2026-08-31-scene-document-atomic-save.md` - UUID editing, runtime copies, explicit migration, atomic saving, and package evidence;
- `docs/checkpoints/2026-08-31-prefabs-scene-commands.md` - schema 7 prefabs, stable instances, overrides, and undoable scene commands;
- `docs/checkpoints/2026-08-31-debug-channels-editor-shell.md` - F1 debug channels and the F2 docked development editor;
- `docs/checkpoints/2026-08-31-typed-events-layer-stack.md` - copied typed runtime events, owned layers, deferred transitions, and package evidence;
- `docs/checkpoints/2026-08-31-frame-pipeline-telemetry.md` - ping-pong framebuffers, external post-processing, frame distributions, renderer diagnostics, and package evidence;
- `docs/references/HAZEL_ADAPTATION.md` - explicit adopt, adapt, and defer decisions from the Hazel reference;
- `docs/references/GAME_ENGINE_SYSTEMS_REPORT_ADAPTATION.md` - adopt, adapt, and defer decisions from the full-stack engine report;
- `IC_2DE_EXECUTION_PLAN.md` - milestone roadmap and quality gates;
- `2d-cpp-raylib-engine-plan.md` - original research and technology survey.
