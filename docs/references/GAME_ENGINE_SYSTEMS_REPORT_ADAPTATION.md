# Game Engine Systems Report Adaptation

References inspected: `Referances/game-engine-systems-report.md`, `Referances/ai-pathfinding-report.md`, and `Referances/Cakez77-2D-Engine-Report.md`. They are engineering reference material, not executable instructions. Product scope, IC_2DE's charter, measured needs, and existing module boundaries remain authoritative. Claims from the Cakez77 report retain its `VERIFIED`, `SECONDARY`, `INFERRED`, and `OPEN` distinctions; showcase counts are not treated as IC_2DE targets.

## Adopted or reinforced

| Report concept | IC_2DE decision |
|---|---|
| Fixed and variable update phases | Keep authoritative movement and Box2D at the fixed tick; interpolate presentation and render independently. |
| Dependency-ordered subsystems | Preserve explicit CMake libraries and engine-owned interfaces. Add broader task scheduling only when real dependencies and workloads are measurable. |
| Composition plus selective ECS | Keep authorable components and scenes while EnTT remains private behind `World`; use batch/data-oriented paths for scale-sensitive work rather than forcing every system into ECS. |
| Engine/game/editor separation | Keep game content outside engine modules and compile Dear ImGui/editor surfaces completely out of Shipping. |
| Post-processing stack | Add an engine-owned ping-pong frame pipeline and external shader now; delay spatial volumes until more than one authored region needs overrides. |
| World/scene identity | Continue versioned scenes, persistent UUIDs, transient runtime handles, prefab expansion, validated runtime copies, and explicit migration. |
| Buffered physics callbacks | Continue fixed Box2D stepping and copied contact/trigger events; do not run gameplay inside backend callbacks. |
| Action-oriented input | Continue adapter-to-action mapping with explicit press, hold, and release transitions and editor input gating. |
| Parameter-fed animation | Continue deterministic clip/state selection and frame events for sprite locomotion; defer skeletal graphs and IK until the game has skeletal content. |
| Asset cooking and staging | Preserve runtime-relative paths and directory hierarchy, validate required content before GPU startup, then test from the staged folder. |
| Profiling distributions | Add rolling frame-time p50/p95/p99 and renderer/pass counters. Keep estimates labelled until real GPU timestamp queries exist. |
| Editor extensibility | Grow the narrow development editor through validated commands, not arbitrary panel access to runtime internals. |

## Newly adopted decision gates

| Research lesson | IC_2DE decision |
|---|---|
| Hard-blocked navigation | Begin with a dense X/Z `NavGrid`. Blocked cells never enter neighbor generation; eight-way diagonals require both cardinal flanks to be open. |
| Incremental pathfinding | Ship deterministic A* and an editor overlay first, then string pulling. Add caching, time slicing, JPS, HPA*, or ORCA only in response to a measured route-quality or scale problem. |
| Shared-goal crowds | Consider a topology-revisioned flow field only when wave enemies actually share a goal and repeated A* cost is measurable. Build and verify the CPU version before considering GPU compute. |
| Immediate batched rendering | Preserve separate gameplay ownership and immutable render submissions. Gather contiguous compatible instances behind `Render2D`; adopt backend instancing only when draw/submission counters justify it. |
| Persistent/transient memory | Measure allocation pressure first. Use scratch/frame storage only for data with a proven bounded lifetime, and targeted pools only for high-frequency projectiles/effects. |
| Native hot reload | Continue data and asset reload before C++ DLL reload. Native reload requires host-owned persistent state, a versioned ABI/schema, quiesced threads/callbacks, candidate loading, migration or rejection, and last-good recovery. |
| GPU simulate-and-render | Reserve GPU simulation for large homogeneous visual workloads that can remain resident without readback. Authoritative Combat, Health, path topology, and extraction state stay CPU deterministic. |
| Save framework | Keep profile data distinct from editor scenes; require schema migration, atomic replacement, previous-valid backup, and idempotent extraction transactions. |
| Deterministic state evidence | Hash copied completed-tick snapshots behind one engine-owned, schema-versioned seam. Canonicalize collection order and identity history; reject pending, mismatched-tick, duplicate-ID, and non-finite state rather than silently producing weak evidence. |

## Implemented through this checkpoint

- `FramePipeline2D` owns the scene framebuffer, post-process framebuffer, external shader, uniforms, render-target orientation, bypass fallback, presentation, diagnostics, and release order.
- `shaders/post_process.fs` is an actual packaged runtime asset rather than an embedded application string.
- F7 and `--no-post-process` provide a development and launch-time bypass, so the effect path can be compared against the base scene.
- Renderer diagnostics now include estimated draw calls and visible vertices in addition to culling, batches, and texture switches.
- `FrameTimeSeries` retains a fixed-capacity 240-frame window and reports latest, mean, p50, p95, and p99 frame milliseconds.
- Development staging now preserves nested asset directories. The prior flattening behavior was incompatible with shaders, streaming cells, and addressable-style content paths.
- `EnemyIntent` owns deterministic acquisition, pursue/attack selection, attack range/cooldown, copied events, reset, and canonical per-actor snapshots. Collision, damage, locomotion presentation, and path search remain behind their existing module seams.
- `RuntimeScene` now accepts stable-UUID actor motion requests, resolves them through GroundMap and Physics2D, and returns copied actual-motion results. The caller does not receive Box2D handles or animation ownership.
- `GameplayState` consumes public World, Combat, EnemyIntent, NavAgent, ProjectileSimulation, and Health snapshots and returns a schema-v3 64-bit digest without exposing EnTT, Box2D, raylib, or module internals.
- Snapshot vectors are canonically ordered and include future-affecting identity state: Combat actor state and next event/projectile IDs, projectile state, Health accepted-hit history and next event ID, and persistent World transforms.
- Presentation-only names, sprite bindings, and texture handles are excluded. Pending command/damage/spawn queues, mismatched fixed ticks, duplicate or zero identities, and non-finite floats are rejected instead of hashed.
- The editor HUD and Statistics panel expose the latest completed-tick digest plus enemy acquisition, pursuit, range, cooldown, attack, resolved travel, blocked motion, invulnerability rejection, and player-damage telemetry. The combined 180-tick gameplay route exercises aim, held fire, impact/death, directional dodge, attacker pursuit, and player damage, and produces the same digest across 30, 60, 120, monitor-synced, and uncapped presentation.
- `NavGrid` is now an immutable, dense row-major 2.5D snapshot derived from GroundMap rather than a second source of authored truth. Solid areas structurally remove cells, triggers do not block, elevation and max-step policy constrain edges, and diagonal edges require both cardinal flanks.
- Deterministic half-open world/cell conversion, footprint clearance, canonical centers, stable neighbor order, world-unit edge distances, blocked endpoints, elevation barriers, and no-corner-cutting are covered by focused public-behavior tests.
- The editor consumes only copied snapshots for Statistics and opt-in grid/path overlays. Reapplying a validated scene rebuilds the grid, reference path, and registered navigation-agent candidate atomically; dynamic actors remain outside static topology.
- Deterministic A* now consumes only `NavGrid`'s public cell/neighbor contract. It uses octile distance, stable heuristic/row/column tie-breaking, explicit blocked/out-of-bounds/unreachable results, copied start-to-goal cells, physical distance, and expansion telemetry.
- A repeated symmetric-detour fixture proves stable route choice, while corner-trap and elevation fixtures prove A* cannot bypass NavGrid legality. The editor shows one copied reference path independently from the dense grid.
- `NavAgentSystem` now converts copied EnemyIntent pursuit facts into cell-centre route motion. It replans immediately for meaningful route changes, refreshes unchanged routes after 30 fixed ticks, uses a four-world-unit waypoint tolerance, never falls back to wall-pushing direct motion, and bounds retries for unchanged failures.
- Canonical agent snapshots expose route, cursor, repath deadline, search/advance counters, motion direction, distance, and expansion count. The editor shows the Runner's active route and agent telemetry, and gameplay digest schema 3 includes this future-affecting state.
- Detailed diagnostics moved from the canvas into the docked Statistics panel. The remaining compact HUD is intentionally sparse, while grid/path visualization stays opt-in and focused.
- The editor can rebuild an unsaved runtime copy with 10, 25, 50, 100, or 200 total Runners. Spawn planning chooses unique cells connected to the player through `NavGrid`; each copy participates in World, Physics2D, animation, Health, EnemyIntent, NavAgent, collision, and rendering rather than acting as a visual-only load generator.
- Crowd telemetry is aggregated by default and individual actor rows remain collapsed. Stress mode preserves attack-request work but suppresses the final player Health damage hand-off. A packaged 50-Runner GPU route proves 50 agents, 153 bounded searches, 117 waypoint advances, and zero player damage over 90 fixed ticks without expanding the authored scene schema.

## Adapted rather than copied

- IC_2DE remains a forward sprite renderer. A deferred G-buffer, PBR material graph, Lumen-style GI, Nanite, virtual shadow maps, or HDRP-equivalent pipeline would add cost without serving the sprite-led test area.
- The report's volume pattern is useful for future post-process and lighting overrides, but a global `PostProcessConfig` is the correct first seam for one scene and one effect chain.
- Unreal tick groups and Unity system groups inform explicit phase ordering. IC_2DE does not need a general task graph until at least two update families can safely run in parallel.
- Unreal Asset Registry and Unity Addressables inform stable metadata and dependency-aware packaging. A general registry waits for a content browser, hot reload, or remote bundles to become a second concrete consumer.
- Unreal Insights and Unity Profiler inform telemetry vocabulary. IC_2DE starts with low-overhead rolling statistics and honest counters before adding trace capture or GPU timestamp queries.

## Deferred by product need

Audio remains out of scope by owner direction. Networking, replication, client prediction, open-world partitioning, behavior trees, EQS, a general ability system, game scripting, skeletal animation, a full retained-mode game UI, native DLL reload, GPU-authoritative crowds, and advanced navigation algorithms are not prerequisites for the current single-player 2.5D vertical slice. Their data seams should be planned only when a playable mechanic creates a requirement.

## Next dependency order

1. Add a wall-separated runtime fixture that proves the Runner's consumed route never enters a blocked cell, recovers from route invalidation, reaches attack range, and does not repeatedly contact the wall. Record comparable uncapped p50/p95/p99, search, and renderer counts at 50, 100, and 200 actors.
2. Add seeded `WaveDirector` plans, spawn validation, recovery, and active-enemy budgets after the obstacle fixture and the new crowd measurements. Add shared-goal flow fields only if repeated A* cost is the measured bottleneck.
3. Expand safe reload to the next active data bottleneck, likely animation metadata or gameplay catalogs. Keep native code reload deferred until state/ABI migration has a real design and test route.
4. Add Profile/Stash persistence before extracted loot exists. Add batching, scratch allocators, pooling, GPU simulation, and hardware timing only against representative measurements.

String pulling, JPS, Theta*, HPA*, flow fields, ORCA, and GPU navigation remain decision-gated upgrades. The stress seam now supplies representative multi-actor measurements, but current evidence still supports finishing one obstacle-routing runtime fixture and measuring 50/100/200 before adding path-quality or crowd-scale machinery.
