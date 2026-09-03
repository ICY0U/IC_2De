# Fuse Enemy Runtime Integration Checkpoint

## Outcome

The active test scene now uses the approved tall Fuse enemy family instead of the two Patchwork NPC presentations. Fuse Stalker replaces the moving attacker/stress enemy, and the larger Fuse Tyrant replaces the stationary combat target. Existing simulation identity and behavior are preserved: only presentation dimensions, pivots, shadows, atlases, and animation bindings changed.

## Runtime assets

- `game/assets/runtime/fuse-stalker-atlas.png`: 960 x 672, 96 x 96 cells.
- `game/assets/runtime/fuse-stalker-atlas.json`: 20 clips and 110 frame records.
- `game/assets/runtime/fuse-tyrant-atlas.png`: 1280 x 896, 128 x 128 cells.
- `game/assets/runtime/fuse-tyrant-atlas.json`: 20 clips and 110 frame records.
- `tools/import-fuse-enemies.ps1`: deterministic source-sheet cleanup and native-grid atlas generation.

The importer removes the baked pale checker background, writes true RGBA transparency, downsamples with nearest-neighbor sampling, quantizes to the approved teal/brass/plum/orange palette, and grounds each pose to a stable root line. Locomotion and idle sheets are segmented as complete connected poses before atlas placement, so artwork that crosses a presentation-grid boundary cannot leak a limb or fragment into the neighbouring runtime frame.

## Scene mapping

- Entity `1016`, formerly the moving Patchwork attacker, is now `Fuse Stalker` using a 68 x 68 presentation and its existing attacker body, intent, navigation, collision, and UUID.
- Entity `1014`, formerly the Patchwork target, is now `Fuse Tyrant` using a 96 x 96 presentation and its existing target collision, 54-health contract, retirement behavior, and UUID.
- Their existing shadow entities remain paired but were resized and renamed.
- All eight idle and all eight movement directions are bound for both enemies.
- Nonlethal damage plays the authored hurt one-shot and returns to locomotion. Lethal projectile damage retires gameplay collision immediately, presents only the authored death one-shot, then hides the enemy and its shadow. The explosion is a separate kamikaze path triggered only when a pursuing Stalker enters its 20-unit attack range of the player.
- The player and unrelated scene entities were not changed by this replacement.

## Validation

- Debug configuration/build passed, followed by the complete 37/37 CTest suite.
- Scene tests passed with the new 71-clip scene contract and complete hurt/death/explosion bindings for both enemy roles.
- Asset hot-reload tests passed.
- Atlas validation passed with 110 frame records per enemy, zero cell-boundary failures, zero detached-pose framing failures, transparent corners, and all six terminal clips authored as one-shots.
- The real RTX 2080 Ti/OpenGL moving-attacker smoke completed 150 fixed ticks, one acquisition, one attack request, 110.699997 world units of collision-resolved travel, 12 player damage, and clean shutdown.
- The real GPU hit smoke completed one hurt reaction with zero death and zero explosion completions.
- The real GPU Tyrant target-death smoke completed three projectile spawns, three resolved impacts, one deterministic death, one matching gameplay retirement, one completed death animation, zero explosions, and clean shutdown.
- The real GPU proximity smoke completed one close-range Stalker detonation with zero death animations and one explosion animation while applying 12 player damage.
- The 400-Stalker crowd-kill smoke requires projectile deaths to complete without producing proximity explosions.

## Deliberate limits

- The approved source art has one true east-facing chase strip. North, south, and diagonal movement use that animated cycle or a mirrored form so movement remains animated, but dedicated direction-authored chase strips would improve perspective and limb readability.
- This change replaces the active scene presentation; it does not delete the older Patchwork source/runtime files.
