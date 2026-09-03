# Fuse Enemy Runtime Integration Checkpoint

## Outcome

The active test scene now uses the approved tall Fuse enemy family instead of the two Patchwork NPC presentations. Fuse Stalker replaces the moving attacker/stress enemy, and the larger Fuse Tyrant replaces the stationary combat target. Existing simulation identity and behavior are preserved: only presentation dimensions, pivots, shadows, atlases, and animation bindings changed.

## Runtime assets

- `game/assets/runtime/fuse-stalker-atlas.png`: 960 x 672, 96 x 96 cells.
- `game/assets/runtime/fuse-stalker-atlas.json`: 20 clips and 110 frame records.
- `game/assets/runtime/fuse-tyrant-atlas.png`: 1280 x 896, 128 x 128 cells.
- `game/assets/runtime/fuse-tyrant-atlas.json`: 20 clips and 110 frame records.
- `tools/import-fuse-enemies.ps1`: deterministic source-sheet cleanup and native-grid atlas generation.

The importer removes the baked pale checker background, writes true RGBA transparency, downsamples with nearest-neighbor sampling, quantizes to the approved teal/brass/plum/orange palette, and grounds each pose to a stable root line.

## Scene mapping

- Entity `1016`, formerly the moving Patchwork attacker, is now `Fuse Stalker` using a 68 x 68 presentation and its existing attacker body, intent, navigation, collision, and UUID.
- Entity `1014`, formerly the Patchwork target, is now `Fuse Tyrant` using a 96 x 96 presentation and its existing target collision, 54-health contract, retirement behavior, and UUID.
- Their existing shadow entities remain paired but were resized and renamed.
- All eight idle and all eight movement directions are bound for both enemies.
- The player and unrelated scene entities were not changed by this replacement.

## Validation

- Debug configuration/build passed.
- Scene tests passed with the new 71-clip scene contract.
- Asset hot-reload tests passed.
- Atlas validation passed with 110 frame records per enemy, zero cell-boundary failures, and transparent corners.
- The real RTX 2080 Ti/OpenGL moving-attacker smoke completed 150 fixed ticks, one acquisition, one attack request, 110.699997 world units of collision-resolved travel, 12 player damage, and clean shutdown.
- The real GPU target-death smoke completed three projectile spawns, three resolved impacts, one deterministic death, one matching scene retirement, and clean shutdown.

## Deliberate limits

- Gameplay currently drives locomotion idle/move only. Hurt, collapse death, and explosion art is imported and named, but the health/intent state machine does not yet play those clips; target death still retires the entity immediately under the existing tested behavior.
- The approved source art has one true east-facing chase strip. North, south, and diagonal movement use that animated cycle or a mirrored form so movement remains animated, but dedicated direction-authored chase strips would improve perspective and limb readability.
- This change replaces the active scene presentation; it does not delete the older Patchwork source/runtime files.
