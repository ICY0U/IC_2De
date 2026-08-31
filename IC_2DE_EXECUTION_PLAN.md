# IC_2DE - Practical Development Plan

## 1. Objective

Build a production-quality, Windows-first 2D-first 2.5D game engine in C++20, using raylib as the platform and rendering foundation. The World uses X/Y/Z space, but the art and gameplay presentation remain sprite-led through an orthographic oblique camera. The engine must prove itself by shipping one polished vertical slice; it is not considered successful merely because it can render sprites or expose many systems.

"AAA-level" means professional production discipline and presentation quality within a realistic 2D scope:

- stable frame pacing and explicit performance budgets;
- fast content iteration, hot-reload, and usable tools;
- strong animation, audio, lighting, particles, camera, and input feel;
- repeatable builds, tests, profiling, crash diagnostics, and versioned data;
- a polished game slice that another person can run and understand.

It does not initially mean a general-purpose competitor to Unreal or Unity. A mature general-purpose engine is a multi-year team project. The first target is a focused engine capable of producing one excellent 2D game.

## 2. Product strategy: build the engine through a game

The engine needs a real customer from the first month: a small reference game called the **Engine Testbed**.

The initial testbed is one compact top-down adventure area containing:

- a controllable character with keyboard and gamepad input;
- solid X/Z world collision, authored elevation, and one dynamic physics object;
- idle, eight-direction move, interact, attack, hit, and death animation states;
- one enemy with a simple state machine;
- one light, one post-processing effect, particles, screen shake, and hit-stop;
- spatial sound effects, music, and master/music/SFX controls;
- a scene transition, save/load, and a pause/settings screen;
- editable spawn points and entity properties;
- a packaged Release build that starts without developer tools installed.

Every engine module must enable a visible testbed improvement. Avoid building systems that have no immediate game use.

## 3. Scope

### Version 0.1 must include

- Windows build and packaging, with Linux kept compiling in CI when practical;
- fixed-step simulation and interpolated rendering;
- scene/world lifecycle and an EnTT-backed entity model;
- sprites, atlases, cameras, layers, render targets, shaders, and debug drawing;
- input actions, rebinding-ready bindings, gamepad support, and action buffering;
- Box2D integration through engine-owned types and events;
- Aseprite animation clips, state transitions, and frame events;
- cached asset loading, placeholders, development hot-reload, and lifetime tracking;
- audio buses and pooled overlapping sound effects;
- particles, shake, hit-stop, time scaling, and basic 2D lighting;
- versioned scenes, prefabs, settings, and save data;
- an ImGui development shell with hierarchy, inspector, console, profiler, and asset browser;
- structured logging, assertions, automated tests, Tracy markers, and a benchmark scene.

### Explicitly deferred

- networking;
- a visual scripting system;
- a full standalone editor application;
- plugin marketplace or public SDK stability;
- arbitrary user-authored engine extensions;
- advanced skeletal animation unless the reference game proves it is required;
- game-code DLL hot-reload before scene/data/asset hot-reload is reliable;
- Web and console ports;
- multiple rendering backends.

## 4. Architecture contracts

The engine should own the application architecture even though it uses proven libraries internally.

1. **Library types stay behind seams.** Gameplay code must not depend on `Texture2D`, `b2BodyId`, or Aseprite loader types. Raylib, Box2D, and content loaders are adapters inside engine modules.
2. **The world is deeper than a public registry.** EnTT may implement entity storage, but passing `entt::registry&` everywhere would make the entire program depend on its implementation. Expose focused world operations and queries needed by gameplay.
3. **Commands enter; results and events leave.** Physics receives body commands and emits engine-owned collision events. Audio receives sound events. Rendering receives a frame description. This keeps tests independent of the window and GPU where possible.
4. **The fixed tick is authoritative.** Gameplay and physics run at 60 Hz. Rendering interpolates state and may run at any display rate. Cap accumulated time to avoid a spiral of death after a breakpoint or stall.
5. **Assets use stable IDs and handles.** Paths belong in manifests and tools, not throughout gameplay code. Missing assets resolve to visible or audible fallbacks and produce actionable diagnostics.
6. **Authored data is versioned.** Scenes, prefabs, input maps, settings, and saves contain schema versions and migration tests before real content accumulates.
7. **Editor actions use commands.** Property edits, creation, deletion, and reparenting go through undoable commands instead of mutating arbitrary state from panels.
8. **Development tools are removable.** Editor and profiling code are compiled behind development options and excluded from the shipping runtime where appropriate.

### Primary module interfaces

| Module | Small interface offered to callers | Complexity hidden inside |
|---|---|---|
| Application | run, request quit, change scene | window loop, timestep, lifecycle, error handling |
| World | spawn, destroy, query, enqueue command | EnTT storage, deferred mutation, entity validity |
| Render2D | begin frame, submit, present | sorting, batching, render targets, shaders, debug draw |
| Assets | request, resolve, reload | cache, file watching, fallbacks, GPU/audio lifetime |
| Physics2D | create/destroy, apply command, step, read events | Box2D IDs, unit conversion, contacts, interpolation state |
| Input | action state and device state | bindings, dead zones, buffering, device switching |
| Animation | play/state/event access | clip timing, transitions, Aseprite import, frame events |
| Audio | post event, set bus | pooling, priorities, streaming, volume routing |
| Persistence | load/save versioned document | JSON format, migrations, validation, atomic writes |
| Tools | execute/undo command, draw panels | selection, inspection, history, console, editor state |

Do not invent an interface merely because a library might be replaced one day. Add a seam when there are two real adapters or when isolating an external type materially improves testing and locality.

## 5. Repository shape

```text
IC_2De/
|-- CMakeLists.txt
|-- CMakePresets.json
|-- cmake/
|-- engine/
|   |-- include/ic2d/       # Stable engine interfaces
|   `-- src/                # Implementations and library adapters
|-- game/
|   |-- src/                # Testbed gameplay
|   `-- assets/             # Source assets and manifests
|-- editor/                 # Development-only panels and commands
|-- tools/                  # Asset validation and packaging tools
|-- tests/
|   |-- unit/
|   |-- integration/
|   `-- fixtures/
|-- benchmarks/
|-- third_party/
|-- docs/
|   |-- ENGINE_CHARTER.md
|   |-- ARCHITECTURE.md
|   |-- DATA_FORMATS.md
|   `-- adr/
`-- dist/                   # Ignored packaged output
```

Prefer explicit source lists in CMake once the project is established. Pin dependency versions; do not track `master` branches.

## 6. Milestone roadmap

Estimates assume one developer working part-time. Treat gates as more important than dates.

### Milestone 0 - Engine charter and target budget (2-3 days)

Deliver:

- choose the reference game's genre, camera style, art resolution, and target display resolutions;
- choose the minimum target PC separately from the development PC;
- write the first performance, memory, startup, and content-iteration budgets;
- record non-goals and the rule that new engine systems require a testbed use case.

Gate: `ENGINE_CHARTER.md` answers what game is being built, for whom, on what hardware, and what "good" is measurable as.

### Milestone 1 - Reproducible boot (Week 1)

Deliver:

- Git repository, CMake presets, pinned raylib, test framework, and formatting/static-analysis configuration;
- Debug and Release builds from a clean checkout;
- application lifecycle, structured log, assertions, and a colored window;
- one automated smoke test that initializes core modules without opening a game window.

Gate: one documented command configures, builds, tests, and launches the project; the build has zero project warnings.

### Milestone 2 - First playable loop (Weeks 2-3)

Deliver:

- fixed 60 Hz update, render interpolation, pause, single-step, and accumulator clamping;
- input actions for move, interact, attack, pause, and debug overlay;
- an oblique orthographic camera, placeholder billboard character, walkable ground, and a reset action;
- live frame-time and simulation-tick display.

Gate: the character is controllable for ten minutes without frame-rate-dependent movement, runaway catch-up, or invalid state.

### Milestone 3 - World, rendering, and assets (Weeks 4-6)

Deliver:

- EnTT-backed World module and deferred entity commands;
- sprite render submission, stable layer ordering, camera transforms, atlases, and debug primitives;
- texture/font asset handles, cache, fallback assets, leak counters, and development reload;
- a benchmark scene and a RenderDoc capture documenting actual draw-call behavior.

Gate: reload a texture while the testbed runs; destroy/recreate the scene without leaking resources; meet the provisional sprite benchmark.

### Milestone 4 - Physics as an engine module (Weeks 7-9)

Deliver:

- Box2D adapter mapping the engine's X/Z ground plane to physics XY, with an explicit units policy, body/shape creation, and safe destruction;
- engine-owned elevation bands/rules so Box2D implementation coordinates never leak into gameplay;
- fixed-tick transform synchronization with render interpolation;
- engine-owned contact, sensor, and trigger events;
- collision layers/masks, debug drawing, and focused physics tests.

Gate: player, enemy, ground, trigger, and dynamic object interact correctly at 30, 60, 120, and uncapped render rates while simulation results remain stable on the same build/platform.

### Milestone 5 - Animation, audio, and game feel (Weeks 10-12)

Deliver:

- Aseprite importer feeding engine-owned clips;
- animation player, state transitions, playback rules, and frame events;
- audio cache, buses, music streaming, and pooled SFX;
- particles, camera shake, hit-stop, dodge/interaction buffering, and controller rumble where supported.

Gate: one combat interaction drives pose, hit reaction, particles, camera, audio, and rumble from the same gameplay event without those presentation modules modifying simulation state.

### Milestone 6 - Scenes, prefabs, and saves (Weeks 13-15)

Deliver:

- versioned scene and prefab documents;
- stable entity references, validation, migrations, and useful error messages;
- settings and save-game slots with atomic replace-on-success writes;
- golden-file round-trip and migration tests.

Gate: save, close, reopen, and reproduce the expected testbed state; old fixture data migrates successfully; malformed data fails safely.

### Milestone 7 - Editor MVP (Weeks 16-21)

Deliver:

- ImGui dockspace, hierarchy, inspector, viewport, console, asset browser, and profiler panels;
- selection/picking, property editing, entity creation/deletion, and transform tools;
- command history with undo/redo and dirty-scene detection;
- play-in-editor using a disposable copy or reloadable snapshot of authored state.

Gate: build the complete test room, save it, enter play mode, stop, undo an edit, and reload without editing JSON by hand or recompiling C++.

### Milestone 8 - Scripting decision gate (Weeks 22-24, optional)

First measure whether data-driven C++ plus editor tooling is slowing iteration. Add Lua only if at least two real gameplay behaviours need runtime authoring.

If accepted, deliver a narrow script interface for entity commands, actions, animation, audio, timers, and events. Scripts never receive raw EnTT, Box2D, or raylib objects.

Gate: reload one enemy behaviour without restarting; report script errors with file, line, entity, and stack information; retain C++ ownership of engine lifetime and physics.

### Milestone 9 - Production pipeline (Weeks 25-30)

Deliver:

- asset manifest, validation, dependency tracking, and packaging;
- shader compilation/error fallbacks and material definitions;
- localization-ready text IDs, input/settings persistence, and accessibility hooks;
- automated Release packaging and clean-machine launch test.

Gate: a packaged build contains only runtime dependencies and declared assets, launches on a clean Windows environment, and produces a diagnostic log on failure.

### Milestone 10 - Vertical slice (Weeks 31-40)

Deliver the complete reference room with final-quality representative art, one enemy, one encounter, menus, saving, lighting, VFX, layered sound, and a beginning/end state.

Gate: five external playtesters can launch and complete it. Critical defects are zero, controls are understood without developer guidance, and profiler captures remain inside budget.

### Milestone 11 - Hardening and v0.1 release (Weeks 41-46)

Deliver:

- soak tests, scene-transition loops, corrupted-data tests, and controller disconnect/reconnect tests;
- performance capture on minimum hardware, loading analysis, and memory-leak pass;
- license notices, dependency inventory, documentation, samples, and tagged binaries;
- prioritized findings from playtests, with crash and save-loss issues resolved.

Gate: the same tagged commit builds in CI, passes automated tests, produces a package, and completes a 60-minute automated or human soak without crash, resource growth, or save corruption.

## 7. Provisional quality budgets

These are starting targets. Milestone 0 must revise them against the actual game and minimum PC.

| Area | Initial gate |
|---|---|
| Frame pacing | 60 FPS minimum; 16.67 ms frame budget; inspect p95 and p99, not only averages |
| Simulation | fixed 60 Hz; no dropped ticks during normal play; bounded catch-up after stalls |
| CPU/GPU split | aim for no more than 8 ms CPU and 8 ms GPU in the representative stress scene |
| Rendering | provisional 10,000 visible sprites at 60 FPS in a synthetic atlas-friendly scene |
| Physics | provisional 500 active dynamic bodies at 60 ticks/s in a representative collision scene |
| Memory | stable after 100 scene reloads; zero known engine-owned leaks at shutdown |
| Startup | packaged testbed reaches its first interactive screen in under 3 seconds on the minimum PC |
| Save safety | interrupted or failed saves leave the previous valid save intact |
| Determinism | identical replay hash on repeated runs of the same build/platform; cross-platform determinism is not promised |
| Build health | Debug and Release pass with zero project warnings; automated tests run from CTest |
| Iteration | ordinary texture, shader, scene, and script changes appear without a full engine rebuild |

The RTX 2080 Ti development machine is not the minimum target. Choose and test a lower hardware baseline so inefficient code is visible before content production.

## 8. Verification strategy

Use four layers of evidence:

1. **Unit tests:** math, handles, state machines, animation timing, input buffering, version migrations, and command history.
2. **Headless integration tests:** world lifecycle, scene round-trip, event delivery, deterministic same-build replay, and asset manifest validation.
3. **Runtime smoke scenes:** render ordering, asset reload, physics contacts, animation/audio events, editor play/stop, and packaging.
4. **Human playtests:** feel, clarity, controller behaviour, audio balance, pacing, and accessibility. Automated checks cannot validate game feel.

Every milestone ends with a runnable build and a short evidence note containing commands, results, profiler captures where relevant, and remaining manual checks.

## 9. First 30 days

### Week 1 - Foundation

- Write `ENGINE_CHARTER.md` using the Milestone 0 questions.
- Initialize Git and add CMake presets for MSVC Debug and Release.
- Add pinned raylib and a test framework.
- Create the Application and Log modules.
- Launch a window, close cleanly, and run one headless test.

Checkpoint: clean checkout to running window with one documented command.

### Week 2 - Time and input

- Add the fixed-step clock and interpolated render loop.
- Add action-mapped keyboard and gamepad input.
- Draw a placeholder player and camera.
- Add pause, single-step, reset, and a frame-time overlay.

Checkpoint: stable movement at multiple render caps.

### Week 3 - World and assets

- Add the World module with entity lifetime tests.
- Add transform and sprite data.
- Add texture handles, cache, fallback texture, and leak counters.
- Add sorted sprite submission and a basic benchmark scene.

Checkpoint: 100 scene rebuilds without stale handles or resource growth.

### Week 4 - First game mechanic

- Add the Box2D adapter with ground, player, trigger, and one dynamic crate.
- Translate contacts to engine collision events.
- Add solid movement, trigger interaction, and the first authored elevation rule.
- Record the first automated same-build input replay and state hash.

Checkpoint: a small playable room that proves input, world, rendering, assets, and physics work together.

## 10. Weekly working rhythm

- Start each week with one player-visible outcome and its measurable gate.
- Keep changes small enough that the testbed runs every day.
- Profile before and after performance work; do not optimize based on intuition alone.
- Add a regression test when fixing a reproducible engine bug.
- Keep a short decision record for lasting choices such as units, coordinates, data ownership, and save compatibility.
- End each week with a packaged or runnable checkpoint and a five-line status note: working, measured, broken, learned, next.

## 11. Main risks and controls

| Risk | Control |
|---|---|
| Building an engine without a game | Every module must unlock a testbed requirement |
| Exposing third-party libraries everywhere | Keep external types inside module implementations and adapters |
| Editor consumes the whole project | Begin with Tiled/Aseprite plus a narrow in-engine inspector |
| Premature scripting or DLL reload | Require measured iteration pain and two concrete use cases first |
| Performance judged by average FPS | Track frame-time distributions and representative stress scenes |
| Save formats become impossible to change | Version data immediately and keep migration fixtures |
| Visual polish postponed indefinitely | Introduce feedback systems in Milestone 5 and polish each mechanic as it lands |
| Scope expands to multiplayer/platforms | Keep deferred systems out until the vertical slice passes its release gate |
| Development hardware hides problems | Establish a lower minimum-PC baseline in Milestone 0 |

## 12. Definition of v0.1 success

IC_2DE v0.1 is complete when:

- the vertical slice is visibly polished and playable from start to finish;
- its room and entity properties can be authored through tools rather than source edits;
- scenes, settings, and saves are versioned and safely recoverable;
- Debug and Release builds are repeatable, tested, profiled, and packageable;
- the minimum target machine stays inside the documented frame budget;
- external playtesters can run it without the developer present;
- the engine's public interfaces do not expose raylib, Box2D, Aseprite-loader, or editor implementation details to ordinary gameplay code;
- remaining limitations and deferred features are documented honestly.

## 13. Immediate next step

The first four expanded Hazel adaptation checkpoints are complete. Schema 7 gives every authored entity a persistent UUID, adds reusable prefab definitions with per-instance identity and field overrides, and expands instances into ordinary entities in authored order. RuntimeScene passes identity into the EnTT-backed World, and deterministic World snapshot/restore preserves UUID, name, transform, and sprite state while rebuilding transient handles. `SceneDocument` supplies UUID-addressed edits, prefab instantiation and removal, validated unsaved runtime copies, explicit schema 5-to-6 and 6-to-7 migration, and validation-before-replace atomic saving. `SceneEditor` puts every mutation behind bounded undo/redo with candidate-copy atomicity. `RuntimeSceneTickResult` now returns copied typed contact, trigger, and stable-identity animation events, while an owned `LayerStack` routes them with safe deferred structural changes. Duplicate or zero UUIDs, orphaned overrides, malformed save candidates, and invalid layer transitions fail safely.

Development builds now carry an F1 debug-visual master switch over independent collision, trigger, elevation-map, world-grid, and statistics channels, plus an F2 docked editor shell with a viewport, hierarchy, inspector, command history, statistics, and channel panels. Dear ImGui is pinned and development-only behind an engine-owned raylib backend; the shipping runtime compiles the whole surface out.

The application now renders through an engine-owned two-target frame pipeline. A packaged external fragment shader provides the first explicit post-process pass and can fall back to the scene target without exposing raylib resources. Development telemetry reports a 240-frame p50/p95/p99 distribution and honest estimated draw, batch, vertex, target-switch, shader-pass, and GPU-pass counts. Nested content directories are preserved during development staging and validated through the relocatable runtime manifest.

Audio is explicitly deferred by owner direction. Continue the non-audio Hazel adaptation in dependency order:

1. Add an editor camera with viewport picking, then 2D lighting and its debug channel. Framebuffer/post-process resources, the first external shader asset, and baseline renderer statistics are complete.
2. Close the remaining development-editor gaps: content browser, console, profiler, live-World inspection, and isolated play/stop mode.
3. Add asset watching/reload, project settings, save-game snapshots, profiling instrumentation, particles, and native game UI.
4. Adapt Hazel scripting behind an engine-owned gameplay interface after the scene/editor data model is stable; do not expose EnTT, Box2D, raylib, or a scripting runtime directly.
5. Re-run Debug/Release, five-mode replay, resource-lifetime checks, editor/runtime-copy tests, event/layer tests, and packaged GPU smoke at every checkpoint.

Continuous ramps and audio remain deferred until a real test-area requirement or owner direction brings them back into scope.
