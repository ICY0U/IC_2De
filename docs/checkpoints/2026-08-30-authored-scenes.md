# Authored Scenes Checkpoint - August 30, 2026

## Outcome

The development and shipping runtimes now build the playable room from versioned content instead of reconstructing player, prop, ground, and collision data inside `application.cpp`. Scene loading is divided into a pure validation boundary and one runtime owner.

## SceneDefinition

- Strict schema 2 covers X/Y/Z policy, ground bounds and areas, camera settings, physics policy, collision filters, textures, physics boxes, and sprite entities.
- Parser failures report the scene path and source line where available.
- Unknown or duplicate settings, invalid numbers and booleans, duplicate IDs, broken references, unsafe texture paths, missing files, invalid GroundMap/PhysicsWorld definitions, and unsupported schemas fail before window creation.
- Exactly one kinematic player and one dynamic primary prop are required; player, primary prop, enemy, and generic roles remain engine-owned.
- Bound sprite/shadow entities must agree with their body's initial X/Z center.

## RuntimeScene

`RuntimeScene` consumes a validated definition and owns GroundMap, PhysicsWorld, World, scene texture handles, role bindings, reset state, event state, fixed-tick synchronization, and interpolated render snapshots. The application no longer knows player, crate, shadow, or enemy entity/body identifiers.

The packaged `test_area.scene` now authors 14 entities and three explicit gameplay bodies, including an enemy. Generated solid, trigger, and boundary bodies bring the live physics total to 10. `tests/fixtures/scenes/compact_encounter.scene` supplies an independent layout and enemy role for reusable-format coverage.

## Verification

- Debug and Release compile with project warnings treated as errors.
- Ten of ten CTest suites pass in both configurations.
- Scene tests cover a complete in-memory document, the permanent independent fixture, unsafe paths, unknown bindings, duplicate IDs, and unsupported schemas.
- A real RTX 2080 Ti/OpenGL development smoke loaded schema 2, rendered the authored room, and released all scene textures before graphics shutdown.
- The 300-tick route still observes collision, elevation, a Box2D contact, trigger entry, and dynamic-prop movement.
- One Release binary produced replay hash `7074030210802259671` at 30 Hz, 60 Hz, 120 Hz, monitor VSync, and uncapped presentation.
- The installed shipping runtime loaded `Content/test_area.scene`, completed the GPU smoke, excluded development overlays, and exited zero.
- `dist/IC_2DE-Windows-x64.zip` contains the five declared runtime entries, is 544,951 bytes, and has SHA-256 `CD3A6A54F22DABB20AC37D21BC0DBBD0A13C01BB1D59F0E32B5F651D434828B3`.

## Next checkpoint

Build engine-owned animation clips and a deterministic fixed-tick animation player, then bind player idle/four-direction movement and enemy animation through authored scene data before adding the Aseprite adapter.
