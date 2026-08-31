# Mutable Scene Document and Atomic Save Checkpoint

## Outcome

IC_2DE now has the mutable authored-scene foundation needed by a future editor. `SceneDocument` opens schema 6 scenes, identifies entities by persistent UUID, exposes supported mutations without leaking the text parser, produces validated unsaved runtime copies, and saves only complete valid candidates.

This adapts Hazel's serializer and edit/runtime-scene separation at an architectural level. The reference remains TheCherno/Hazel commit `1feb70572fa87fa1c4ba784a2cfeada5b4a500db` under Apache-2.0; no Hazel source was copied. Audio remains deferred.

## Deep module decision

The public interface is intentionally small:

- inspect authored entities;
- rename an entity by UUID;
- move an unbound entity by UUID;
- create an unsaved validated runtime copy;
- save a complete scene atomically;
- explicitly migrate supported old content.

Record parsing, source-line preservation, deterministic UUID generation, validation staging, temporary-file cleanup, and Windows replacement APIs remain private. Physics-bound X/Z positions cannot be edited through the document because PhysicsWorld owns those transforms.

## Persistence guarantees

- Untouched scene records, comments, and Aseprite declarations survive supported edits.
- Save writes a sibling `.ic2de.tmp` candidate and validates it with `SceneDefinition::load()` before replacement.
- Existing destinations use write-through replacement; new destinations use write-through move on Windows.
- Failed validation preserves the previous destination byte-for-byte and removes the temporary file.
- `runtime_copy()` applies unsaved edits to a validated `SceneDefinition` without modifying the source scene.
- `migrate_to_current()` explicitly upgrades schema 5 to schema 6 with deterministic, non-zero, scene-unique UUIDs derived from the scene and entity textual IDs.
- Repeating the migration produces the same schema 6 document.

## Verification

- Scene tests cover UUID lookup, rename, unbound position edits, physics-owned movement rejection, unsaved runtime copies, source preservation, atomic round trips, comment preservation, failed-save rollback, temporary cleanup, and deterministic/idempotent schema 5-to-6 migration.
- Debug CTest: 14 of 14 passed.
- Release CTest: 14 of 14 passed.
- Release presentation verification at 30 Hz, 60 Hz, 120 Hz, monitor-matched, and uncapped retained replay hash `7074030210802259671`.
- The packaged Shipping GPU smoke loaded schema 6 with fourteen entities, ten physics bodies, and two animation players; it completed 300 ticks, retained the replay hash, released resources, and shut down cleanly.
- Combined-folder Debug content validation passed using the adjacent runtime manifest and shared Content tree.
- No development debug markers or leftover scene temporary files remain.
- `dist/IC_2DE-Windows-x64.zip` contains ten entries and both `IC_2DE-Debug.exe` and `IC_2DE.exe`. It has 13,006,896 expanded bytes and 5,867,240 archive bytes. SHA-256: `5AB6EFD105A8FB965B363089035CA42C0090151E0C8D8C311456D676D604BDFB`.

## Next checkpoint

Add versioned prefab definitions and instances using persistent UUID identity, then route document mutations through undoable commands. That creates the data and history seams required before a hierarchy/inspector UI is useful.
