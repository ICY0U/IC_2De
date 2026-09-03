# Player V3 Motion Integration Checkpoint

## Outcome

The approved V3 review motion set is now the live player presentation. The runtime uses eight directional idle loops, north/south seated idles, four cardinal shooting views, and four visually distinct dodge families while preserving deterministic gameplay ownership and the existing scene schema.

The original review sheets under `art/review/main-character-v3-motion-study` and all V2 source/runtime files remain unchanged. Generated V3 assets use the `player-v3-` prefix.

## Production asset contract

- `tools/import-player-v3.ps1` flood-fills the opaque review-sheet background, isolates the authored poses, reduces them to a shared character palette, and emits hard-alpha nearest-neighbour runtime sheets.
- Every V3 frame uses a 48 x 48 cell, a bottom-centre root at x=24/y=40, pixel sampling, and enough transparent padding to prevent source-boundary contact.
- Idle loops use `[20, 10, 8, 8, 10, 16]` fixed ticks; seated loops use `[24, 12, 12, 12, 16, 20]`; shooting uses `[1, 1, 1, 2, 2, 2]`; each dodge action occupies twelve fixed ticks.
- The review set did not contain replacement walk cycles or a north dodge. Those poses are compatibility imports from V2, normalized into the same V3 canvas, root, palette, alpha, and metadata contract. They do not overwrite the V2 files.

## Runtime behavior

- All eight idle facings use the new V3 loops.
- After 180 stationary fixed ticks (three seconds at 60 Hz), a north- or south-facing player enters the matching seated loop. Moving, dodging, firing, or changing facing exits or cancels it.
- Every successful projectile spawn advances a monotonic shot sequence. RuntimeScene starts or restarts a nine-tick shooting presentation with dodge taking higher priority. Eight-way aim is reduced to authored south, west, north, or east views.
- Dodge presentation is compass-sector mapped: south back-hop; southwest and southeast slide; west and east sidestep; northwest and northeast roll; north uses the normalized compatibility dodge.
- Presentation priority is dodge, shooting, seated idle, then ordinary idle/movement. Action states restart at frame zero; normal directional turns preserve animation phase.

## Measured verification

- V3 atlas validation passed alpha, transparent-corner, source-boundary, root, exact-timing, and mirrored-west metadata checks for all 23 PNG/JSON pairs.
- Debug CTest: 33/33 passed.
- Release CTest: 33/33 passed.
- Deterministic gameplay digest `v3 7999596398074229857` matched at 30, 60, and 120 Hz, monitor-synchronised, and uncapped presentation modes.
- Real GPU seated-idle, held-fire, and dodge smokes ran through OpenGL 3.3 on an NVIDIA RTX 2080 Ti. Held fire produced three successful projectile spawns; dodge displacement, duration, recovery, invulnerability, and cleanup passed.
- `dist/IC_2DE-Editor-Windows-x64.zip` contains the editor executable and all 46 V3 runtime files (23 PNG plus 23 JSON files).

## Visual acceptance note

The generated atlases and sampled runtime captures were inspected enlarged and remain crisp, aligned, transparent, and unclipped. Hands-on review of every direction at native 1x scale remains valuable for final feel approval. The current seated change is intentionally immediate because the approved art supplies seated loops but no authored sit-down or stand-up transition frames; those transitions require an additional art strip before they can be genuinely animated.
