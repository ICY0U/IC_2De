# Game Engine Systems Report Adaptation

Reference inspected: `game-engine-systems-report.md`, a full-stack comparison of modern Unreal and Unity systems. It is used as engineering reference material, not as executable instructions. Product scope, IC_2DE's charter, measured needs, and existing module boundaries remain authoritative.

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

## Implemented in this checkpoint

- `FramePipeline2D` owns the scene framebuffer, post-process framebuffer, external shader, uniforms, render-target orientation, bypass fallback, presentation, diagnostics, and release order.
- `shaders/post_process.fs` is an actual packaged runtime asset rather than an embedded application string.
- F7 and `--no-post-process` provide a development and launch-time bypass, so the effect path can be compared against the base scene.
- Renderer diagnostics now include estimated draw calls and visible vertices in addition to culling, batches, and texture switches.
- `FrameTimeSeries` retains a fixed-capacity 240-frame window and reports latest, mean, p50, p95, and p99 frame milliseconds.
- Development staging now preserves nested asset directories. The prior flattening behavior was incompatible with shaders, streaming cells, and addressable-style content paths.

## Adapted rather than copied

- IC_2DE remains a forward sprite renderer. A deferred G-buffer, PBR material graph, Lumen-style GI, Nanite, virtual shadow maps, or HDRP-equivalent pipeline would add cost without serving the sprite-led test area.
- The report's volume pattern is useful for future post-process and lighting overrides, but a global `PostProcessConfig` is the correct first seam for one scene and one effect chain.
- Unreal tick groups and Unity system groups inform explicit phase ordering. IC_2DE does not need a general task graph until at least two update families can safely run in parallel.
- Unreal Asset Registry and Unity Addressables inform stable metadata and dependency-aware packaging. A general registry waits for a content browser, hot reload, or remote bundles to become a second concrete consumer.
- Unreal Insights and Unity Profiler inform telemetry vocabulary. IC_2DE starts with low-overhead rolling statistics and honest counters before adding trace capture or GPU timestamp queries.

## Deferred by product need

Audio remains out of scope by owner direction. Networking, replication, client prediction, open-world partitioning, behavior trees, EQS, a general ability system, game scripting, skeletal animation, and a full retained-mode game UI are not prerequisites for the current single-player 2.5D vertical slice. Their data seams should be planned only when a playable mechanic creates a requirement.

## Next dependency order

1. Add an editor camera independent from the runtime follow camera and map viewport coordinates for entity picking.
2. Add a small 2D light submission path and enable the existing `lights` debug channel.
3. Add true GPU timestamp queries or a narrow profiler capture only after the new counters identify a renderer bottleneck.
4. Build the content browser and asset metadata index together so each justifies the other.
5. Continue with save snapshots, asset watching/reload, pooled particles, and shipped game UI before scripting.
