# Target Health and Death Checkpoint

## Outcome

Projectile impacts now drive a deterministic gameplay consequence. The patchwork NPC is a stable 54-health target dummy; three 18-damage needle hits reduce it to zero, emit one death event, and retire its physics body, sprite, and shadow until the running scene is reset.

The implementation follows the Hazel-inspired deep-module boundary used elsewhere in IC_2DE: `Health` owns gameplay rules and copied values, `RuntimeScene` owns actor-to-physics/presentation lifetime, and rendering/editor panels consume snapshots without mutating either system.

## Working

- `Health::register_target`, `submit`, `fixed_update`, `snapshot`, `drain_events`, and `reset` form one backend-independent public seam.
- Damage is accepted only for the next sequential fixed tick, a live registered target, a positive finite amount, and a non-zero stable hit identity.
- Hit identity combines the firing actor UUID and projectile ID, so one projectile instance cannot apply damage twice.
- Damage clamps to remaining health and emits a copied `DamageAppliedEvent` containing requested/applied damage plus before/after values.
- Reaching zero emits exactly one copied `ActorDiedEvent` after its damage event.
- Projectile impact, health reduction, and actor retirement occur in that order during the same fixed tick.
- Enemy role bindings are discovered from the scene rather than hard-coding the patchwork NPC UUID in the application loop.
- A dead non-player actor is removed from Physics2D queries and Render2D presentation; `RuntimeScene::reset()` recreates its body and restores presentation.
- The viewport draws a read-only pixel health bar. Statistics reports health targets, applied hits, deaths, rejected duplicates, latest damage, and latest death.
- Applying a validated editor runtime copy constructs a matching fresh Health instance before swapping the running scene.

## Measured

- The initial Health test was red at link time with seven unresolved public symbols before the implementation existed.
- The fixed-tick tests prove three unique 18-damage hits produce health `54 -> 36 -> 18 -> 0`, three damage events, and one death event.
- A separate regression proves replaying the same actor/projectile hit identity applies damage once and increments the duplicate-rejection counter.
- All 22 Debug and all 22 Release CTest targets pass.
- Sprite-atlas validation passes for the player, diagonals, tree, and grounding contracts.
- The real Debug NVIDIA RTX 2080 Ti/OpenGL editor route completed 66 fixed ticks with three projectile spawns, three impacts, one deterministic death, one matching scene retirement, a captured frame, and clean texture lifetime.
- The packaged Release editor repeated the same final camera X/Z state `-106.589951/-108.697289` at 30 Hz, 60 Hz, and unlocked presentation. Every run produced the same three impacts, one death, one retirement, and exit code zero.
- Packaged content validation and the live texture hot-swap/resource-release probe pass.
- `dist/IC_2DE-Editor-Windows-x64.zip` is 11,520,355 bytes with SHA-256 `4273AB7D355C3A97ED8C02F9E38F032FD926233BD4E566C3B0FD6923D30B7DF1`.

## Manual check

1. Launch `dist/editor-windows-x64/IC_2DE-Editor.exe`.
2. Keep the pointer inside the Viewport and shoot the patchwork NPC.
3. Confirm its bar drops by one third per hit and that the third hit removes the NPC and shadow.
4. Open Statistics and confirm target `1014` reports `0 / 54`, `DEAD`, three applied hits, and one death.
5. Press F5 or use the editor reset action. Confirm the NPC, shadow, physics collision, and full health bar return.

## Broken or deferred

- The 54-health value is the temporary target-dummy default. Authored health data/components arrive when enemy authoring needs its first editable stat.
- There is no hit flash, impact particle, knockback, floating damage, death animation, loot drop, corpse, or camera response yet.
- Damage resistances, armour, status effects, invulnerability, teams/factions, and healing are intentionally absent.
- The automated route proves fixed-tick results, scene retirement, rendering startup/capture, and resource release. Subjective hit readability and reset feel still need the manual check above.

## Learned

Stable entity identity alone is insufficient for damage deduplication: the hit instance needs its own stable identity as well. Keeping the health transition independent from scene storage made duplicate rejection and event ordering cheap to prove headlessly, while a separate runtime retirement seam kept Box2D and rendering concerns out of gameplay rules.

The first failed GPU route also confirmed that automated controller aim is camera-relative. Converting the authored player-to-target world vector through the existing camera-direction contract fixed the route without adding a test-only gameplay shortcut.

## Next small step

Add a fixed-tick dodge state behind Combat with explicit duration, cooldown, and invulnerability queries. Start with failing public tests, expose the state in Statistics, then add one short editor smoke route before any roll animation or VFX.
