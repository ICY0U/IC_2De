# Combat Command Foundation Checkpoint

## Outcome

Logical gameplay input now crosses a deterministic Combat seam instead of ending at editor telemetry. The application translates render-frame input into copied `CombatCommand` values, the Combat module consumes them only on the next strict fixed tick, and the editor reports the resulting snapshot and intent events.

This checkpoint deliberately stops before weapon behavior. Fire, reload, dodge, and swap produce authoritative intent events, but they do not yet spawn projectiles, change ammunition, grant invulnerability, apply damage, or drive animation.

## Working

- `CombatCommand` carries one stable actor UUID, an optional normalized X/Z aim update, and logical fire/reload/dodge/swap edges.
- `Combat::submit()` validates identity, content, and finite non-zero aim without exposing its queue.
- `Combat::fixed_update()` accepts only one-based sequential ticks and rejects invalid ticks before consuming buffered input.
- Commands remain FIFO; intents within one command use a stable order and copied events receive contiguous sequence IDs.
- Consecutive aim-only samples for the same actor coalesce before the fixed tick, preventing high presentation rates from flooding a 60 Hz simulation.
- The locomotion-bound player sprite is the authoritative actor identity, so the player shadow cannot become the combat actor even though it shares the physics body.
- Mouse position is mapped through either the docked editor viewport or standalone letterboxed canvas, inverse-projected through camera pitch/yaw, and normalized in world X/Z.
- Mouse clicks over editor panels do not become fire commands. Controller right-stick aim remains camera-relative and is normalized before submission.
- Runtime reset and applying an edited scene reset Combat state, observations, submitted aim, event sequence, and fixed-tick numbering together.
- Interact and choose-extraction remain logical inputs for the later RaidSession seam; Combat does not claim them.

## Measured

- The aim-coalescing test first failed with three pending/consumed commands, then passed with one newest command.
- The canvas projection test first failed at the missing inverse-projection symbol, then passed for right, forward, yaw rotation, zero direction, and invalid dimensions.
- All 19 Debug CTest suites pass; `ic2de.combat` covers buffering, copied events, FIFO/stable intent order, aim normalization, rejected ticks, reset, invalid commands, and render-rate coalescing.
- The Debug editor and runtime targets compile with project warnings treated as errors.
- The Release editor-only package validates adjacent content and passes the real GPU texture hot-swap/resource-lifetime probe on the development RTX 2080 Ti.
- `dist/IC_2DE-Editor-Windows-x64.zip` is 11,496,975 bytes with SHA-256 `960A7FA224BF22F20FF69137D6F837A994FD1D8C8EEDCB220C1C6BB3D9AF486A`.

## Manual test

Run `dist/editor-windows-x64/IC_2DE-Editor.exe` and open **Statistics**:

1. Move the pointer over the Viewport. `world aim X/Z` should change as the pointer moves around the character.
2. Press left mouse, R, Space, and Q separately. `last` should report Fire, Reload, Dodge, and Swap weapon after the next fixed tick, and the event number should increase once per press.
3. Hold a key. The logical action may show `DOWN`, but the emitted intent count must increase only on the initial pressed edge.
4. Click an editor panel. It must not emit Fire.
5. Pause, press an action, then single-step with O. The queued command should be consumed by exactly that fixed tick.
6. Press F5 or the Statistics Reset button. Combat tick/event numbering and last-event telemetry should restart; the current pointer aim may immediately queue again on the following render frame.

The automated checks prove command semantics and projection math. They do not prove mouse/controller feel; that remains a manual play check.

## Broken or deferred

- Combat intents currently have no visible game effect.
- Weapons, ammunition, reload duration, projectiles, health, damage identity, death, dodge movement, and invulnerability are intentionally deferred to the next tested slices.
- Interact and choose-extraction have no consumer yet.
- No automated UI driver presses live input inside the editor, so the Statistics interaction checklist remains manually unverified in this checkpoint.

## Learned

- Render input must not map one-for-one to simulation commands: high-refresh pointer samples need explicit latest-value coalescing while action edges require lossless FIFO buffering.
- A physics body can own multiple presentation entities. Gameplay identity must select the controllable locomotion entity rather than whichever bound sprite happened to be authored first.
- Editor mouse capture is too broad for gameplay aiming. The actual rendered Viewport rectangle is the correct authority for pointer projection and firing.

## Next small step

Write the first failing fixed-tick Combat tests for the needle pistol data and state: fire cooldown, ammunition consumption, reload start/completion, and a copied projectile-spawn event. Keep collision, damage, death, and dodge timing as following green checkpoints so each behavior can be tested and played independently.
