# Player V2 Runtime Integration Checkpoint

## Outcome

The approved main-character V2 strips replace the live player's presentation without deleting the old atlases. Five authored walk directions and five authored dodge directions cover all eight facings through forward-order horizontal mirroring for west, southwest, and northwest.

## Asset pipeline

- Runtime assets use `player-v2-*` names so the previous atlases remain available as a fallback.
- Seven checkerboard review sheets received a genuine-alpha background extraction pass with the built-in image-generation workflow. The three review sheets that already had transparent alpha were copied directly.
- `tools/normalize-player-v2-alpha.ps1` clears only sub-visible alpha below 16/255. `tools/pack-player-v2-atlases.ps1` detects the eight transparent-separated poses and places them unscaled into uniform padded cells. `tools/generate-player-v2-metadata.ps1` then emits untrimmed Aseprite-shaped JSON, preserves forward frame order, and duplicates mirrored frames with `ic2d_flip_x`.
- Walk timing is `[7, 5, 6, 6, 7, 5, 6, 6]` fixed ticks with footsteps on contact frames one and five.
- Dodge timing is `[1, 1, 2, 2, 2, 2, 1, 1]` fixed ticks with presentation events at launch and recovery.

## Runtime contract

The sixteen idle/move states remain mandatory for every locomotion binding. Eight `dodge_*` states are an optional complete group, so existing NPC and stress-copy bindings remain unchanged. Combat still owns dodge permission, frozen direction, movement multiplier, invulnerability, duration, and cooldown; RuntimeScene receives only a generic `dodging` presentation flag and chooses the matching clip.

Aim facing cannot rotate the body during an active dodge. Application converts Combat's frozen world dodge direction into camera presentation space, and RuntimeScene restarts the selected dodge strip at frame zero. When the action ends, locomotion resumes with phase-preserving transition behavior.

## Verification route

- normalize alpha with `tools/normalize-player-v2-alpha.ps1`, pack uniform runtime cells with `tools/pack-player-v2-atlases.ps1`, then regenerate metadata with `tools/generate-player-v2-metadata.ps1` (run each through `powershell -NoProfile -ExecutionPolicy Bypass -File`);
- validate alpha, bounds, timing, events, mirror order, and grounding with `tools/validate-sprite-atlases.ps1`;
- run Debug and Release CTest;
- run the deterministic replay verifier and the real `--smoke-dodge` route;
- manually inspect idle, walk, turn, dodge, and recovery at 1x for every direction.
