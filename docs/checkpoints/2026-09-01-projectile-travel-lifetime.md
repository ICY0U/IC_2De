# Projectile Travel and Lifetime Checkpoint

## Outcome

Successful needle-pistol shots now become visible projectiles that travel deterministically through the true X/Z world. Combat remains responsible for weapon rules and emits a copied spawn result; the new projectile module owns buffering, movement, stable active state, exact lifetime expiry, and copied endpoint events.

The module's interface remains independent of raylib and Box2D. Render2D consumes copied projectile snapshots and draws an interpolated cyan pixel bolt, while the editor Statistics panel reports active and expired counts plus the latest expiry identity.

## Working

- A Combat spawn is buffered without changing authoritative projectile position during a render frame.
- The event is activated only by its matching sequential fixed tick.
- Each step integrates normalized direction multiplied by speed and fixed-step duration in world X/Z; Y remains at the resolved weapon height.
- Previous and current positions are retained for interpolation and the next segment-collision slice.
- Lifetime decreases by exactly one per simulated step.
- A projectile is removed after its exact authored number of steps and emits one copied expiry event containing tick, projectile identity, owner identity, and endpoint.
- Duplicate, malformed, stale-tick, non-finite, zero-speed, and zero-lifetime spawns are rejected at the module interface.
- Reset clears pending, active, expired, and tick state.
- The editor viewport renders active projectiles through the existing immutable Render2D frame description.

## Measured

- The first movement test was red with three failures: the spawn was rejected, the buffer stayed empty, and the matching tick activated nothing. It now proves a 120-unit/second shot moves from `(10, 3, -4)` to `(11.2, 3, -2.4)` in one 60 Hz step.
- The lifetime test was red because the projectile remained active at zero life and emitted no event. It now proves a three-tick projectile remains active through tick two, expires on tick three, and reports the independently calculated endpoint `(13, 2, 6)`.
- All 21 Debug CTest targets pass, including the new `ic2de.projectiles` target.
- The Debug real-window projectile smoke completed 24 fixed ticks and wrote `build/runtime-projectile-smoke.png`.
- The packaged Release editor loaded its adjacent runtime content, visibly rendered the projectile, captured `build/editor-hotswap-probe/build/runtime-projectile-smoke.png`, and shut down cleanly.
- The Release editor package also passed adjacent-content validation and the live NVIDIA/OpenGL texture hot-swap/resource-lifetime probe.
- `dist/IC_2DE-Editor-Windows-x64.zip` is 11,503,713 bytes with SHA-256 `939E8CB13218F20A20DF231FB87CBF11F23C2A185D6B7758F8E1B332C06CBAEE`.

## Manual check

1. Launch `dist/editor-windows-x64/IC_2DE-Editor.exe`.
2. Aim inside the Viewport and left-click. A small cyan bolt should leave the character at the crosshair angle without interrupting movement.
3. Open Statistics and repeat. Successful clicks should increase the spawned/observed count and briefly increase `Projectile simulation` active state.
4. Wait roughly 1.2 seconds. The shot should disappear at its 72-tick lifetime and the expired count should increase once.
5. Fire in several directions and while moving; presentation should remain interpolated while the authoritative path stays fixed-tick.

Travel/lifetime rules and the packaged GPU route are automated. Mouse feel, bolt size, colour, and combat readability remain manual tuning checks.

## Broken or deferred

- Projectiles do not collide with scenery or actors yet and therefore pass through the tree, walls, crates, and NPC.
- There is no hit event, ownership filtering, damage deduplication, health, hit reaction, or target death.
- Runtime origin resolution currently accepts the player actor used by this slice. Enemy-fired projectiles will require a generic stable-UUID position lookup when that use case arrives.
- The cyan rectangle is temporary production feedback, not final weapon VFX.
- The automated combat state hash is still deferred to the complete target-damage loop.

## Learned

Keeping movement behind a dedicated projectile module preserves locality: Combat does not learn about positions or collision, and rendering does not advance gameplay. Retaining each fixed step's previous/current segment supports smooth uncapped presentation now and the next collision query without changing the interface again.

## Next small step

Write failing tests for a projectile segment hitting the nearest eligible Physics2D body while ignoring its owner. Then add the narrowest engine-owned segment query needed to make that behavior pass without exposing Box2D types.
