# Prefabs and Command-Based Scene Edits Checkpoint

## Outcome

IC_2DE now has reusable authored content and one undoable seam for changing it. Scene schema 7 adds prefab definitions, prefab instances, and per-instance field overrides; `SceneEditor` puts every document mutation behind bounded undo/redo with candidate-copy atomicity.

This is the third expanded Hazel adaptation stage. The reference remains TheCherno/Hazel commit `1feb70572fa87fa1c4ba784a2cfeada5b4a500db` under Apache-2.0; no Hazel source was copied. Audio remains deferred.

## Deep module decisions

**Prefabs expand at load, not at runtime.** `SceneDefinition::load()` turns every `prefab_instance` record into an ordinary `SceneEntityDefinition` whose `prefab_id` records where its sprite came from. `RuntimeScene`, `World`, and the render path learn nothing new, and the testbed's entity count, replay hash, and rendering are unchanged by converting content to prefabs.

**One identity space, one placement order.** Prefab UUIDs, entity UUIDs, and instance UUIDs are validated as one scene-unique set, so a definition can never be mistaken for a placement. Entities and instances are returned in authored source order rather than grouped by record type, so interleaved authored content stays predictable.

**Instance ids stay in the entity id space.** Physics bindings and animation bindings reference a prefab instance exactly as they reference an authored entity. The testbed fixture proves this with an animated, physics-bound player authored as an instance.

**Commands describe intent; history stores documents.** `SceneEditor` exposes rename, move, create-instance, and destroy-instance. Each command applies to a private candidate copy and is committed only on success, so a rejected command leaves the document and the history untouched. Undo and redo are the same mirrored operation over stored document states, which restores untouched authored bytes exactly instead of reconstructing them from an inverse-edit description. History is bounded (64 steps by default, roughly 5 KB per step for the current test area), and a saved state that falls out of a bounded history keeps the document marked modified rather than falsely clean.

**Saving a copy is not saving.** `modified()` clears only when the destination is the opened document. Save-as leaves unsaved state intact.

## Schema 7

- `prefab=id|uuid|name|width|height|origin_x|origin_y|r|g|b|a|layer|texture_id`
- `prefab_instance=id|uuid|prefab_id|name|physics_binding|x|y|z`
- `prefab_override=instance_id|field|comma_separated_value`

Overridable fields are `sprite_size`, `sprite_origin`, `tint`, `layer`, and `texture`. One instance may override each field at most once. Unknown prefabs, orphaned overrides, repeated override fields, and identity collisions fail before window and GPU startup.

`SceneDocument::migrate_to_current()` now accepts schema 5 and schema 6. Schema 5 gains deterministic UUIDs as before; schema 6 migrates by version alone because schema 7 only adds optional records. Both paths are idempotent.

## Content

The test area now declares `tree` and `ground-shadow` prefabs and places four instances, two of which override sprite size and tint. The independent `compact_encounter` fixture declares its own shadow prefab and two bound instances, proving prefabs are not tied to the packaged scene. Both scenes keep their previous entity counts, UUIDs, and appearance.

## Verification

- Scene tests add prefab expansion in authored order, override application, unknown-prefab rejection, identity-collision rejection, orphaned-override rejection, repeated-override rejection, and schema 6-to-7 migration.
- New scene editor tests cover undo/redo state restoration, redo-branch discard, rejected commands leaving history untouched, physics-ownership and animation-reference guards, deterministic instance identity, duplicate-id rejection, override removal with its instance, save-as versus save semantics, and bounded-history behavior.
- Debug CTest: 14 of 14 passed. Release CTest: 14 of 14 passed.
- Release presentation verification at 30 Hz, 60 Hz, 120 Hz, monitor-matched, and uncapped retained replay hash `7074030210802259671` - unchanged by the prefab conversion.
- The packaged Shipping GPU smoke loaded schema 7 with fourteen entities, ten physics bodies, two animated entities, and fourteen stable UUIDs; it completed 300 ticks, retained the replay hash, passed ground, physics, animation, and texture-lifetime validation, and shut down cleanly.
- Combined-folder Debug content validation passed against the adjacent runtime manifest and shared Content tree.
- No leftover scene temporary files remain.
- `dist/IC_2DE-Windows-x64.zip` contains ten entries and both `IC_2DE-Debug.exe` and `IC_2DE.exe`. It has 13,107,253 expanded bytes and 5,890,075 archive bytes. SHA-256: `99F5B4699BF45A7E6972B4A33FCDAE5D96665F7DCD97F49B75A5F71E86AC3995`.

## Next checkpoint

Add typed engine events and an owned scene/layer stack with deferred transitions, keeping physics and animation results buffered at their existing seams. That gives the future editor shell somewhere to route selection, play-mode transitions, and cross-module notifications without a global application singleton.
