# IC_2DE

IC_2DE is a Windows-first C++20, 2D-first 2.5D engine. Its gameplay World uses true X/Y/Z positions, while characters and props remain crisp 2D billboards projected through an orthographic oblique camera. Raylib supplies the platform and graphics implementation while engine-facing interfaces remain owned by IC_2DE.

## Current checkpoint

Editor enemy-stress checkpoint:

- reproducible CMake build with raylib 6.0 pinned;
- a small application interface that hides raylib;
- fixed 60 Hz simulation with interpolation and bounded catch-up;
- unlocked rendering by default with optional command-line caps;
- camera-relative X/Z keyboard and gamepad movement with normalized diagonals;
- pause, single-step, and reset controls;
- device-independent aim, fire, reload, dodge, interact, weapon-swap, and extraction-choice actions with stable pressed/held/released states;
- a deterministic Combat command buffer with stable actor UUIDs, strict sequential ticks, copied intent/result events, reset-safe snapshots, and render-rate aim coalescing;
- Combat-owned dodge state with a frozen normalized world direction, 12-tick active window, 9-tick invulnerability window, 36-tick cooldown, exact ready-tick behavior, per-actor invulnerability queries, copied start events, and reset-safe counters;
- collision-resolved dodge locomotion at 3x authored player speed: ordinary input cannot bend an active dodge, while retained movement direction wins over stationary aim and the initial facing supplies a deterministic fallback;
- data-defined needle-pistol state with a 12-round magazine, 48 reserve rounds, fixed-tick cooldown/reload timing, and stable copied projectile-spawn results;
- render-rate-independent held fire: LMB state changes are latched per actor and fixed ticks produce repeated shots until release, including across 30 FPS and unlocked presentation;
- an engine-owned projectile simulation with buffered Combat hand-off, interpolated world X/Z travel, exact tick lifetime expiry, copied endpoint events, and a layered gold/cyan pixel bolt with trail and muzzle flash;
- a nearest-solid Physics2D segment query with engine-owned filters, sensor/owner rejection, copied hit geometry, one-time projectile impact resolution, and stable target/tag/damage telemetry;
- an engine-owned `Health` module with stable target and hit identities, fixed-tick damage reduction, duplicate-hit rejection, clamped health, copied damage/death events, and exactly-once death;
- an engine-owned `EnemyIntent` seam that deterministically acquires the player, chooses pursue/attack intent, produces cooldown-authoritative attack requests, and exposes canonical snapshots without owning path search, collision, damage, or presentation;
- generic collision-resolved kinematic actor motion in `RuntimeScene`, so the application supplies actor intent while GroundMap, Physics2D, World synchronization, and locomotion animation remain scene-owned;
- an engine-owned `GameplayState` seam that validates completed fixed-tick snapshots and produces a schema-versioned, order-stable 64-bit digest over authoritative World, Combat, EnemyIntent, NavAgent, ProjectileSimulation, and Health state while excluding presentation-only data;
- the authored patchwork NPC bound as a 54-health target dummy: three 18-damage needle hits retire its physics and presentation until reset, while the editor viewport and Statistics panel consume read-only health snapshots;
- a separate authored Threadbound Runner that consumes deterministic A* waypoints in eight directions, stops in attack range, applies 12 damage per fixed-tick attack request through `Health`, and respects Combat dodge invulnerability;
- viewport-aware mouse-to-world X/Z aim plus normalized controller aim, with editor-panel clicks excluded from gameplay fire;
- a crisp gold/cyan canvas crosshair for mouse and controller aim, firing-state feedback, and independent eight-way character facing with boundary hysteresis while WASD remains the movement authority;
- ordered LMB held/release transitions that cannot be overwritten by later high-rate mouse-aim samples before the next fixed tick;
- editor input routing that preserves held movement during viewport fire; Ctrl+left-click now performs editor selection while ordinary left-click remains gameplay-only;
- live editor Combat and EnemyIntent telemetry for commands, weapon/dodge state, pursuit state/direction/range, cooldowns, acquisitions, attacks, resolved travel, player damage, blocked motion, and invulnerability rejections;
- 640 x 360 virtual canvas with continuous window fitting and aspect-ratio letterboxing;
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
- engine-owned X/Z walkable bounds, swept axis-separated solid contacts that prevent fast axial tunnelling, discrete elevation, and trigger queries;
- an immutable dense X/Z `NavGrid` derived from GroundMap data, with deterministic half-open world/cell conversion, row-major cells, actor-footprint clearance, 2.5D elevation, hard-blocked solids, max-step neighbors, and no diagonal corner cutting;
- deterministic eight-way A* over the public NavGrid contract, with octile distance, stable heuristic/row/column tie-breaking, explicit endpoint/unreachable statuses, copied start-to-goal paths, world-unit distance, and expansion counts;
- an engine-owned `NavAgentSystem` that turns pursuit facts into cell-centre motion without owning intent or physics, with immediate route recovery and target-cell replans, a bounded 30-tick refresh, a four-world-unit waypoint tolerance, and zero fallback motion for failed paths;
- read-only navigation/grid/path/agent data in the editor Statistics tab, an opt-in teal/red navigation-grid channel, and a focused path channel with teal start, gold steps, and magenta goal; while pursuing, the displayed path is the Runner's active route rather than the standalone reference path;
- a runtime-only editor stress seam that clones the complete authored Runner actor graph before the first fixed tick and offers deterministic total-count presets of 10, 25, 50, 100, and 200 without modifying `test_area.scene`;
- reachable stress placement over the public `NavGrid` contract, with real UUID, World, Physics2D, animation, Health, EnemyIntent, NavAgent, rendering, and collision participation instead of display-only sprites;
- harmless stress combat that retains acquisition, pursuit, attack-state transitions, and attack-request telemetry while suppressing only the final player-damage hand-off; restoring the authored scene restores normal damage;
- compact aggregate crowd telemetry in Statistics, with individual health, intent, and navigation rows collapsed until requested;
- a deterministic smoke route that climbs, slides around a wall, and enters a trigger;
- Box2D 3.1.1 pinned behind engine-owned generational handles and a 32 pixels-per-metre policy;
- static footprints, a kinematic player, sensor, dynamic crate, layers/masks, and safe body destruction;
- buffered engine-owned contact and trigger events copied after each fixed physics step;
- a typed `EngineEvent` stream for copied contact, trigger, and animation events, with persistent entity UUIDs carried across the runtime seam;
- an owned `LayerStack` with attach/detach lifecycle, regular/overlay ordering, handled-event propagation, fixed updates, and deferred structural changes;
- interpolated World synchronization and Render2D physics-footprint diagnostics;
- strict scene schema 10 for stable entity UUIDs, optional sprite depth spans, ground, textures, generated radial assets, Aseprite imports, sprites, prefabs and instances, physics bodies including the explicit `attacker` role, animation clips, frames, eight-way locomotion maps, and offset automatic animations;
- Hazel-inspired persistent entity identity plus deterministic World snapshot/restore, with EnTT and transient handles remaining private;
- reusable prefab definitions with per-instance identity and field overrides, expanded into ordinary entities in authored order so runtime code learns no new concept;
- a small mutable `SceneDocument` interface with UUID-addressed edits, prefab instantiation and removal, validation-before-replace atomic saves, explicit schema migration, and isolated unsaved runtime copies;
- a `SceneEditor` command seam with bounded undo/redo, candidate-copy atomicity, and unsaved-state reporting;
- an F1 debug-visual master switch over independent collision, trigger, elevation-map, world-grid, navigation-grid, navigation-path, compact-HUD, and reserved lighting channels;
- a compact viewport HUD reduced to identity, FPS/pacing, tool shortcuts, and critical pause/stall warnings; detailed engine, rendering, combat, health, enemy, navigation, and replay telemetry now stays in the docked Statistics panel;
- an F2 development editor shell with a docked viewport, hierarchy, inspector, command history, statistics, and debug-channel panels, built on a pinned Dear ImGui and an engine-owned raylib backend;
- a cleaner first-run editor workspace with a larger central viewport, compact side/bottom docks, a searchable hierarchy, categorized debug controls, and tabbed/collapsible telemetry instead of one continuous panel;
- automatic per-user dock-layout persistence in `%LOCALAPPDATA%\IC_2DE\Editor\layout-v1.ini`, explicit save/reset controls in the Workspace menu, and an isolated `--editor-layout=PATH` override for automated or portable runs;
- click-to-select in the viewport, resolved against the running scene in the renderer's own draw order, with the hit placement outlined in the viewport and revealed in the hierarchy;
- pre-window scene validation with file/line diagnostics, safe relative texture paths, and cross-reference checks;
- a `RuntimeScene` owner that constructs and synchronizes GroundMap, PhysicsWorld, World, and scene textures;
- player, primary prop, and enemy bodies authored in data rather than identified by the application loop;
- a permanent independent scene fixture proving the format is not tied to the packaged test area;
- an engine-owned integer-tick `AnimationPlayer` with loop, once, ping-pong, pause, reset, clip switching, and frame events;
- a narrow Aseprite JSON-array adapter that translates atlas paths, frame rectangles, millisecond timing, tag ranges, all four directions, and optional frame events into engine-owned clips;
- the original green patchwork character as the transparent player atlas, with eight-frame walk cycles in all eight directions and authored cardinal idle loops;
- the newer stitched character retained as a second full eight-way character and bound to the existing NPC/enemy slot rather than replacing the player;
- physics-derived player idle/eight-direction movement with tested 22.5-degree sector boundaries, retained facing on release or obstruction, and gait-phase-preserving direction changes;
- schema-9 automatic animation bindings with deterministic phase offsets, proven by two out-of-sync swaying tree instances;
- a reusable code-generated radial-alpha texture replacing the player's previous hard rectangular shadow;
- the enemy using the same authored locomotion interface and render-snapshot frame selection;
- animation kept presentation-only so it cannot modify physics or deterministic replay state;
- an identical schema-v3 gameplay digest after a 180-tick aim, held-fire, three-impact death, directional dodge, navigation-driven attacker pursuit, and two attacker requests at 30, 60, 120, monitor-synced, and uncapped presentation rates;
- camera transforms, conservative sprite culling, and submission/batch diagnostics;
- rolling 240-frame p50/p95/p99 frame-time telemetry plus estimated draw, batch, vertex, render-target, shader, and GPU-pass diagnostics in the overlay and editor;
- generational texture handles, path/generated-asset caching, reference counts, and a fallback texture;
- editor-only live bitmap replacement with stable handles, a two-poll write debounce, revision/status telemetry, and last-good-texture retention after a rejected reload;
- versioned runtime-project manifests with package-relative content paths;
- separate development and shipping executables, a development-tools-off shipping preset, and static MSVC runtime linkage;
- one-command minimal folder/ZIP packaging with a real GPU runtime smoke test;
- headless clock, frame telemetry, flow/layers, input, Combat, EnemyIntent, projectiles, Health, GameplayState, project, GroundMap, NavGrid, NavPathfinding, NavAgent, Physics2D, animation, Aseprite, scene, presentation, jobs, World, and Render2D tests through CTest;
- project warnings treated as errors.

## Build on Windows

Visual Studio with the Desktop development with C++ workload is required. The helper locates Visual Studio's bundled CMake and Ninja, prepares the MSVC environment, configures, builds, and optionally tests or launches.

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1 -Configuration Debug -RunTests -Launch
```

The first configure downloads pinned raylib, EnTT, Box2D, and nlohmann JSON sources into the ignored `build` directory. Development presets also download pinned Dear ImGui for the editor; the shipping preset does not.

During the current editor-first production phase, build only the editor target:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1 -Configuration Release -EditorOnly
```

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

Every smoke run is one entry in the registry in `game/src/smoke_scenarios.cpp`,
which also generates the `--help` listing. To see what is registered, without
opening a window:

```powershell
.\build\windows-debug\IC_2DE-Editor.exe --list-scenarios
```

Adding a run means adding a name, a description and the configuration it needs
to that one table. It previously meant adding a field to `ApplicationConfig`, a
branch of a two-hundred-line `if`/`else` chain in `main`, a line of `--help`
text, and a hand-written `add_test`, with nothing connecting the four.

The runs CTest registers name themselves through the `SCENARIO` keyword of
`ic2de_add_smoke_test`, and `ic2de.smoke_scenario_registry` checks those names
against the binary's own list. Renaming a scenario without updating its
registration would otherwise only surface on a machine that can run the GPU
tests; that check needs no GPU, because `--list-scenarios` returns before a
window is opened.

Rendering is uncapped by default. Temporary caps are available for frame-rate checks:

```powershell
.\build\windows-debug\ic2de_testbed.exe --fps=30
.\build\windows-debug\ic2de_testbed.exe --fps=60
.\build\windows-debug\ic2de_testbed.exe --fps=120
.\build\windows-debug\ic2de_testbed.exe --monitor-hz
```

Gameplay controls: W/A/S/D or the arrow keys move across the X/Z ground plane; left mouse fires; R reloads; Space dodges; E interacts; Q or the mouse wheel swaps weapon; and X chooses extraction. On an Xbox-compatible controller these map to left stick, right trigger, X, A, B, Y, and left bumper respectively; the right stick supplies continuous aim.

Development controls: P pauses, O advances one fixed tick while paused, F5 resets, F1 toggles debug visuals, F2 toggles the editor, F6 cycles render pacing, F7 toggles post-processing, G toggles the GPU backdrop/CPU fallback, and Escape quits. The reset moved from R to F5 so R can remain the conventional reload action. Development builds only: the shipping runtime compiles the editor, debug overlay, and live toggles out.

F1 is a master switch over independent debug channels - collision shapes, trigger volumes, the elevation map, the world grid, the navigation grid, the navigation path, and the compact HUD - so a development build can present exactly what ships. The dense grid and focused path are opt-in. Grid cells are teal/red; a path uses a teal start, gold intermediate cells, and magenta goal. The viewport no longer carries the former wall of statistics: FPS/pacing and two tool shortcuts remain compact, while detailed values live behind Overview, Navigation, Gameplay, and Scene tabs in the Statistics panel. The Debug channels panel groups related overlays and offers `Clean viewport` and `Hide all`; Hierarchy placements can be filtered by name. The `lights` channel is listed but unavailable until a lighting module exists. `Debug > Enemy stress test` rebuilds only the unsaved runtime copy with 10, 25, 50, 100, or 200 total Runners; `Restore authored scene` removes the runtime copies. These actors use the real gameplay and navigation systems, but stress mode suppresses their final player-damage hand-off so a performance run cannot kill the player. Attack requests remain active and measurable, and the Debug menu plus Gameplay Statistics state whether damage is disabled. Restoring the authored scene restores normal enemy damage. Overview supplies FPS and frame-time percentiles, while Navigation and Gameplay show aggregate crowd totals with optional collapsed per-actor detail. F2 opens the docked editor shell: hierarchy, inspector, command history, statistics, debug channels, and the game rendered into a viewport panel. Ctrl+left-click a sprite in the viewport to select it: the hierarchy scrolls to the placement, the inspector opens it, and the viewport outlines it. Ctrl+left-clicking empty ground clears the selection. Ordinary left-click remains gameplay fire. The character stays drivable while the shell is open; movement keys are withheld only while an editor text field owns keyboard input. Escape does not quit while the shell is visible, so cancelling a text edit cannot close the window - hide the editor with F2 first, or use the window close button. Every edit it makes goes through `SceneEditor`, so undo, validation, and atomic saving behave identically to any other caller, and "Apply to running scene" rebuilds the derived NavGrid and reference path before swapping in a validated runtime copy without touching the authored file.

The dock arrangement auto-saves while it changes and again when the editor closes. It is shared by source Debug/Release builds and the packaged editor through `%LOCALAPPDATA%\IC_2DE\Editor\layout-v1.ini`, so moving, resizing, floating, or re-docking panels survives a restart without writing into game content. Use `Workspace > Save layout now` for an immediate save or `Workspace > Reset to default layout` to rebuild the clean first-run arrangement. Automated probes use `--editor-layout=PATH`, keeping the personal workspace untouched. A file counts as a layout only when it holds a real dock tree, so a window-only ini yields the complete default workspace instead of a half-built one. Each launch logs `Built default editor layout: PATH` or `Restored editor layout: PATH` exactly once, which is the observable difference between the two paths.

Run the automated three-launch workspace probe with:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify-editor-layout.ps1 -BuildDirectory build/windows-release
```

It requires the first launch to build and save the default workspace, hand-widens one saved dock node, then requires the second launch to restore it and keep the arranged width. Checking only that an ini exists cannot tell restoration from a rebuild that recomputes the same arrangement; the arranged width can only survive a real restoration. A third launch replaces the file with a window-only ini and requires the default workspace back, which keeps the restore assertion honest.

The editor watches every loaded file-backed bitmap while its shell is visible. Replacing a PNG reloads it in the running viewport without changing its texture handle. A partially written or invalid replacement is rejected and the last valid GPU texture remains active. The Statistics tab reports watched, successful, and rejected reload counts. An editor launched from the repository root watches `game/assets/runtime`; a double-clicked build/package watches its adjacent `Content` copy.

Run the automated live GPU replacement probe with:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify-editor-hotswap.ps1 -BuildDirectory build/windows-release
```

```powershell
.\build\windows-debug\ic2de_testbed.exe --editor
.\build\windows-debug\ic2de_testbed.exe --no-debug-visuals
.\build\windows-debug\ic2de_testbed.exe --no-post-process
```

For a deterministic moving-camera and culling smoke run:

```powershell
.\build\windows-debug\ic2de_testbed.exe --monitor-hz --smoke-movement
```

To capture the authored west-facing walk through the real renderer:

```powershell
.\build\windows-debug\ic2de_testbed.exe --fps=60 --no-debug-visuals --smoke-left
```

To capture independent northward movement, eastward aim, and the crosshair inside the editor:

```powershell
.\build\windows-debug\IC_2DE-Editor.exe --fps=60 --no-debug-visuals --smoke-crosshair
.\build\windows-debug\IC_2DE-Editor.exe --fps=60 --no-debug-visuals --smoke-projectile
.\build\windows-debug\IC_2DE-Editor.exe --fps=60 --no-debug-visuals --smoke-held-fire
.\build\windows-debug\IC_2DE-Editor.exe --fps=60 --no-debug-visuals --smoke-run-and-gun
.\build\windows-debug\IC_2DE-Editor.exe --fps=60 --no-debug-visuals --smoke-projectile-impact
.\build\windows-debug\IC_2DE-Editor.exe --fps=60 --no-debug-visuals --smoke-target-death
.\build\windows-debug\IC_2DE-Editor.exe --fps=60 --smoke-dodge
.\build\windows-debug\IC_2DE-Editor.exe --fps=60 --smoke-gameplay-replay
.\build\windows-debug\IC_2DE-Editor.exe --fps=60 --smoke-moving-attacker
.\build\windows-debug\IC_2DE-Editor.exe --fps=60 --smoke-nav-grid
.\build\windows-debug\IC_2DE-Editor.exe --fps=60 --smoke-nav-path
.\build\windows-debug\IC_2DE-Editor.exe --fps=60 --smoke-runner-path
```

The dodge route starts one stationary retained-facing dodge, captures its active window, and fails unless the exact 78-unit open-ground displacement, invulnerability observation, duration expiry, collision-clear result, screenshot, and texture lifetime all pass.

The moving-attacker route keeps the player stationary and requires one acquisition, at least 100 world units of collision-resolved pursuit, one attack request, and 12 applied player damage. The navigation-grid route captures the opt-in derived-grid overlay after 15 fixed ticks; in the current test area it reports a 64 x 46 grid of 20-unit cells, 2,930 walkable cells, and 14 hard-blocked cells using 10 x 6 actor half-extents. The navigation-path route captures a seven-cell, 136.57-world-unit standalone A* route around the first authored solid after expanding eight cells. The Runner-path route requires at least three bounded searches and two waypoint advances from the real enemy navigation consumer, then captures the active route. The combined gameplay route also aims, holds fire, resolves three impacts and one death, performs one 78-unit dodge, and exercises navigation-driven attacker pursuit/damage before reporting the schema-v3 authoritative digest. It fails if any required gameplay transition or the final digest sample is missing.

Verify that the same Release build reaches one identical state at every presentation mode:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify-replay.ps1 -Configuration Release
```

This runs the combined gameplay route at 30, 60, 120, active-monitor, and uncapped presentation rates and requires all five schema versions and digest values to match.

Validate that authored player and tree frames stay inside their source rectangles and share stable ground baselines:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\validate-sprite-atlases.ps1
```

Packaging runs this asset gate automatically before either build starts.

Build the current editor-only test folder and ZIP without rebuilding or packaging the game runtimes:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\package-editor.ps1 -Configuration Release -RunHotSwapProbe -RunDodgeProbe -RunGameplayReplayProbe -RunMovingAttackerProbe -RunNavigationGridProbe -RunNavigationPathProbe -RunRunnerPathProbe -RunEnemyStressProbe -RunEditorLayoutProbe
```

Editor-only outputs are written to `dist/editor-windows-x64` and
`dist/IC_2DE-Editor-Windows-x64.zip`.

Run the focused 50-Runner GPU stress route directly with:

```powershell
.\build\windows-debug\IC_2DE-Editor.exe --fps=60 --smoke-enemy-stress
```

The route requires at least 50 registered navigation agents, 100 bounded searches, 50 waypoint advances, and exactly zero player damage before it can pass. This proves the integrated harmless crowd path is active; it is not an FPS target. Use F6 to select uncapped presentation and inspect Overview p50/p95/p99 while comparing the editor's 50, 100, and 200 presets on the target machine.

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

## Continuous integration

`.github/workflows/ci.yml` builds both configurations on every push and pull
request, and checks formatting as a separate job so a style failure is obvious
at a glance rather than buried in a build log.

Both the Debug and the shipping preset are built. The shipping preset compiles
with `IC2DE_ENABLE_DEVELOPMENT_TOOLS` off, which is a genuinely different
translation of `application.cpp`, and it had been broken without anyone noticing
precisely because nothing built it automatically.

The three tests that drive a real executable open a window and need a GPU, so
they carry the `gpu` CTest label. A headless machine runs everything else:

```powershell
powershell -ExecutionPolicy Bypass -File .	oolsuild.ps1 -Configuration Debug -RunTests -ExcludeGpuTests
```

Adding a smoke test through `ic2de_add_smoke_test` applies that label
automatically, so a new one cannot accidentally be handed to a machine that
cannot run it.

## Formatting and static analysis

`.clang-format` describes the house style. Its values were chosen by measuring
the existing sources rather than by preference: each candidate configuration was
run across `engine/`, `game/` and `tests/`, and the one that rewrote the fewest
lines was kept, so the file records the style already in use instead of imposing
a new one.

clang-format is not on PATH on a stock Windows machine, so the helper locates
the copy bundled with Visual Studio, the same way `tools/build.ps1` locates
CMake and Ninja:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\format.ps1
```

`-Check` reports unformatted files and exits non-zero without writing to them,
which is the mode continuous integration runs.

Vendored third-party sources are excluded; reformatting them would create a
permanent diff against upstream and make future updates harder to apply.

`.clang-tidy` holds a deliberately curated check list. Enabling every available
check on a codebase this size produces thousands of findings, and a diagnostic
set nobody can clear is one nobody reads, so the list is kept to checks that are
expected to hold at zero. The three suppressed checks are documented in the file
with the reasoning for each.

`git blame` skips the one-time reformatting commit through
`.git-blame-ignore-revs`. To have it applied automatically:

```powershell
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

## Documents

- `docs/ENGINE_CHARTER.md` - the current product and technical assumptions;
- `docs/game/LOOMHOLD_PRODUCTION_PLAN.md` - professional game plan for the action roguelite wave-defence extraction shooter direction;
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
- `docs/checkpoints/2026-08-31-application-loop-decomposition.md` - application loop phases, tested automated-run verdict, and refactor evidence;
- `docs/checkpoints/2026-08-31-character-ambient-animation.md` - new pixel-art character, phase-locked eight-way gait, automatic tree sway, and package evidence;
- `docs/checkpoints/2026-08-31-editor-texture-hot-swap.md` - failure-safe bitmap replacement, live GPU evidence, and the editor-only delivery path;
- `docs/checkpoints/2026-08-31-combat-input-actions.md` - logical combat actions, continuous aim sources, mappings, and editor telemetry;
- `docs/checkpoints/2026-09-01-combat-command-foundation.md` - fixed-tick Combat commands, viewport-aware world aim, event telemetry, tests, and editor package evidence;
- `docs/checkpoints/2026-09-01-crosshair-aim-facing-input-fix.md` - left-click movement regression, pixel crosshair, independent aim-facing, and packaged GPU evidence;
- `docs/checkpoints/2026-09-01-needle-pistol-state.md` - deterministic ammunition, fire cooldown, reload timing, projectile results, editor telemetry, and package evidence;
- `docs/checkpoints/2026-09-01-projectile-travel-lifetime.md` - deterministic projectile movement, exact expiry, visible presentation, editor telemetry, and packaged GPU evidence;
- `docs/checkpoints/2026-09-01-held-fire-projectile-impact.md` - continuous LMB fire repair, nearest segment collision, owner filtering, impact deduplication, and packaged runtime evidence;
- `docs/checkpoints/2026-09-01-run-and-gun-aiming.md` - mouse-release ordering, stable aim-facing, muzzle presentation, and movement-plus-fire evidence;
- `docs/checkpoints/2026-09-01-target-health-death.md` - deterministic target health, duplicate-hit protection, death retirement, editor feedback, and packaged GPU evidence;
- `docs/checkpoints/2026-09-01-fixed-tick-dodge-state.md` - authoritative dodge duration, invulnerability, cooldown, reset, and packaged countdown evidence;
- `docs/checkpoints/2026-09-01-directional-dodge-movement.md` - frozen direction, exact collision-resolved displacement, swept wall contact, and packaged GPU evidence;
- `docs/checkpoints/2026-09-01-deterministic-gameplay-state-digest.md` - canonical authoritative snapshots, schema-versioned hashing, five-presentation combat replay, editor telemetry, and packaged GPU evidence;
- `docs/checkpoints/2026-09-01-deterministic-moving-attacker.md` - narrow enemy intent, collision-resolved actor motion, player damage, schema-v2 replay, editor telemetry, and packaged GPU evidence;
- `docs/checkpoints/2026-09-01-deterministic-nav-grid-data.md` - immutable dense 2.5D navigation data, hard-block/no-corner rules, editor overlay, tests, and packaged GPU evidence;
- `docs/checkpoints/2026-09-01-deterministic-a-star-compact-hud.md` - deterministic copied A* results, focused path visualization, compact HUD, tests, and packaged GPU evidence;
- `docs/checkpoints/2026-09-01-persistent-editor-workspace.md` - cleaner dock defaults, per-user layout persistence, grouped tooling, restart proof, and package evidence;
- `docs/checkpoints/2026-09-01-deterministic-runner-path-consumption.md` - bounded A* replanning, cell-centre following, authoritative navigation snapshots, tests, and packaged GPU evidence;
- `docs/checkpoints/2026-09-01-editor-enemy-stress-testing.md` - deterministic runtime-only Runner cloning, reachable crowd placement, compact aggregate telemetry, and packaged GPU evidence;
- `docs/references/HAZEL_ADAPTATION.md` - explicit adopt, adapt, and defer decisions from the Hazel reference;
- `docs/references/GAME_ENGINE_SYSTEMS_REPORT_ADAPTATION.md` - adopt, adapt, and defer decisions from the full-stack engine report;
- `Referances/IC_2DE_EXECUTION_PLAN.md` - milestone roadmap and quality gates;
- `2d-cpp-raylib-engine-plan.md` - original research and technology survey.
