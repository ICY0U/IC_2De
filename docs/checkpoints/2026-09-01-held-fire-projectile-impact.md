# Held-fire and Projectile-impact Checkpoint

## Outcome

Holding left mouse now fires continuously at the needle pistol's authoritative fixed-tick cadence until release. The result no longer depends on fresh render-frame press edges, so low, capped, VSync, and unlocked presentation rates cannot create gaps in automatic fire.

Projectiles also collide with the nearest eligible solid body along each fixed-step movement segment. The query ignores the firing actor and sensors, returns only engine-owned copied values, and removes a projectile after one resolved impact so the same projectile cannot apply damage twice.

## Root cause and repair

The input tracker correctly reported `down`, `pressed`, and `released`, but the render-to-Combat adapter submitted Fire only when `pressed` was true. After that one render frame, Combat retained no knowledge that LMB remained held. Repeated behavior therefore depended on incidental new edges instead of fixed-tick cooldown readiness.

The adapter now submits a held-state update only when LMB changes. Combat latches that state per stable actor and evaluates ready weapons every fixed tick. The original press acknowledgement remains a copied intent event; repeated successful shots remain copied projectile results. A release update clears the latch before the next automatic shot.

## Working

- Held fire shoots immediately, then again on ticks 9, 17, and subsequent eight-tick cooldown boundaries.
- Releasing LMB stops later shots without requiring another render-frame edge.
- Held state survives frames with no fixed update and fixed-update batches with no new input sample.
- Per-actor held state and aim are retained independently; simultaneous automatic results iterate in stable actor-identity order.
- Reload continues to block firing, and a held trigger can resume after reload completion.
- `PhysicsWorld::cast_segment()` returns the nearest accepted hit with engine body ID, pixel-space point/normal, fraction, and tag.
- Segment queries support category/mask filtering, reject sensors by default, and ignore a supplied engine body without exposing Box2D identifiers.
- `RuntimeScene` translates stable owner UUIDs to their bound physics body and maps actor hits back to stable gameplay UUIDs when available.
- A valid impact removes the active projectile and emits one copied result containing owner, target, weapon, hit geometry, tag, and authored damage.
- Owner impacts, stale ticks, malformed geometry, missing projectile IDs, and repeated resolution are rejected without mutation.
- Statistics reports active, impacted, and expired counts plus the latest impact target/tag/damage.

## Measured

- The held-fire regression test was red with three failures: no first held shot, no cooldown-ready repeat, and an incorrect final count. It now proves two shots on ticks 1 and 9 followed by complete release suppression.
- The Physics2D segment test was red because the crossing query returned no hit. It now independently verifies the nearest box at pixel X=25 and fraction 0.25 rather than the farther candidate.
- A second Physics2D test verifies that owner and sensor bodies are skipped before the next solid target is selected.
- The impact test was red because a valid target left the projectile active and emitted no result. It now proves owner rejection, one valid impact, copied 18 damage, removal, and duplicate-ID rejection.
- All 21 Debug CTest targets pass.
- The real held-fire editor smoke produced exactly three projectiles while held through tick 18. It passed at 30 FPS, 60 FPS, and unlocked presentation.
- The real projectile-impact editor smoke aimed at the authored dynamic crate and required exactly one resolved impact.
- The packaged Release editor repeated the 30 FPS held-fire and 60 FPS impact routes using its adjacent `Content` directory.
- The Release editor package also passed content validation and the live NVIDIA/OpenGL texture hot-swap/resource-lifetime probe.
- `dist/IC_2DE-Editor-Windows-x64.zip` is 11,513,031 bytes with SHA-256 `08E34FAB3103880EF6982A6B9347111F15C20B0EFBEBF865491F1743BCAB5EDA`.

## Manual check

1. Launch `dist/editor-windows-x64/IC_2DE-Editor.exe` and keep the pointer inside the Viewport.
2. Hold LMB without repeatedly clicking. Shots should continue at a steady cadence until release.
3. Repeat while moving, at `--fps=30`, `--fps=60`, or `--uncapped`; cadence should follow fixed simulation rather than presentation FPS.
4. Aim through the player toward the dynamic crate, wall, or NPC. The projectile must not hit its owner and must disappear at the first eligible solid hit.
5. Open Statistics and inspect spawned, active, impacted, expired, and latest-impact rows.

The timing, nearest-hit, owner-filtering, deduplication, and packaged routes are automated. Physical mouse feel and final impact readability remain manual checks.

## Broken or deferred

- Impact events carry damage, but no health module consumes it yet; crates and the NPC do not lose health or die.
- A physics body without an animated gameplay entity may currently report its first bound presentation UUID, while authored actor bodies resolve to the animated actor UUID. The health slice will establish explicit damageable identity.
- There is no hit flash, particle, knockback, animation reaction, floating damage, or camera response yet.
- Collision is point-segment based; projectile radius/shape casts remain deferred until measured gameplay requires them.
- Collision on the terminal lifetime step should be revisited when collision and lifetime ordering receive their final combat-state tests.
- The full deterministic combat state hash is not implemented yet.

## Learned

Continuous actions belong as latched state at the fixed-tick seam, while discrete actions remain buffered edges. This preserves control feel without allowing render FPS to author gameplay cadence. Keeping Box2D filtering inside Physics2D and stable-identity mapping inside RuntimeScene also prevents projectile and Combat modules from learning backend or scene-storage details.

## Next small step

Write failing fixed-tick tests for a damageable target with explicit stable identity, health reduction, duplicate-impact rejection, and one copied death event. Then bind the existing NPC as the first target dummy and expose its health in the editor.
