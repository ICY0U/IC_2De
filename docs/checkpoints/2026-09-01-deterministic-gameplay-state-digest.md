# Deterministic Gameplay-State Digest Checkpoint

## Outcome

IC_2DE now has one engine-owned, schema-versioned proof of authoritative gameplay state. `GameplayState` consumes copied public snapshots from World, Combat, ProjectileSimulation, and Health, validates that they represent a completed fixed-tick boundary, canonicalizes their collection order, and returns a stable 64-bit digest. Presentation-only names, sprites, and texture handles cannot change that value.

The editor exposes the latest completed-tick digest in the development HUD and Statistics panel. A combined automated route now exercises aim, held fire, projectile travel and impacts, one target death, and one directional collision-resolved dodge. The same Release executable reaches digest schema v1 value `11299741357852979897` at all five supported presentation modes.

## Working

- `GameplayStateSnapshot` composes existing public `WorldSnapshot`, `CombatSnapshot`, `ProjectileSimulationSnapshot`, and `HealthSnapshot` values; it does not expose EnTT, Box2D, raylib, or private module storage.
- `GameplayStateDigest` carries explicit schema version 1 and a stable 64-bit value so future authoritative schema changes cannot masquerade as comparable results.
- Canonical ordering removes source-container order as a variable for World entities, Combat actors, active projectiles, Health targets, and accepted-hit identities.
- The digest covers persistent World UUIDs and transforms; every Combat actor's retained aim, weapon, dodge, and held-fire state; Combat's future event/projectile identities; complete projectile state; and Health values, accepted-hit history, and future event identity.
- Signed zero is canonicalized. Non-finite floats, zero or duplicate identities, mismatched subsystem ticks, and pending Combat, projectile, or damage work are rejected rather than converted into misleading evidence.
- Presentation-only entity names, sprite bindings, and texture handles are deliberately excluded and have a regression test.
- Digest sampling occurs only after at least one completed fixed tick, when the editor is visible or an automated route requests the report. Shipping does not pay the snapshot/hash cost by default.
- `--smoke-gameplay-replay` runs 66 fixed ticks and requires three projectile spawns, three impacts, one exactly-once death/retirement, one dodge start, and 78 units of resolved dodge travel before reporting a digest.
- `tools/verify-replay.ps1` runs that route at 30, 60, 120, monitor-synced, and uncapped presentation rates and compares both schema and value.
- `tools/package-editor.ps1 -RunGameplayReplayProbe` runs the combined route from the staged relocatable editor and keeps its smoke artifacts out of the ZIP.

## TDD and measured verification

- The first red build failed because the public GameplayState seam did not exist; the minimal canonical-order behavior then passed.
- Subsequent red tests proved that non-latest Combat actor state, future Combat identities, Health accepted-hit history, future Health identities, and pending work were initially absent from or accepted by the digest. Each became green only after the production implementation covered or rejected it.
- The final presentation/authoritative separation regression passes: changing name, sprite, or texture does not change the digest, while World transform, projectile position, health, and frozen dodge direction changes do.
- Focused GameplayState, Combat, and Health executables pass.
- All 23 Debug CTest targets pass with project warnings treated as errors.
- All 23 Release CTest targets pass with project warnings treated as errors.
- The five Release presentation routes all report digest v1 `11299741357852979897`.
- The real Debug 60 Hz NVIDIA GeForce RTX 2080 Ti / OpenGL 3.3 route completed all 66 ticks with three spawns, three impacts, one death/retirement, one dodge, and `78.000061` units of resolved travel.
- The relocatable Release editor passed content and sprite validation, live bitmap hot swap, the dedicated dodge route, and the combined gameplay route on the same GPU. It reported the same final digest and clean texture, shader, framebuffer, and window shutdown.
- Visual inspection of `build/runtime-gameplay-digest-packaged-smoke.png` confirmed the editor viewport, active GameplayState v1 telemetry, completed 78-unit dodge telemetry, surviving player/tree presentation, and retired target at capture tick 51.
- `dist/IC_2DE-Editor-Windows-x64.zip` contains exactly 14 runtime entries and no staged `build` directory or smoke capture.
- Package size: 11,536,219 bytes. SHA256: `400B91AB4C584020774176018123BC121D922B4BA032B6B831872FD2027733A0`.

## Manual playtest still required

1. Launch `dist/editor-windows-x64/IC_2DE-Editor.exe` and open Statistics.
2. Pause with P and confirm the digest remains stable while no fixed tick completes.
3. Press O once and confirm the tick advances once and the digest updates once.
4. Resume, then fire, reload, and dodge; confirm the displayed digest changes only on completed simulation ticks while animation and editor-only interactions do not perturb authoritative play.
5. Reset with F5 and confirm tick-owned Combat, projectile, Health, and dodge state return to their clean starting sequence.
6. Complete the directional-dodge feel checks from the previous checkpoint: retained direction, stationary aim fallback, movement-over-aim priority, and wall slide/stop behavior.

## Deferred

- This digest is verification evidence, not a save-game format, network protocol, rollback buffer, or portable replay file.
- World coverage intentionally stops at current authoritative persistent identity and transform state. New gameplay components must be added when they become future-affecting.
- Manual input recording/playback, digest history timelines, divergence localization, and cross-machine replay fixtures remain future tools.
- Digest generation copies public snapshots in the visible development editor. It is not claimed to be free and should be profiled before enabling continuous telemetry in a production-scale scene.
- Roll art, impact animation, richer gameplay VFX, authored health definitions, and the first moving attacker remain incomplete.

## Learned

A replay hash is useful only when its input boundary is explicit. Hashing a convenient final position would have missed held-fire state, cooldowns, accepted-hit history, and future identity allocation; hashing private containers would have coupled tests to implementations. Composing copied public snapshots creates a deeper seam: each subsystem remains responsible for its state, while GameplayState owns validation, canonical ordering, schema identity, and comparison. Rejecting an incomplete fixed-tick boundary is stronger evidence than silently hashing queued work whose order has not yet resolved.

## Next

Add the first moving attacker through a narrow deterministic enemy-intent seam. Give one authored actor only the ability to acquire the player, produce copied movement/attack intent, advance on the fixed tick, and reuse existing GroundMap/Physics2D collision, Combat, Health, presentation, reset, digest, and replay paths. Preserve the stationary target dummy and do not add a behavior tree, general AI framework, navigation stack, or WaveDirector until this small attacker slice is independently testable and packaged.
