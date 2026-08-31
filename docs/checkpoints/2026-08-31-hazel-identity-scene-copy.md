# Hazel Identity and Scene-Copy Foundation - August 31, 2026

## Outcome

The expanded non-audio Hazel adaptation has begun with the identity and copy foundation required by save/load, prefabs, hierarchy selection, undo/redo, and isolated play-in-editor state. The reference is TheCherno/Hazel commit `1feb70572fa87fa1c4ba784a2cfeada5b4a500db` (Apache-2.0). No Hazel source was copied.

## Deep module decision

`EntityId` remains a transient handle valid only in one `World`. New `EntityUuid` values are persistent authored identities. EnTT entities and mappings remain private inside the World implementation.

`World::snapshot()` returns deterministic persistent-UUID order and captures UUID, name, transform, and optional sprite state without leaking EnTT types or transient handles. `World::restore()` builds a replacement World first, so zero or duplicate UUIDs fail without damaging live state. Restored entities receive new transient handles and remain findable through their stable UUIDs.

`RuntimeScene::world_snapshot()` exposes that same engine-owned copy seam for later editor play-mode isolation. Development builds visibly report `Scene schema 6 | Stable UUIDs 14` in the runtime overlay; shipping builds keep the overlay compiled out and report the same evidence in their startup log.

## Authored content

Scene schema 6 adds a non-zero, scene-unique 64-bit UUID to every entity record. `SceneDefinition` validates UUIDs before graphics startup, and `RuntimeScene` passes them into World construction. The shipped test scene currently instantiates fourteen entities and confirms fourteen stable UUIDs.

## Hazel system map

`docs/references/HAZEL_ADAPTATION.md` now maps the complete public non-audio Hazel surface to existing IC_2DE modules or ordered adaptation checkpoints. OpenGL backend duplication and global/raw-pointer patterns are adapted differently because raylib remains the platform/render adapter and IC_2DE interfaces remain the test surface.

## Verification

- Debug and Release: fourteen of fourteen CTest suites passed, including relocatable Debug startup validation from the executable directory.
- World tests cover authored/generated UUID lookup, duplicate rejection, deterministic ordering, state preservation, restored transient handles, optional sprite copying, and strong failure behaviour.
- Scene tests cover schema-6 UUID retention plus zero/duplicate rejection before runtime construction.
- Fixed 30 Hz, 60 Hz, 120 Hz, monitor-matched, and unlocked presentation modes retain replay hash `7074030210802259671`.
- The Shipping RTX 2080 Ti/OpenGL smoke loaded schema 6, instantiated fourteen stable UUIDs, completed 300 fixed ticks, retained the replay hash, released resources, and shut down cleanly.
- The latest `dist/IC_2DE-Windows-x64.zip` contains ten entries, including Debug and Shipping executables beside one shared runtime manifest/content tree. It has 13,006,896 expanded bytes and 5,867,240 archive bytes. SHA-256: `5AB6EFD105A8FB965B363089035CA42C0090151E0C8D8C311456D676D604BDFB`.

## Next checkpoint

The mutable scene-document, atomic writer, migration, and runtime-copy checkpoint is complete. Continue with prefab definitions/instances and undoable scene commands. Audio remains deferred.
