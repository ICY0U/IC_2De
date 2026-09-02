# Character and Ambient Animation Checkpoint

## Working

- The first green patchwork design remains the player and uses transparent pixel-art atlases with eight authored gait frames for each of the eight movement directions.
- Cardinal and diagonal player atlases now use the same green patchwork character, close-fitting purple beanie, scale, and grounded frame convention; the former alternate diagonal character art is gone.
- The newer stitched doll is retained as a second complete character and now supplies the existing enemy/NPC entity's sixteen idle/move bindings.
- Direction and idle/move clip changes preserve normalized cycle phase, preventing a turn from restarting the walk on its first footfall.
- Per-frame `ic2d_flip_x` metadata now travels through Aseprite import, deterministic animation sampling, runtime sprites, render submission, and the raylib backend. The east-facing source gait is mirrored for west travel while retaining forward footfall order; the west-facing source idle is mirrored for east. Player movement uses 150 ms frames, cardinal idles use 180 ms frames, and tree sway uses 220 ms frames.
- Camera-relative input is split at the projection seam: simulation receives the camera-rotated X/Z vector while player animation receives the screen-relative direction that was actually pressed. Cardinal input can no longer select a diagonal or opposite-facing clip because of camera yaw.
- Scene schema 8 adds `animation_auto=entity|clip|initial_tick_offset` for deterministic animation that is independent of a physics locomotion state.
- Two tree prefab instances use the same eight-frame sway clip with different integer phase offsets. The opaque painted tree-sheet backdrop was removed, every root is bottom-aligned, and origin Y 0.94 sinks the visible roots 4.16 rendered pixels into the ground contact while soft radial shadows remain separate sprites.
- Player, NPC, and tree atlases use pixel sampling. Per-pose source rectangles and low-alpha fringe cleanup give the player transparent padding below the feet instead of cutting the idle/down rows at their cell boundaries.

## Measured

- Debug and Release: all 17 CTest suites passed in both configurations.
- `tools/validate-sprite-atlases.ps1` checks all 104 active player/tree source frames before packaging. It reports zero boundary failures, player bottom gaps of 3..6 and 3..5 source pixels, an exact one-pixel tree-root gap, forward west playback with horizontal presentation flip, minimum animation durations, and a 4.16-pixel tree contact offset.
- Headless content validation loaded `test_area` as schema 8.
- NVIDIA RTX 2080 Ti/OpenGL 3.3 smoke runs loaded the 1122x1402 cardinal player atlas, the new 1774x887 matching diagonal atlas, both 1536x1024 NPC atlases, and the cleaned 1536x1024 tree atlas; instantiated four animated entities; captured the dedicated west-walk and route frames; and released every texture and GPU resource cleanly. The west-walk capture visibly faces left.
- The 300-tick movement smoke retained replay hash `7074030210802259671` and passed ground, physics, animation-event, and texture-lifetime validation.
- The same replay hash passed at 30, 60, 120, monitor-synced, and uncapped presentation rates.
- The combined Debug/editor/Shipping package and ZIP were rebuilt with continuous window fitting. The archive is 14,427,123 bytes with SHA-256 `86DB9F34D87D8DD218E29A9C2E0580137ECD07BA15B87EE18855EF802A1CA0F1`.

## Broken or deferred

- The automatic smoke proves frame selection, resource ownership, and deterministic timing, but it cannot judge the full animation loop as a human does. Interactive play remains the final gait/sway quality check.
- Cardinal player idles have authored loops; diagonal player and NPC idles currently hold the first contact pose from each directional row. A later character-state pass can add dedicated diagonal breathing/blink idles alongside interact, attack, hit, and death.
- Audio remains deferred by owner direction.

## Learned

- Pixel-art direction changes read more cleanly when corresponding clips retain footfall phase; image cross-fades would create ghosted pixels.
- A 2.5D camera creates two equally valid movement directions: rotated world movement for simulation and unrotated screen intent for character presentation. Making both explicit prevents art selection from depending on camera yaw.
- Uniform atlas grids are unsafe for generated animation sheets. Per-frame bounds plus a package-time alpha/baseline gate catch sliced feet, background residue, and floating props before runtime.
- Ambient animation belongs in scene data, not application-loop special cases. Integer phase offsets keep repeated props organic without sacrificing deterministic replay.

## Next

Add an independent editor camera with pan/zoom and transform gizmos, then extend the character state graph with authored action and reaction clips before beginning the 2D lighting checkpoint.
