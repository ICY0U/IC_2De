# Needle-pistol State Checkpoint

## Outcome

The first Loomhold weapon now has deterministic, fixed-tick state behind the existing Combat seam. A successful fire command consumes magazine ammunition and emits a copied projectile-spawn result with stable identity and data-defined motion/damage parameters. Fire cooldown and reload behavior remain authoritative when rendering is uncapped, VSync-bound, or explicitly capped.

The editor Statistics panel now distinguishes an acknowledged Fire/Reload intent from a successful projectile result. It reports magazine and reserve ammunition, cooldown ticks, reload progress, total projectile spawns, and the latest projectile identity and parameters.

## Working

- One needle-pistol definition: 12-round magazine, 48 reserve rounds, 8-tick fire cooldown, 54-tick reload, 520 world-units/second projectile speed, 72-tick lifetime, and 18 damage.
- Per-actor weapon state is owned by Combat and reset with the rest of authoritative combat state.
- An eligible fire consumes exactly one magazine round and emits one `ProjectileSpawnedEvent`.
- Projectile and event sequences are stable, contiguous, copied values; no internal pointer or raylib/Box2D type crosses the interface.
- Fire attempts during cooldown or reload are acknowledged as intents but do not consume ammunition or emit projectile results.
- Reload completes on the exact authored fixed tick, transfers only missing rounds, and deducts the same amount from reserve.
- Live editor telemetry exposes the difference between commands, intents, and successful results.

## Measured

- The shot test first failed because no projectile result existed and ammunition/counts remained unchanged; it now passes.
- The cooldown test first observed seven invalid extra shots during the blocked window; it now permits the next shot only on tick `T + 8`.
- The reload test first observed no timed reload, no fire block, and no reserve transfer; it now completes exactly at start tick plus 54.
- All 20 Debug CTest targets pass.
- The Release editor package passed adjacent-content validation, the live NVIDIA/OpenGL texture hot-swap probe, and clean GPU texture/shader/framebuffer shutdown.
- `dist/IC_2DE-Editor-Windows-x64.zip` is 11,500,406 bytes with SHA-256 `C69B4F6F62249A63734F995942886D3BEB18AE5ED9A870A315F36167B3C9EC8F`.

## Manual check

1. Launch `dist/editor-windows-x64/IC_2DE-Editor.exe` and open the Statistics panel.
2. Move the pointer over the Viewport and left-click once. `Needle pistol` should change from `12 / 48` to `11 / 48`, the projectile counts should increase once, and the last projectile row should appear.
3. Click repeatedly during the displayed cooldown. Fire intents may continue to increase, but ammunition and projectile counts must change only for eligible shots.
4. Press R after spending at least one round. The 54-tick reload countdown should appear; left-clicks during it must not spawn or consume a shot.
5. At completion, the magazine should refill only by its missing amount and reserve should fall by exactly that amount.

The timing and state rules are automated. Mouse feel and readability of the live Statistics presentation remain manual interaction checks.

## Broken or deferred

- `ProjectileSpawnedEvent` is currently a hand-off contract; there is no travelling or visible projectile entity yet.
- Projectile origin resolution, lifetime expiry, Physics2D collision, ownership filtering, damage deduplication, health, hit reaction, and target death remain deferred to the following slices.
- Dodge, target dummy, combat smoke replay/hash, and presentation feedback are not part of this checkpoint.
- Holding left mouse does not create automatic fire because gameplay actions currently submit pressed edges; repeated semi-automatic shots require repeated clicks.

## Learned

Separating intent acknowledgements from successful result events makes cooldown/reload behavior directly observable without coupling input, presentation, and weapon rules. A later projectile module can consume the copied result at the authoritative tick and resolve the actor origin without enlarging the Combat interface.

## Next small step

Write failing fixed-tick tests for projectile travel and exact lifetime expiry, then implement the smallest engine-owned projectile state needed to make those tests pass. Collision and the target dummy follow after movement is deterministic and inspectable.
