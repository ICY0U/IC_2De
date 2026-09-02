# Deterministic A* and Compact HUD Checkpoint

## Outcome

IC_2DE now has deterministic eight-way A* over the immutable NavGrid interface and a focused editor path visualization. The former wall of viewport diagnostics has been replaced by a compact HUD; detailed telemetry remains available in the docked Statistics panel. The Threadbound Runner deliberately continues direct pursuit until path consumption is tested as its own checkpoint.

## A* module

- `find_nav_path()` accepts explicit cells; `find_nav_path_world()` applies NavGrid's half-open conversion first.
- Every request returns a copied `NavPathResult` with an explicit status, start-to-goal cells, world-unit distance, and expanded-cell count.
- Endpoint failures distinguish start/goal out of bounds and start/goal hard blocked. A valid but disconnected request returns unreachable.
- Eight-way search uses an octile heuristic consistent with NavGrid's cardinal and diagonal distances.
- Equal candidates resolve by lower heuristic, then row, then column. Neighbor order and equal-cost parent retention are deterministic.
- NavGrid remains the sole movement-legality source, so search inherits hard blocking, footprint clearance, maximum-step barriers, and diagonal corner prevention.
- The current standalone scene route crosses the first authored solid's span: seven cells, 136.568542 world units, and eight expanded cells.
- The result remains independent from EnemyIntent, Physics2D, rendering, editor, and the gameplay digest.

## Display change

The chosen presentation is **compact HUD plus docked diagnostics**:

- The viewport keeps only the IC_2DE identity, FPS/pacing, F2/F1 tool hints, and critical pause/frame-stall warnings.
- Detailed engine, renderer, hot-swap, gameplay digest, input, combat, projectile, enemy, health, NavGrid, and A* telemetry remains in Statistics.
- `Navigation grid` and `Navigation path` are separate opt-in channels. A path uses teal for start, gold for intermediate cells, and magenta for goal.
- The dense grid can remain hidden while inspecting only the path.

Other viable options remain available if playtesting favours them:

1. Paged overlays: Gameplay, Performance, and Navigation, with one small page visible at a time.
2. Bottom status strip: one persistent editor bar with expandable details.
3. Inspect mode: no persistent diagnostic text; selecting actors or cells reveals contextual tooltips.

The compact-HUD option was selected because it removes clutter immediately and reuses the existing Statistics owner instead of creating a second telemetry source.

## Automated and runtime evidence

- The first red build rejected the declared target because `nav_pathfinding.cpp` did not exist.
- The next compile exposed ambiguous cell/world brace-initializer overloads; the interface was corrected to distinct `find_nav_path` and `find_nav_path_world` names.
- Debug: 26/26 CTest tests passed with warnings treated as errors.
- Release: 26/26 CTest tests passed.
- A* tests cover optimal open-grid diagonals, stable repeated hard-block detours, corner traps, elevation barriers, every endpoint failure, world conversion, copied-result ownership, same-cell paths, physical cost, and stable status names.
- Release replay remains digest v2 `4259082930085396436` at 30, 60, 120, monitor-synced, and uncapped presentation.
- Debug and packaged Release GPU path captures passed on NVIDIA GeForce RTX 2080 Ti / OpenGL 3.3 and were visually inspected.
- The capture clearly shows a seven-cell route around the solid and only three compact HUD elements.
- Asset validation and the complete editor package probe set passed: hot swap, dodge, combined gameplay, moving attacker, navigation grid, and navigation path.
- Final editor archive: `dist/IC_2DE-Editor-Windows-x64.zip`, 11,554,562 bytes, SHA-256 `B4383C7255A7FD007A473782B575B0CEE70E3AEDB326B4C16D481DE2372EEE70`, 14 entries, executable and manifest present, 12 content entries, and no temporary `build` directory.
- `tools/package-editor.ps1` passes PowerShell parse validation and `git diff --check` reports no whitespace errors.

## Manual playtest

1. Launch `dist/editor-windows-x64/IC_2DE-Editor.exe`.
2. Confirm the viewport no longer contains the previous multi-line statistics wall.
3. Open Debug channels and toggle `Compact HUD` to compare the clean shipped view.
4. Enable `Navigation path`; confirm teal, gold, and magenta cells bend around the solid near the tree.
5. Optionally enable `Navigation grid` and confirm the path never enters a red hard-blocked cell.
6. Open Statistics and confirm `Found`, seven cells, about 136.6 world units, and eight expanded cells.
7. Confirm the Runner still pursues directly; this is expected until the next checkpoint.

## Deferred

- Runner path requests, waypoint following, and repath policy.
- Dynamic local avoidance and moving-obstacle treatment.
- String pulling, JPS, Theta*, HPA*, flow fields, ORCA, and GPU navigation.
- A final shipped-game HUD; the compact overlay remains development tooling.
- Shipping validation remains a later gate under the current editor-only direction.

## Next

Connect the Threadbound Runner through a narrow navigation request/response seam. Repath on bounded, explicit triggers; follow copied cell centers through collision-resolved RuntimeScene motion; retain EnemyIntent ownership of acquire/pursue/attack state; and prove obstacle routing, attack range, reset, and replay determinism before introducing multiple attackers or WaveDirector.
