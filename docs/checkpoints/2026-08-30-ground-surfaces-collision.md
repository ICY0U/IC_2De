# Ground Surfaces and Collision Checkpoint - August 30, 2026

## Outcome

The testbed now has rendered ground geometry and a deterministic gameplay collision layer. The previous screen-aligned shader boxes have been removed: a grid-free GPU backdrop sits behind one bounded grid projected onto the X/Z World plane.

## Ground rendering

- `GroundQuadSubmission2D` is immutable frame data, separate from camera-facing sprites.
- Ground quads retain authored submission order and always render before billboards.
- The raylib adapter owns quad winding conversion, visibility checks, and triangle submission.
- Diagnostics report submitted, visible, and culled ground surfaces.
- The test area submits a base floor, raised floor, solid-footprint overlays, and a trigger overlay.

## GroundMap collision

`GroundMap` provides one engine-owned movement interface over authored data:

- X/Z walkable bounds account for the actor's half extents;
- solid footprints resolve movement one axis at a time, allowing wall sliding;
- elevation areas sample a discrete floor height into World Y;
- `max_step_height` rejects elevation changes that are too tall;
- trigger footprints return engine-owned numeric tags without blocking motion;
- no Box2D or raylib type crosses the module interface.

The first elevation rule is deliberately discrete and centre-sampled. Continuous ramps and multi-level overlap remain deferred until the test area requires them.

## Runtime demonstration

The 300-tick automated route now:

1. moves camera-relative across X/Z;
2. climbs from Y=0 to the Y=24 raised area;
3. descends to the base floor;
4. collides with and slides around a solid wall;
5. enters trigger tag `1`.

The application returns an error if an automated movement run misses collision, elevation, or trigger state. It also stops on the exact requested fixed tick, including when one rendered frame contains multiple simulation ticks.

## Verification

- Debug: seven of seven CTest suites passed with project warnings treated as errors.
- Release: seven of seven CTest suites passed with project warnings treated as errors.
- GroundMap tests cover free movement, world bounds, axis-separated sliding, permitted elevation, rejected tall elevation, triggers, and invalid definitions.
- Render2D tests cover immutable ground submission order and submission lifecycle.
- 30 Hz, 120 Hz, 240 Hz VSync, and uncapped runs completed exactly 300 fixed ticks.
- Every tested pacing mode ended at camera X=46.088829, Z=396.605316.
- Every runtime mode reported successful collision, elevation, and trigger validation.
- GPU, texture, render-target, and window resources shut down cleanly.

## Remaining manual checks

1. Feel-test wall sliding and stepping with keyboard and gamepad input.
2. Walk along every world edge and obstacle corner.
3. Toggle the GPU backdrop and confirm the CPU fallback has no duplicate grid.
4. Resize the window while standing on the raised region and trigger.

## Next checkpoint

Add a narrow Box2D adapter that maps engine X/Z onto Box2D XY, produces engine-owned contacts and sensor events, and debug-draws its footprints through Render2D. Keep GroundMap elevation authoritative until continuous ramps have a concrete gameplay requirement.
