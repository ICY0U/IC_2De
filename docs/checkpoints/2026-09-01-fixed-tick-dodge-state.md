# Fixed-Tick Dodge State Checkpoint

## Outcome

Space now reaches an authoritative per-actor dodge state inside `Combat`. A successful request starts 12 active ticks, 9 invulnerable ticks, and a 36-tick cooldown at the fixed 60 Hz simulation rate. Repeated input cannot restart the timers, the exact cooldown-ready tick accepts a new dodge, and reset clears every timer and counter.

This checkpoint deliberately proves timing and invulnerability before movement impulse, roll animation, or VFX. Runtime and editor code consume copied state and a copied `DodgeStartedEvent`; they do not own dodge policy.

## Working

- `DodgeDefinition` records active, invulnerable, and cooldown durations in integer fixed ticks.
- A render-frame dodge edge remains buffered and cannot make the actor invulnerable before `Combat::fixed_update`.
- A ready request emits one `DodgeStartedEvent` with stable tick, sequence, actor identity, and copied timing values.
- `Combat::invulnerable(EntityUuid)` provides a narrow per-actor query without exposing internal storage.
- `DodgeSnapshot` exposes active, invulnerable, remaining timers, and successful-start count for read-only consumers.
- Requests during activity or cooldown still produce their ordinary intent acknowledgement but cannot restart timers or increment the successful-start count.
- A second dodge becomes eligible exactly 36 ticks after the first start.
- `Combat::reset` clears activity, invulnerability, cooldown, counters, pending commands, and events.
- Statistics reports dodge activity, invulnerability, cooldown, starts, and the latest start event.
- The development HUD exposes the same copied countdown so automated captures visibly show the window.
- `--smoke-dodge` synthesizes one Space press, captures during the active window, runs past duration expiry, and fails unless start, invulnerability, expiry, capture, and texture lifetime all pass.
- `tools/package-editor.ps1 -RunDodgeProbe` runs that route from the relocatable editor folder and retains a verification capture outside the package.

## Measured

- Before implementation, the new public test failed to compile because `DodgeStartedEvent`, `DodgeSnapshot`, `player_dodge`, `CombatSnapshot::dodge`, and `Combat::invulnerable` did not exist.
- Public Combat tests prove an active window on ticks 1 through 12, invulnerability on ticks 1 through 9, rejection at several points inside cooldown, readiness on tick 37, and clean reset.
- All 22 Debug and all 22 Release CTest targets pass.
- The real Debug NVIDIA RTX 2080 Ti/OpenGL 3.3 route completed 18 fixed ticks, observed one dodge start and invulnerability, verified duration expiry, captured a frame, and released all tracked textures.
- Visual inspection of the Debug capture at simulation tick 8 showed `Dodge ACTIVE | active 5 | invulnerable 2 | cooldown 29`.
- The packaged Release editor repeated the route from `dist/editor-windows-x64` with the same tick-8 countdown, exit code zero, and clean resource release.
- Packaged content validation, sprite-atlas validation, and the existing live texture hot-swap probe pass.
- `dist/IC_2DE-Editor-Windows-x64.zip` is 11,522,390 bytes with SHA-256 `4AE94873C49DBE586FFE89D9B1B867452013F8EC1C8784393B11342113968189`.

## Manual check

1. Launch `dist/editor-windows-x64/IC_2DE-Editor.exe`.
2. Open Statistics and press Space once while the viewport has gameplay focus.
3. Confirm Dodge becomes active, invulnerability counts down before activity ends, and cooldown continues afterward.
4. Press Space repeatedly during the countdown and confirm the remaining timers do not restart.
5. After cooldown reaches zero, press Space and confirm the successful-start count increments once.

## Broken or deferred

- Dodge currently changes authoritative state but does not move the character. Direction capture, movement impulse, collision response, and camera feel are the next slice.
- There is no roll animation, sprite squash/afterimage, trail, flash, or other dodge VFX.
- The player is not yet a registered Health target and no attacker submits damage, so the invulnerability query is proven but has no incoming player-damage consumer.
- Dodge timings are temporary compiled definitions; they move into validated gameplay data when the catalog has a second definition to serve.
- Dodge state is not yet included in the complete combat replay hash.
- Subjective responsiveness still needs the manual check above.

## Learned

Keeping dodge timers behind the existing Combat seam made exact window and cooldown behavior cheap to prove without a window. A separate per-actor invulnerability query gives future damage handling one stable fact to consume without coupling Health to Combat storage. The first GPU capture also showed that editor-tab state is not reliable visual evidence, so the route now places the same immutable snapshot in the existing development HUD.

## Next small step

Add a direction-carrying dodge activation and collision-safe fixed-tick movement impulse through RuntimeScene/PhysicsWorld while Combat remains the timing authority. Prove blocked-wall behavior, exact displacement, retained facing, and cross-presentation determinism before adding roll animation or VFX.
