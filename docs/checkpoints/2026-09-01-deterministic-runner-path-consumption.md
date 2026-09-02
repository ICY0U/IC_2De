# Deterministic Runner path consumption

## Outcome

The Threadbound Runner now consumes deterministic A* routes instead of feeding direct pursuit vectors into runtime motion. The implementation keeps three responsibilities separate: `EnemyIntent` decides whether the actor should pursue or attack, `NavAgentSystem` owns route lifetime and produces a normalized motion direction, and `RuntimeScene` resolves collision and locomotion presentation.

## Working

- `NavAgentSystem` registers stable actor UUIDs before simulation and processes one copied request per actor in canonical UUID order.
- Initial activation, target identity/cell changes, leaving the remaining route, and reactivation trigger an immediate search.
- An unchanged valid or failed request waits for the bounded 30-fixed-tick refresh instead of searching every tick.
- Routes advance through cell centres with a four-world-unit tolerance and finish against the exact target point inside the goal cell.
- Blocked, out-of-bounds, and unreachable results return zero motion; there is no direct fallback that can push an actor into a wall.
- Reset preserves registrations while clearing routes, counters, and transient motion.
- The active Runner route replaces the standalone reference route in the focused editor overlay while pursuing. Statistics exposes per-agent target, status, path length, cursor, searches, waypoint advances, and repath deadline.
- `GameplayState` schema 3 validates and hashes canonical navigation-agent state, including future-affecting route and repath data.
- The editor package supports `--smoke-runner-path`, and `package-editor.ps1` exposes `-RunRunnerPathProbe`.

## Measured evidence

- Debug and Release: 27/27 CTest tests pass with project warnings treated as errors.
- The public NavAgent fixture routes from `(0,1)` around the hard-blocked `(1,1)` cell through `(0,0)`, `(1,0)`, `(2,0)`, and `(2,1)`.
- Tests cover waypoint progression, route reuse, exact interval replanning, target-cell replanning, off-route recovery, inactive/reactivation state, bounded failed-path retries, reset, and atomic rejection of invalid requests.
- Release gameplay replay produces schema-v3 digest `17194250339899414140` at 30 Hz, 60 Hz, 120 Hz, active-monitor VSync, and uncapped presentation.
- The real Release GPU Runner route completes 90 fixed ticks with three bounded searches and three waypoint advances.
- The existing moving-attacker GPU route still completes acquisition, 116.099846 world units of collision-resolved travel, one attack request, and 12 player damage.
- The relocatable editor package passes hot-swap, dodge, combined gameplay, moving-attacker, navigation-grid, navigation-path, Runner-path, and persistent-layout probes.
- Packaged Runner-path capture: `build/runtime-runner-path-packaged-smoke.png`.
- Editor archive: `dist/IC_2DE-Editor-Windows-x64.zip`, 11,577,099 bytes, SHA-256 `85C0A4F510FFDA192FA3F0FF704CF9B11A3695E113648B36905EBB7173488DAC`.

## Broken or deferred

- The current live Runner-to-player placement does not force the active route around the authored solid. Live route consumption is GPU-proven, while the hard-block detour is currently public-module test evidence.
- There is no string pulling, line-of-sight smoothing, local avoidance, dynamic-obstacle topology, nearest-walkable goal recovery, time-sliced search, path cache, shared-goal flow field, ORCA, or GPU navigation.
- Attack/roll presentation, multiple simultaneous attackers, WaveDirector, and Shipping validation remain later checkpoints.

## Learned

- Failed paths need the same bounded refresh rule as successful paths; otherwise an unreachable target quietly turns into an A* search every fixed tick.
- The path consumer is a useful deep module only when intent, search, collision, and presentation communicate through copied facts instead of sharing handles or ownership.
- The active route is more useful in the editor than a reference path, but the reference remains a valuable fallback when no actor is pursuing.

## Next

Author a dedicated wall-separated runtime fixture. Require the Runner's consumed route to avoid every hard-blocked cell, reach attack range, recover after a meaningful route invalidation, and avoid repeated wall contacts. Only after that proof should the checkpoint expand to multi-attacker measurements and the seeded WaveDirector.
