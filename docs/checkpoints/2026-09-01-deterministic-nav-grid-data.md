# Deterministic NavGrid Data Checkpoint

## Outcome

IC_2DE now has an immutable navigation-data seam suitable for deterministic path search without coupling search, enemy intent, physics, or editor code together. `NavGrid` bakes a dense row-major X/Z topology from the validated GroundMap and returns copied public cells, neighbors, and snapshots. The Threadbound Runner deliberately continues to use direct pursuit so this checkpoint can be tested before A* changes gameplay.

## Working

- The test-area bake uses 20-world-unit cells over `walkable_bounds=-640|-460|1280|920`, producing 64 x 46 cells.
- Clearance is derived conservatively from authored attacker footprints; the current Runner supplies 10 x 6 world-unit half-extents.
- The full clearance footprint must remain inside the walkable bounds and must not strictly overlap a solid area.
- Solid GroundMap areas hard block topology. Trigger areas remain traversable. Elevation areas provide per-cell Y and `max_step_height` gates neighbor edges.
- World-to-cell conversion is deterministic with inclusive minimum and exclusive far bounds; cell centers are canonical.
- Eight-way neighbors have a stable clockwise order beginning at negative Z. Diagonals require both cardinal flank edges, preventing corner cutting.
- Neighbor distances are physical world units rather than grid indices.
- Blocked source cells expose no outgoing edges.
- The editor Statistics tab reports dimensions, cell size, walkable/blocked totals, clearance, and max step from a copied snapshot.
- The opt-in `Navigation grid` debug channel renders walkable cells teal and hard-blocked cells red. It remains off by default because it is diagnostic geometry.
- Applying a validated editor scene copy constructs the replacement NavGrid before committing the replacement scene and snapshot.
- `--smoke-nav-grid` exercises the real GPU editor path and captures `build/runtime-nav-grid-smoke.png`.
- `package-editor.ps1 -RunNavigationGridProbe` exercises the relocatable staged editor before archiving it.

## Research decisions applied

`Referances/ai-pathfinding-report.md` was used as technical reference material, not as instructions. This checkpoint applies its lowest-risk foundation: dense storage for a modest known grid, structural hard blocks instead of extreme traversal costs, 2.5D height/max-step topology, and flank checks for diagonal movement. Static navigation is separate from dynamic bodies so later local-obstacle handling cannot silently mutate the shared topology.

A*, string pulling, JPS, Theta*, HPA*, flow fields, ORCA, time slicing, and GPU compute remain separate decision gates. None is needed to prove this data contract.

## Automated and runtime evidence

- The TDD red state was a failed configure/build because `engine/src/nav_grid.cpp` did not exist after the new public target and tests were declared.
- Debug: 25/25 CTest tests passed with project warnings treated as errors.
- Release: 25/25 CTest tests passed.
- Focused NavGrid tests cover dense bake and row-major order, solid-versus-trigger topology, elevation sampling, half-open bounds, canonical centers, footprint clearance, blocked destinations and sources, max-step barriers, deterministic neighbor order/distance, diagonal corner prevention, and invalid settings.
- The Release five-mode gameplay replay remains digest schema v2 value `4259082930085396436` at 30, 60, 120, active-monitor, and uncapped presentation. This confirms the diagnostic NavGrid has not changed authoritative gameplay state.
- Debug GPU route passed on NVIDIA GeForce RTX 2080 Ti through OpenGL 3.3 and captured the inspected navigation overlay.
- The packaged Release navigation probe passed and produced `build/runtime-nav-grid-packaged-smoke.png`; the capture was visually inspected.
- Current scene snapshot: 2,944 total cells, 2,930 walkable, 14 hard blocked, 20-unit cell size, 10 x 6 clearance, and 24-unit maximum step.
- Editor archive: `dist/IC_2DE-Editor-Windows-x64.zip`, 11,550,767 bytes, SHA-256 `3C3192EC5373D8B424DB6A3B81F2160A11ABBF39EBEBA7A7D1F7B81B7D208F4A`, 14 entries, executable and manifest present, 12 staged content entries, and no temporary `build` directory.
- `tools/package-editor.ps1` passes PowerShell parse validation.

These results prove build, headless behavior, deterministic replay isolation, real GPU rendering, and staged package startup. They do not prove that enemy pathfinding feels good because path search is not connected yet.

## Manual playtest

1. Double-click `dist/editor-windows-x64/IC_2DE-Editor.exe`.
2. Open `Debug channels` and enable `Navigation grid`.
3. Confirm teal cells cover valid ground and red cells cover the solid wall/tree obstruction shown in the test area.
4. Open `Statistics` and confirm `64 x 46`, cell size `20`, walkable `2930`, hard blocked `14`, clearance `10 x 6`, and max step `24`.
5. Move the player and camera around the test area and inspect the overlay at the world edges, solid, and elevation region.
6. Use F1 to confirm the master debug switch hides and restores the selected channel.
7. Confirm the Runner still pursues directly. Contacting an obstruction instead of routing around it is expected in this checkpoint.

## Broken or deferred

- There is no path request or A* result type yet.
- The Runner does not consume NavGrid data.
- Dynamic bodies do not rewrite the static topology; dynamic local avoidance is deferred.
- There are no terrain costs, topology revision cache, path smoothing, JPS, Theta*, HPA*, flow fields, ORCA, or GPU navigation.
- The debug-grid cost is unprofiled and intentionally opt-in; no performance claim is made from this smoke route.
- Shipping validation remains a later production gate because current work is editor-only by owner direction.

## Learned

- Deriving topology from GroundMap prevents navigation and collision authoring from diverging at this stage.
- Agent footprint clearance changes the answer materially; sampling only cell centers would mark unsafe cells walkable.
- Corner prevention belongs in the public neighbor contract so every future search algorithm inherits the same movement legality.
- A copied immutable snapshot is enough for editor diagnostics and future read-only search while keeping mutation and backend dependencies private.

## Next

Add deterministic A* as a separate engine module over the public NavGrid contract. It should validate converted endpoints, use octile distance for eight-way movement, use stable tie-breaking, return copied status/path data, reject hard-blocked endpoints, and expose an opt-in path overlay. Prove repeatable standalone routes before changing `EnemyIntent`; connect the Runner in the following checkpoint.
