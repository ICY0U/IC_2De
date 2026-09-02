# Editor enemy-stress testing

## Outcome

The editor can now create a representative on-screen enemy crowd without adding test clutter to the authored scene. `Debug > Enemy stress test` offers 10, 25, 50, 100, and 200 total-Runner presets plus `Restore authored scene`. A preset rebuilds an unsaved runtime candidate before simulation begins, so every generated Runner uses the real World, physics, sprite, shadow, animation, health, intent, navigation, collision, and render paths.

## Working

- `RuntimeScene::spawn_actor_copies()` is an initialization-only interface. It copies the first authored non-player actor graph for a requested physics role and rejects calls after the first fixed tick.
- Each Runner copy receives deterministic UUIDs above the authored/prefab-expanded identity range, an independent kinematic body, sprite and shadow bindings, a locomotion animation player, and runtime lookup bindings.
- The editor registers every copy with `Health`, `EnemyIntent`, and `NavAgentSystem`; the ImGui layer only emits a requested total count and never constructs gameplay-module state.
- Spawn planning performs a deterministic breadth-first traversal from the player's cell over `NavGrid::neighbors()`, then selects unique connected cells around repeatable angular/radius targets. Player, target, and existing attacker cells are excluded.
- Stress presets use a 600-world-unit editor-only acquisition range so the crowd exercises intent and pathfinding across the visible test area. The normal authored scene keeps its existing 180-world-unit range.
- Stress enemies still acquire, pursue, enter attack state, and emit cooldown-authoritative attack requests, but their final damage hand-off to player Health is suppressed. `Restore authored scene` restores normal enemy damage.
- The Debug menu and Gameplay Statistics explicitly report whether enemy attack damage is normal or disabled.
- Selecting `Restore authored scene`, applying edited content, or choosing a new preset constructs the complete runtime/grid/health/intent/navigation candidate before swapping it into the application.
- Statistics presents aggregate Health, EnemyIntent, and Navigation counts first. Per-actor rows are collapsed until requested, preserving the clean viewport and readable editor layout.
- `--smoke-enemy-stress` starts 50 total Runners, enables the path overlay, and requires at least 50 agents, 100 searches, 50 waypoint advances, and exactly zero player damage over 90 fixed ticks.
- `package-editor.ps1 -RunEnemyStressProbe` runs the same route from the relocatable editor and preserves a GPU capture.

## Measured evidence

- Debug and Release pass 27/27 CTest tests with project warnings treated as errors.
- Debug and Release real-GPU stress runs each create 49 copies around the one authored Runner: 50 navigation agents, 114 World entities, 60 physics bodies, 54 animated entities, and 114 stable UUIDs.
- Both 50-Runner routes complete 90 fixed ticks with 153 bounded A* searches, 117 waypoint advances, and zero player damage, then shut down cleanly.
- The existing Release five-mode replay remains deterministic at schema 3 digest `17194250339899414140` for 30, 60, 120, active-monitor, and uncapped presentation.
- The relocatable Release editor passes hot-swap, dodge, combined gameplay, moving-attacker, navigation-grid, navigation-path, Runner-path, enemy-stress, and persistent-layout GPU probes.
- Packaged capture: `build/runtime-enemy-stress-packaged-smoke.png`.
- Editor archive: `dist/IC_2DE-Editor-Windows-x64.zip`, 11,593,478 bytes, SHA-256 `1EDC67BCCB59850C7759F3389BE1BF7AF5183A4E9979B64B38369548750C7147`.

## Broken or deferred

- The automated stress route validates integrated actor count and activity, not a performance target. The 60 FPS capture is deliberately capped and does not measure maximum headroom.
- Enemy-to-enemy collision and local avoidance are not enabled, so crowded Runners may overlap. This is honest baseline behavior rather than an unmeasured ORCA or separation implementation.
- The current stress layout does not prove every actor must route around the authored wall. The public hard-block fixture remains the obstacle-legality proof; a dedicated wall-separated runtime fixture is still required.
- Search time slicing, shared-goal flow fields, path caching, pooling, render instancing, GPU compute, and GPU timestamp queries remain measurement-gated.
- Stress actors are ephemeral editor data. They are not saved as placements, wave definitions, or gameplay content.

## Learned

- A useful crowd test must exercise full simulation ownership; drawing duplicate sprites would hide physics, intent, navigation, animation, and lifetime costs.
- Runtime copies need one narrow initialization seam so the editor does not become a second scene loader or reach into private module handles.
- Aggregate telemetry scales much better than opening one text block per enemy, while expandable detail still supports diagnosis.
- Connected-cell placement prevents an enemy-count test from being dominated by invalid or permanently unreachable starts.

## Next

Run uncapped editor comparisons at 50, 100, and 200 Runners and record p50/p95/p99 frame time, visible/culled sprites, estimated draw calls/batches, search counts, and waypoint advances. Then add a wall-separated runtime fixture with assertions for blocked-cell avoidance, attack-range arrival, route-invalidation recovery, and bounded wall contacts. Use those two measurements to decide whether the next implementation is WaveDirector spawning, path scheduling/caching, a shared-goal flow field, rendering work, or local avoidance.
