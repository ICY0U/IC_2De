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

## Authored scene - schema 7

A scene describes the playable ground plane, camera, simulation policy, texture assets, physics bodies, visible entities, and animation. The development testbed loads `game/assets/runtime/test_area.scene`; a packaged runtime reaches the same file through its runtime project manifest.

Required singleton settings:

| Key | Value fields |
|---|---|
| `schema` | Must be `7`. |
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

The adapter consumes `frames[].frame`, millisecond `duration`, `meta.image`, `meta.size`, and inclusive `meta.frameTags` ranges. Directions `forward`, `reverse`, `pingpong`, and `pingpong_reverse` map to engine-owned loop modes and frame order. The optional per-frame `ic2d_events` string array is an IC_2DE extension for event IDs such as `footstep`.

Durations are quantized once at load time using `max(1, (milliseconds * fixed_update_hz + 500) / 1000)`. The integer rule is deterministic and independent of presentation FPS. Hash-format JSON, empty tags, invalid ranges, duplicate/invalid clip IDs, rotated or trimmed frames, out-of-sheet rectangles, unsafe paths, and missing atlases fail before graphics startup. Metadata and `meta.image` must remain beneath the scene directory.

### Ground areas

```text
ground_area=kind|x|z|width|depth|elevation|tag
```

`kind` is `solid`, `elevation`, or `trigger`. Elevation areas use `elevation`; triggers use the integer `tag`. Unused fields remain explicit so every record has one stable shape.

### Physics boxes

```text
physics_box=id|role|motion|x|z|half_width|half_depth|category|mask|tag|sensor|fixed_rotation|linear_damping|angular_damping|density|friction
```

Roles are `player`, `primary_prop`, `enemy`, or `generic`; motion is `static`, `kinematic`, or `dynamic`. Each scene requires exactly one kinematic `player` and one dynamic `primary_prop`. Physics uses X/Z world pixels in authored data and converts privately to Box2D metres at runtime. Gravity scale is currently fixed to zero because this solver represents the ground plane; World Y elevation remains owned by GroundMap.

### Sprite entities

```text
entity=id|uuid|name|physics_binding|x|y|z|width|height|origin_x|origin_y|r|g|b|a|layer|texture_id
```

`uuid` is a non-zero, scene-unique 64-bit integer that remains stable when the entity is copied into a runtime World or captured in a World snapshot. The textual `id` remains the human-authored cross-reference used by physics and animation records; renaming the display `name` does not change identity. Use `-` for no physics binding or no texture. A physics-bound entity must begin at its body's X/Z center; its authored Y and any offset become its maintained render offset. Multiple entities can bind to one body, allowing a sprite and shadow to synchronize without application-side entity IDs.

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

An animated entity must be physics-bound and declare exactly one `initial=true` record. Every locomotion map supplies all sixteen states: idle and move variants for `south`, `southwest`, `west`, `northwest`, `north`, `northeast`, `east`, and `southeast`. For example, `idle_northwest` and `move_northwest` are separate records. Several states may intentionally reference the same clip, as the placeholder enemy currently does.

Runtime facing divides the X/Z movement vector into eight equal 45-degree sectors. Cardinal directions own a 45-degree cone centered on their axis, with boundaries 22.5 degrees from the axis. When movement stops or is obstructed, the last idle-facing variant is retained.

## Validation and ownership

`SceneDefinition::load()` completes syntax, numeric, path, textual-ID/UUID uniqueness, cross-reference, GroundMap, camera, PhysicsWorld, animation-clip, and locomotion-map validation before the application creates a window. Diagnostics include the absolute scene path and a source line when one exists.

After graphics startup, `RuntimeScene` consumes the validated definition and owns its World, GroundMap, PhysicsWorld, texture handles, role lookup, animation players, reset state, fixed-tick synchronization, trigger state, and interpolated render snapshots. The application loop supplies only a ground-plane movement direction and does not identify individual World entities, animation clips, or physics bodies.

`SceneDefinition::load()` remains strict and accepts only the current schema. Tools must explicitly call `SceneDocument::migrate_to_current()` before loading older authored data. Schema 5 migrates by deriving deterministic, non-zero, scene-unique UUIDs from the scene and entity textual IDs, and schema 6 migrates by version alone because schema 7 only adds optional prefab records. Both paths are idempotent.

`SceneDocument` edits supported entity and prefab-instance fields by UUID while preserving comments and untouched records. It also creates prefab instances with deterministic identity derived from the scene and instance ids, and removes an instance together with the overrides that address it. A removal is refused while an animation binding still names the instance. `runtime_copy()` validates and materializes unsaved edits without changing the source file. `save_atomic()` writes a sibling temporary candidate, validates the complete scene through `SceneDefinition`, and replaces the destination only after validation succeeds. A malformed candidate leaves the previous destination bytes intact and removes the temporary file.

`SceneEditor` is the single undoable seam above the document. Rename, move, create-instance, and destroy-instance commands apply to a private candidate copy, so a rejected command leaves both the document and the history untouched. History is bounded, `undo()`/`redo()` restore authored records exactly, and `modified()` reports unsaved state; saving over the opened document clears it, while saving a copy elsewhere does not.
