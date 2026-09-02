# Hazel Fundamentals Adaptation

Reference inspected: TheCherno/Hazel commit `1feb70572fa87fa1c4ba784a2cfeada5b4a500db` from October 27, 2023, Apache-2.0.

Hazel is a broad learning engine built around GLFW/OpenGL, ImGui, EnTT, Box2D, project files, scenes, scripting, and separate client/editor targets. IC_2DE is deliberately narrower: raylib remains the single platform adapter and the engine is optimized for a sprite-led 2.5D game.

No Hazel implementation has been copied. The work below adapts architectural ideas using IC_2DE-owned interfaces and conventions. Audio is excluded by owner direction.

## Adopted now

| Hazel idea | IC_2DE adaptation | Reason |
|---|---|---|
| `ApplicationSpecification` | `ApplicationConfig` plus package manifest | Startup policy belongs in data, not hard-coded entry-point state. |
| Project-relative assets | Versioned `RuntimeProject` with safe relative paths | A shipped folder must remain movable and must not depend on the source checkout. |
| Separate client entry point | `ic2de_testbed` and `ic2de_shipping` executables | Development and player runtime requirements are different. |
| Editor/runtime separation | `IC2DE_ENABLE_DEVELOPMENT_TOOLS=OFF` shipping preset | Debug overlay, grid, and tuning controls do not belong in the shipped runtime. |
| Explicit lifecycle | Package smoke validates startup, fixed update, render, shutdown, and resource release | A successful compile is not runtime evidence. |
| Event buffering | `PhysicsStepResult` owns copied contact and trigger events | Gameplay consumes stable engine data after each fixed step rather than running inside Box2D callbacks. |
| Gameplay behavior module | `EnemyIntent` owns copied perception, deterministic acquire/pursue/attack state, cooldown, events, reset, and canonical snapshots | RuntimeScene keeps collision/presentation ownership and Health keeps damage ownership, so enemy logic stays testable without a GPU or Box2D handle. |
| UUID identity | `EntityUuid` is stable while `EntityId` is transient | Save/load, runtime copies, prefabs, and editor selection must not depend on EnTT handles. |
| Scene copy | Deterministic `WorldSnapshot`/`World::restore()` through the World interface | Copies retain UUID, name, transform, and sprite data while rebuilding transient handles. |
| Scene serialization | Strict scene schema 10 with validated UUIDs, optional sprite depth spans, cross-references, and an explicit kinematic `attacker` role | Invalid content fails before window/GPU startup. |
| Mutable scene document | `SceneDocument` edits by UUID, preserves untouched records, validates full candidates, and atomically replaces files | Editor-facing mutations need one safe seam without exposing parser or filesystem details. |
| Explicit migration and runtime copy | Deterministic schema 5 UUID migration plus version-only migration through schemas 6, 7, 8, and 9, with validated unsaved runtime materialization | Old data changes only by explicit request, migration never invents attackers, and play mode can consume edits without modifying source content. |
| Prefab assets and instances | Schema 7 `prefab`, `prefab_instance`, and `prefab_override` records expanded into ordinary entities at load | Reusable content needs one authored definition, per-placement identity, and no runtime concept the gameplay code must learn. |
| Editor command history | `SceneEditor` applies every mutation to a candidate document and owns bounded undo/redo | Panels must not mutate arbitrary state, and a rejected edit must leave the document and history untouched. |
| ImGui layer and Hazelnut dockspace | `EditorShell` owns a docked viewport, hierarchy, inspector, history, statistics, and debug-channel panels behind a pimpl, with an engine-owned Dear ImGui/raylib backend | The editor is a development adapter: ImGui types never reach the application loop, and the shipping preset compiles the whole module out. |
| Hazelnut theme and `UI::` widgets | `editor_ui` owns one graphite/amber palette, the style pass, the system-font atlas, and the shared widgets: property grid, axis control, badges, section headers, and search field | Panels inherit the look instead of each choosing colours and spacing, so a new panel cannot drift from the rest. |
| Hazelnut toolbar and status bar | A docked no-tab `Toolbar` strip carries save/apply on the left, centred transport, and document identity on the right; a viewport side bar carries the status line and live frame figures | Transport position must not move with the scene name, and the menu bar is not a place for a running status message. |
| Debug rendering | `DebugVisuals` master switch plus independent channels, toggled with F1 | A development build must be able to present exactly what ships, and each channel must be verifiable on its own. |
| Typed event routing | `RuntimeSceneTickResult` carries a batch of `EngineEvent` variants for contacts, triggers, and animation | Producers return copied engine data after a fixed tick; consumers never run inside Box2D or animation internals. |
| Owned layer stack | `LayerStack` owns regular layers and overlays, routes events top-down, and defers callback-requested structural changes | Layer lifetime and traversal safety belong in one module rather than in every application caller. |
| Framebuffer and post process | `FramePipeline2D` owns two raylib render targets, external shader loading, uniforms, texture orientation, fallback presentation, diagnostics, and release order | The application describes a frame; it no longer manages backend framebuffer state. |
| Renderer statistics | Rolling frame-time p50/p95/p99 plus estimated draws, batches, vertices, render-target switches, shader passes, and GPU passes are exposed as engine-owned values | Development needs actionable distributions and honest estimates before a lower-level profiler is justified. |
| Scene-owned ambient animation | Schema 8 binds a looping clip and deterministic phase offset to an entity without requiring locomotion physics | Foliage, water, and machinery reuse the animation player without gameplay-specific application-loop code. |
| UUID-addressed gameplay state | `Health` owns stable target/hit identities, fixed-tick reduction, duplicate rejection, snapshots, and copied damage/death events; `RuntimeScene` only translates death into actor retirement/reset | Gameplay state remains testable without EnTT, Box2D, renderer, editor, or application-loop ownership leaking into the module. |
| Scene-derived navigation data | `NavGrid` owns an immutable dense 2.5D topology snapshot and exposes copied cells/neighbors; the editor receives copied statistics and debug geometry | Navigation topology stays testable without EnTT, Box2D, renderer, or ImGui types, and validated scene apply can rebuild it as one candidate. |
| Navigation search | `find_nav_path`/`find_nav_path_world` return copied deterministic results over the NavGrid interface | Search remains headless and testable; EnemyIntent, Physics2D, rendering, and editor types remain outside the module. |
| Navigation-agent path consumption | `NavAgentSystem` accepts copied actor/target facts and returns copied normalized motion plus canonical snapshots | EnemyIntent retains decision ownership, NavAgent owns bounded route lifetime, and RuntimeScene retains collision and presentation; no subsystem receives another subsystem's private handles. |
| Editor-only stress instantiation | `EditorActions` requests a total actor count while one initialization-only `RuntimeScene` interface copies the authored Runner graph and the application registers copied module state; stress mode suppresses only the final player-damage hand-off | The ImGui adapter does not construct gameplay internals, stress content never enters the authored scene, and a harmless but otherwise integrated crowd can be measured from a relocatable editor. |

## Adapt differently

| Hazel approach | IC_2DE decision |
|---|---|
| Global `Application::Get()` and active `Project` singleton | Pass configuration into `run_application()` and return immutable project data from `RuntimeProject::load()`. |
| Raw-pointer `LayerStack` | Own layers with `std::unique_ptr`, detach top-down, and identify removals with engine-owned `LayerId` values. |
| Immediate polymorphic events with macros | Route a closed typed variant batch after simulation; events propagate from overlays downward and stop only when handled. |
| Public-facing EnTT entity convenience | Keep EnTT private behind the deeper `World` interface. |
| Renderer backend abstraction | Keep one raylib adapter until a second backend is real. |
| Random UUID generation inside the UUID type | Let World allocate non-zero IDs when needed and require explicit stable IDs in authored scenes. Deterministic snapshots never depend on random-device state. |
| Mono/C# scripting internals | Later expose an IC_2DE gameplay interface to a chosen scripting adapter; scripts never receive raw engine-library objects. |
| OpenGL vertex arrays, uniform buffers, and graphics context | Adapt the capabilities as renderer resources/statistics through raylib/rlgl instead of recreating a parallel graphics backend. |

## Complete non-audio system map

| Hazel area | IC_2DE status or adaptation target |
|---|---|
| Core application, window, timestep, input, logging | Present through `ApplicationConfig`, fixed-step clock, raylib adapters, actions, and engine logging. |
| Buffers and filesystem helpers | Atomic sibling-temp save and replacement are private to `SceneDocument`; add broader helpers only when another real consumer appears. |
| UUID, entities, components, scene copy | UUID, World snapshot/restore, and UUID-addressed mutable scene documents are present; component expansion follows real editor needs. |
| Layer stack and application events | Typed copied simulation events and an owned regular/overlay stack with deferred structural changes are present; window/input remain action-oriented until they have a second consumer. |
| Project files and serializers | Runtime project schema plus atomic scene writing and explicit migration exist; development project settings remain. |
| Renderer2D, camera, textures, shaders, framebuffer, fonts | Sprite submission, camera, texture handles, engine-owned ping-pong framebuffers, a packaged external post shader, post-processing, frame distributions, and richer submission/pass statistics exist; add a general shader registry only when a second material path needs it, then lighting, text, and fonts. The debug `lights` channel stays unavailable until a lighting module exists. |
| Physics2D | Present behind `PhysicsWorld`; continue with scene reconstruction, query/cast interfaces, joints only when gameplay requires them. |
| Scene cameras and editor camera | Runtime 2.5D camera exists; add an editor camera independent of the playable camera. |
| Scene serialization and prefabs | Mutable schema 10 documents, atomic save, migration, runtime copy, prefab identity/overrides, an authored attacker role, and round-trip fixtures exist; save-game snapshots remain. |
| Scripting | Planned after the scene/editor model stabilizes, behind an engine-owned interface rather than Hazel's Mono-specific surface. |
| ImGui layer and Hazelnut editor | Dockspace, docked toolbar, status bar, engine-owned theme and widget vocabulary, docked viewport with click-to-select, hierarchy, inspector, command history, statistics, debug channels, runtime-only enemy stress presets, and authored-scene restore exist; content browser, console log, gizmos, and isolated play mode remain. |
| Instrumentation | Rolling p50/p95/p99 frame metrics, renderer/pass counters, and aggregate crowd navigation/gameplay totals exist. Add true GPU timestamp queries, scoped CPU markers, capture controls, and optional Tracy integration when profiling needs exceed the live panel. |
| UI utilities and platform dialogs | Add native development file dialogs and a separate lightweight shipped-game UI module. |
| Particles and renderer examples | Add pooled 2.5D particles driven by gameplay/animation events. |
| Gameplay health and actor lifecycle | Deterministic Health and reset-safe target retirement are present; authored health components, reactions, loot, and AI remain game-led additions rather than general Hazel cloning. |
| Navigation data and diagnostics | Dense GroundMap-derived walkability, footprint clearance, height/max-step edges, hard blocking, no diagonal corner cutting, deterministic neighbors, copied A* results, bounded Runner path consumption, canonical agent telemetry, separate opt-in grid/path overlays, and deterministic 10/25/50/100/200-Runner stress presets are present. A dedicated wall-separated runtime fixture and 50/100/200 frame-time comparison are next. |
| Audio | Explicitly skipped for now. |

## Adaptation order

1. Persistent identity and deterministic World copy - complete.
2. Mutable scene document, round-trip serialization, schema migration, and runtime-copy lifecycle - complete.
3. Prefab definitions/instances and command-based scene edits - complete.
4. Typed event routing plus owned layer stack - complete.
5. Renderer resources, post-processing, lighting, text, statistics, and editor camera - framebuffer/post-process resources, external shader, and baseline statistics complete.
6. Development editor with hierarchy, inspector, content browsing, and isolated play mode - dockspace, viewport, hierarchy, inspector, history, statistics, and debug channels complete.
7. Project/content tools, file watching, hot reload, saves, profiling, particles, and game UI.
8. Scripting interface and adapter after the data model is stable.

Each stage ends in a runnable packaged checkpoint. Hazel is a design reference, not a requirement to recreate incompatible OpenGL, global singleton, or Mono internals inside the raylib-based engine.
