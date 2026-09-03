# IC_2DE Data Formats

IC_2DE runtime content uses strict, versioned text documents. Unknown keys, duplicate singleton keys, malformed numbers, and unsupported schema versions are errors rather than silently ignored data. Lines are `key=value`; blank lines and lines beginning with `#` are ignored.

## Runtime project manifest - schema 1

`IC_2DE.runtime` is resolved beside the executable by the shipping entry point. It declares:

```text
schema=1
name=IC_2DE Shipped Test
asset_directory=Content
start_scene=test_area.scene
```

`asset_directory` and `start_scene` must be safe relative paths. Absolute paths, rooted paths, and any `..` component are rejected. The start scene is resolved beneath the manifest's asset directory.

## Authored scene - schema 10

A scene describes the playable ground plane, camera, simulation policy, texture assets, physics bodies, visible entities, and animation. The development testbed loads `game/assets/runtime/test_area.scene`; a packaged runtime reaches the same file through its runtime project manifest.

Required singleton settings:

| Key | Value fields |
|---|---|
| `schema` | Must be `10`. |
| `id` | Unique document identifier using letters, digits, `_`, `-`, or `.`. |
| `world_space` | Must be `x_y_z`. |
| `ground_plane` | Must be `x_z`. |
| `elevation_axis` | Must be `y`. |
| `walkable_bounds` | `x|z|width|depth` in world pixels. |
| `max_step_height` | Maximum discrete +Y step the controlled character can enter. |
| `camera` | `yaw_degrees|pitch_degrees|pixels_per_world_unit|zoom`. Initial focus is derived from the player body. |
| `physics` | `pixels_per_metre|substeps|gravity_x|gravity_z|enable_sleep|world_boundary_thickness`. |
| `ground_filter` | `category_bits|mask_bits` for solid ground and generated world boundaries. |
| `trigger_filter` | `category_bits|mask_bits` for GroundMap trigger sensors. |
| `player_speed` | Controlled-character speed in world pixels per second. |

Repeatable records use pipe-separated fields.

### Textures

File texture:

```text
texture=id|file|relative/path.png|pixel
```

Checker texture:

```text
texture=id|checker|width|height|cell_size|r1|g1|b1|a1|r2|g2|b2|a2|pixel
```

Generated radial-alpha texture:

```text
texture=id|radial|width|height|inner_r|inner_g|inner_b|inner_a|outer_r|outer_g|outer_b|outer_a|smooth
```

Radial textures interpolate from the inner color to the outer color and are useful for reusable soft shadows and glows. A square source can be stretched by an entity's sprite dimensions to form an ellipse without adding a bitmap asset or renderer special case.

Sampling is `pixel` or `smooth`. File paths are resolved relative to the scene file and must be safe relative paths without `..`; referenced files must exist during validation. Color channels range from 0 through 255.

### Aseprite sprite sheets

```text
aseprite=texture_id|relative/metadata.json|sampling|fixed_update_hz
```

This record creates both a file texture and all clips declared by the metadata's frame tags. Export Aseprite content with JSON-array ordering and tags:

```text
aseprite -b player.aseprite --sheet player.png --data player.json --format json-array --list-tags
```

The adapter consumes `frames[].frame`, millisecond `duration`, `meta.image`, `meta.size`, and inclusive `meta.frameTags` ranges. Directions `forward`, `reverse`, `pingpong`, and `pingpong_reverse` map to engine-owned loop modes and frame order. Optional IC_2DE per-frame extensions are `ic2d_events`, a string array for event IDs such as `footstep`, and `ic2d_flip_x`, a boolean that mirrors the selected source rectangle horizontally without modifying the atlas.

Durations are quantized once at load time using `max(1, (milliseconds * fixed_update_hz + 500) / 1000)`. The integer rule is deterministic and independent of presentation FPS. Hash-format JSON, empty tags, invalid ranges, duplicate/invalid clip IDs, rotated or trimmed frames, out-of-sheet rectangles, unsafe paths, and missing atlases fail before graphics startup. Metadata and `meta.image` must remain beneath the scene directory.

### Ground areas

```text
ground_area=kind|x|z|width|depth|elevation|tag
```

`kind` is `solid`, `elevation`, or `trigger`. Elevation areas use `elevation`; triggers use the integer `tag`. Unused fields remain explicit so every record has one stable shape.

### Derived navigation grid

Navigation data is derived at runtime; schema 10 adds no authored navigation record. `NavGrid` bakes an immutable dense row-major X/Z grid from `walkable_bounds`, `max_step_height`, and the current ground areas. The current editor checkpoint uses 20-world-unit cells and derives conservative agent clearance from the largest authored attacker footprint (falling back to the player when no attacker exists). In `test_area.scene` that produces 64 x 46 cells with 10 x 6 half-extents.

The minimum X/Z bounds are inclusive and the far edges are exclusive. Each cell stores a canonical center, sampled elevation, and walkability. A cell is hard blocked when the full agent footprint leaves the walkable bounds or strictly overlaps a `solid` area. `trigger` areas do not affect topology; `elevation` areas provide the 2.5D heightfield.

The bake also labels connected regions using that same neighbor contract, so component equality and reachability cannot disagree. Blocked cells are region zero. A search whose start and goal fall in different regions returns `unreachable` immediately with zero expanded cells, instead of expanding the whole of the start region to rediscover it on every request. This matters wherever content deliberately seals an area off, such as the enclosed player spawn in `perf_test.scene`.

Neighbors are returned in a stable clockwise order beginning at negative Z. Cardinal and diagonal destinations must be walkable and within `max_step_height`; a diagonal also requires both cardinal flank cells to be traversable from the source. Distances are stored in world units. A blocked source exposes no edges.

`snapshot()` returns a copy; `topology()` borrows the same data for the lifetime of the grid and is what search uses, so a path request does not duplicate every cell before reading the request. The snapshot is rebuilt when the editor applies a validated scene copy. Dynamic physics bodies are deliberately absent from the static topology and remain a separate local-obstacle concern. The grid is immutable topology input for A* and is not itself hashed. Future-affecting `NavAgentSystem` state is part of gameplay digest schema 3.

### Derived navigation paths

Path results are runtime-derived and add no schema-10 record. `find_nav_path()` accepts explicit cells; `find_nav_path_world()` first applies the grid's half-open conversion. Both return a copied result with one explicit status: found, start/goal out of bounds, start/goal blocked, or unreachable. A found path includes start and goal cells, total physical world distance, and the number of expanded cells.

Search uses eight-way A* with an octile heuristic. The open set resolves equal estimates by lower heuristic, then row, then column, so a symmetric topology returns one repeatable route. Connectivity and edge distances come only from `NavGrid::neighbors()`, preserving hard blocking, elevation limits, and no-corner-cutting as one source of truth. Returned cells never retain an internal grid pointer.

The editor builds one standalone reference path across the first usable solid obstruction for diagnostics. The Threadbound Runner separately requests and consumes its own route through `NavAgentSystem`: a new target cell, target identity change, invalid remaining route, or reactivation replans immediately; an unchanged route refreshes after a bounded 30 fixed ticks. Following advances through cell centres with a four-world-unit tolerance, then closes on the exact target point inside the goal cell. Blocked, out-of-bounds, and unreachable results produce zero motion instead of direct fallback movement, and unchanged failures wait for the bounded refresh rather than searching every tick.

Navigation-agent snapshots are canonical actor-UUID-ordered runtime data. They contain the target, current/goal cells, copied path, waypoint cursor, next repath tick, movement direction, search/advance counters, distance, and expansion count. Gameplay digest schema 3 validates and hashes this future-affecting state. Applying a validated scene copy constructs the replacement grid, reference path, and registered navigation-agent candidate before committing them atomically. The editor's focused overlay shows the active Runner route while it is pursuing and otherwise falls back to the standalone reference path.

### Runtime-only editor stress actors

Enemy stress actors are generated initialization data, not authored scene data, so schema 10 gains no stress-spawn record. Before the first fixed tick, `RuntimeScene` may copy the complete entity/body/animation graph of the first authored non-player actor for a requested physics role. Runtime UUIDs are allocated deterministically above the authored and prefab-expanded identity space; every copy receives its own World entities, physics body, sprites, shadow, locomotion animation player, Health target, EnemyIntent actor, and NavAgent registration.

The editor plans unique spawn positions from cells reachable from the player through the public `NavGrid` neighbor contract. It rejects cells occupied by the player, primary target, or existing attackers and never alters the source `SceneDocument`. Stress actors retain acquisition, pursuit, attack-state transitions, and attack requests, but the application suppresses their final Health damage submission to the player. Selecting `Restore authored scene` constructs a fresh runtime candidate from authored content, removes all stress copies, and restores normal enemy damage. Actor graph copying is rejected after simulation starts so runtime identity and module registrations cannot change halfway through a replay.

### Physics boxes

```text
physics_box=id|role|motion|x|z|half_width|half_depth|category|mask|tag|sensor|fixed_rotation|linear_damping|angular_damping|density|friction
```

Roles are `player`, `primary_prop`, `enemy`, `attacker`, or `generic`; motion is `static`, `kinematic`, or `dynamic`. Each scene requires exactly one kinematic `player` and one dynamic `primary_prop`. Every `attacker` must be kinematic and have at least one bound entity so intent, collision, World synchronization, and presentation share one stable actor UUID. The `enemy` role remains available for stationary target fixtures. Physics uses X/Z world pixels in authored data and converts privately to Box2D metres at runtime. Gravity scale is currently fixed to zero because this solver represents the ground plane; World Y elevation remains owned by GroundMap.

### Sprite entities

```text
entity=id|uuid|name|physics_binding|x|y|z|width|height|origin_x|origin_y|r|g|b|a|layer|texture_id[|depth_span]
```

`uuid` is a non-zero, scene-unique 64-bit integer that remains stable when the entity is copied into a runtime World or captured in a World snapshot. The textual `id` remains the human-authored cross-reference used by physics and animation records; renaming the display `name` does not change identity. Use `-` for no physics binding or no texture. A physics-bound entity must begin at its body's X/Z center; its authored Y and any offset become its maintained render offset. Multiple entities can bind to one body, allowing a sprite and shadow to synchronize without application-side entity IDs. Schema 10 optionally appends a non-negative `depth_span`; omitting it preserves the former zero-span behavior.

### Parenting

```text
parent=child_entity_id|parent_entity_id
```

An optional record, added in schema 12, naming the placement a placement belongs to. Both ends are textual entity or prefab-instance ids that must already exist. A child may name at most one parent, an entity may not parent itself, and a cycle is rejected, so the hierarchy is always a forest that can be walked to an end.

Parenting is an ownership link, not a transform link. A parent owns its children's lifetime: taking a parent out of play takes its whole subtree with it, which is what lets a used pickup remove the shadow it casts instead of leaving it on the ground. A child that has to follow a moving parent still shares its `physics_binding`, exactly as it did before, so the two concerns stay separate.

`SceneDefinition::load()` resolves each record onto the child's `parent` field as the parent's UUID, so runtime and tools read identity rather than the textual id space. Unparented placements carry a zero parent. The editor's Hierarchy panel draws the resulting tree, and `RuntimeScene::retire_entity()` walks it.

### Prefabs and instances

A prefab is a reusable sprite template with its own persistent identity. An instance places that template in the scene and may override individual sprite fields.

```text
prefab=id|uuid|name|width|height|origin_x|origin_y|r|g|b|a|layer|texture_id
prefab_instance=id|uuid|prefab_id|name|physics_binding|x|y|z
prefab_override=instance_id|field|comma_separated_value
```

Overridable fields are `sprite_size` (`width,height`), `sprite_origin` (`origin_x,origin_y`), `tint` (`r,g,b,a`), `layer`, and `texture` (a texture id or `-`). One instance may override each field at most once; an override naming an instance that does not exist is an error.

Prefab UUIDs, entity UUIDs, and instance UUIDs share one scene-unique identity space, so a prefab definition can never be confused with a placement. Instance ids share the textual-id space with entity ids, which lets physics and animation records reference an instance exactly as they reference an authored entity.

`SceneDefinition::load()` expands every instance into a `SceneEntityDefinition` whose `prefab_id` records where its sprite came from; entities and instances are returned in authored source order. Runtime code therefore consumes prefab content through the entity list it already understands, and unused prefab definitions remain available to tools.

### Animation clips and frames

```text
animation_clip=id|texture_id|loop_mode
animation_frame=clip_id|source_x|source_y|source_width|source_height|duration_ticks|events
```

Loop mode is `once`, `loop`, or `ping_pong`. Source rectangles select frames from the referenced scene texture. Durations are positive integer fixed-simulation ticks, so playback does not depend on presentation FPS. Use `-` for no event or a comma-separated list of event IDs. Events are copied into engine-owned results whenever playback enters that frame.

### Locomotion bindings

```text
animation_binding=entity_id|locomotion_state|clip_id|initial
```

An animated entity must be physics-bound and declare exactly one `initial=true` record. Every locomotion map supplies all sixteen core states: idle and move variants for `south`, `southwest`, `west`, `northwest`, `north`, `northeast`, `east`, and `southeast`. For example, `idle_northwest` and `move_northwest` are separate records. Several states may intentionally reference the same clip, as the placeholder enemy currently does.

Dodge presentation is optional. An entity that declares any `dodge_<direction>` record must declare all eight compass directions. RuntimeScene selects those clips only while the generic player-motion command has `dodging=true`; authoritative displacement, invulnerability, duration, and cooldown remain owned by Combat. The player V3 dodge clips total exactly twelve fixed ticks, matching the authored gameplay action. Its authored variants are assigned by compass sector: south back-hop, southwest/southeast slide, west/east sidestep, northwest/northeast roll, and north forward dodge.

Seated idle presentation is optional and forms a complete two-state group: `seated_south` and `seated_north`. A stationary player facing north or south enters the appropriate seated loop after 180 fixed ticks. Movement, dodge, shooting, or changing to another facing cancels the delay or leaves the seated state. The current V3 art begins directly at the seated loop because the approved source set contains no sit-down or stand-up transition strip.

Shooting presentation is optional and forms a complete four-state group: `shoot_south`, `shoot_west`, `shoot_north`, and `shoot_east`. RuntimeScene selects the nearest authored cardinal view from the eight-way aim direction. A monotonic successful-shot sequence starts or restarts the nine-fixed-tick shooting strip for every projectile actually spawned, so held fire retriggers recoil without coupling rendering to input polling.

Runtime facing divides the X/Z movement vector into eight equal 45-degree sectors. Cardinal directions own a 45-degree cone centered on their axis, with boundaries 22.5 degrees from the axis. When movement stops or is obstructed, the last idle-facing variant is retained.

Switching between ordinary directional locomotion clips preserves the normalized cycle phase rather than restarting, so a character that turns while walking keeps its gait instead of snapping back to the first frame. Dodge, seated, and shooting action transitions restart from their first frame; clips that play once fall back to a restart.

### Automatic animations

```text
animation_auto=entity_id|clip_id|initial_tick_offset
```

A deterministic looping clip for entities that animate independently of physics locomotion, such as foliage, water, or machinery. Unlike a locomotion binding, an automatic animation does not require a physics-bound entity, but one entity cannot declare both. The clip and the entity must already exist.

`initial_tick_offset` advances playback by that many fixed ticks once at construction and again on reset. It staggers repeated props authored from the same clip so they do not animate in lockstep, and because the offset is applied in integer ticks the result stays deterministic and independent of presentation FPS.

## Validation and ownership

`SceneDefinition::load()` completes syntax, numeric, path, textual-ID/UUID uniqueness, cross-reference, GroundMap, camera, PhysicsWorld, animation-clip, and locomotion-map validation before the application creates a window. Diagnostics include the absolute scene path and a source line when one exists.

After graphics startup, `RuntimeScene` consumes the validated definition and owns its World, GroundMap, PhysicsWorld, texture handles, role lookup, animation players, reset state, fixed-tick synchronization, trigger state, and interpolated render snapshots. The application supplies copied movement requests addressed by stable actor UUID. `RuntimeScene` validates that each requested actor is an active non-player kinematic body, resolves movement through GroundMap and Physics2D, and returns copied resolved-motion results; the caller never identifies animation clips or private physics handles.

`SceneDefinition::load()` remains strict and accepts only the current schema. Tools must explicitly call `SceneDocument::migrate_to_current()` before loading older authored data. Schema 5 migrates by deriving deterministic, non-zero, scene-unique UUIDs from the scene and entity textual IDs. Schemas 6 through 11 then migrate by version alone: schema 7 added optional prefab records, schema 8 added optional automatic-animation records, schema 9 added the optional `attacker` physics role, schema 10 added the optional entity depth span, schema 11 added optional interactable records, and schema 12 adds optional parent records. Every path is idempotent; migration never invents attacker records, depth spans, or parent links.

`SceneDocument` edits supported entity and prefab-instance fields by UUID while preserving comments and untouched records. It also creates prefab instances with deterministic identity derived from the scene and instance ids, and removes an instance together with the overrides and the parent record that address it. A removal is refused while an animation binding still names the instance, or while another placement is still parented to it. `runtime_copy()` validates and materializes unsaved edits without changing the source file. `save_atomic()` writes a sibling temporary candidate, validates the complete scene through `SceneDefinition`, and replaces the destination only after validation succeeds. A malformed candidate leaves the previous destination bytes intact and removes the temporary file.
