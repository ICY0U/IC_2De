# Directional Collision-Safe Dodge Movement Checkpoint

## Outcome

Space now produces real fixed-tick displacement rather than state-only telemetry. `Combat` normalizes and freezes one world X/Z direction on the accepted start tick. `RuntimeScene` applies it for the existing 12 active ticks at 3x the scene's authored player speed, while all movement continues through `GroundMap` and the Physics2D kinematic target.

The direction policy is movement first, stationary aim second, retained direction third, and the authored initial facing as the deterministic fallback. An active dodge cannot be bent by later input, and a request rejected by cooldown cannot redirect it.

## Working

- `DodgeDefinition` owns the 3x speed multiplier alongside the established duration, invulnerability, and cooldown values.
- `CombatCommand::dodge_direction`, `DodgeStartedEvent::direction`, and `DodgeSnapshot::direction` use copied engine math types only.
- Combat rejects unusable explicit directions, normalizes accepted values, and mutates direction only when a dodge starts successfully.
- Ordinary locomotion is overridden for exactly 12 fixed ticks; presentation facing remains independent so mouse aim does not steer the movement vector.
- `RuntimeScenePlayerMotion` keeps scene movement free of Combat knowledge: it receives direction, presentation direction, and a speed multiplier.
- `GroundMap` now sweeps each movement axis to the nearest solid face. Fast axial steps cannot tunnel across a thin solid and the unblocked axis still slides.
- Runtime and Statistics telemetry report frozen direction, actual collision-resolved distance, and whether the dodge encountered a blocker.
- `--smoke-dodge` now requires the exact open-ground displacement as well as start, invulnerability, expiry, capture, and clean texture lifetime.

## Measured verification

- The TDD red build failed on the intentionally absent direction fields before implementation.
- Focused Combat and GroundMap tests pass, including normalization, cooldown redirection rejection, nearest-face contact from both directions, and axis sliding.
- All 22 Debug CTest targets pass with warnings treated as errors.
- All 22 Release CTest targets pass with warnings treated as errors.
- The real Debug NVIDIA GeForce RTX 2080 Ti / OpenGL 3.3 route completed 18 ticks and measured `78.000008` world units against an expected 78.0.
- Visual inspection of the tick-8 Debug capture showed an active dodge with direction `X -0.31 / Z 0.95` and 52.0 units travelled.
- The relocatable Release editor passed content validation, sprite-atlas validation, live texture hot swap, and the same 78-unit dodge route.
- The packaged capture was visually inspected and showed the same direction/travel telemetry during the active window.
- `dist/IC_2DE-Editor-Windows-x64.zip` contains 14 runtime entries, one editor executable, and no staged `build` or smoke artifact.
- Package size: 11,523,715 bytes. SHA256: `2C8FDA9F7EA8039ED4D0DCC8409BD7C6BB114A47E745CDA43E0E08EB8BEBB392`.

## Manual playtest still required

1. Launch `dist/editor-windows-x64/IC_2DE-Editor.exe` and open Statistics.
2. Move in each cardinal and diagonal direction, release the keys, then press Space; confirm the last movement direction is retained.
3. While stationary, aim elsewhere and press Space; confirm aim supplies the dodge direction.
4. Hold movement while aiming elsewhere and press Space; confirm movement direction wins while aim-facing remains independent.
5. Dodge directly into the solid wall and along its edge; confirm no tunnelling, no jitter, and a clean slide/stop.

## Deferred

- There is no dedicated roll clip, squash, afterimage, trail, camera kick, or dodge VFX.
- Swept collision currently preserves the existing deterministic X-then-Z axis order; arbitrary rotated collision geometry is not part of GroundMap.
- Dodge tuning remains compiled gameplay data until a second authored dodge definition creates a real catalog consumer.
- The complete Combat/Projectile/Health replay digest is not yet implemented.

## Learned

Keeping timing and direction capture inside `Combat` prevents render-frame input from bending an authoritative action. Passing only a generic motion command into `RuntimeScene` also avoids coupling scene/physics code to combat policy. The endpoint-only GroundMap check was safe at walking speed but not at burst speed; the dodge slice exposed the need for nearest-face swept contact before any faster actors or enemies are introduced.

## Next

Add an order-stable deterministic gameplay digest for Combat, ProjectileSimulation, Health, and relevant player state, including dodge direction and remaining timers. Extend the existing five-presentation replay route to exercise aim, held fire, impact/death, and dodge movement before adding roll presentation or VFX.
