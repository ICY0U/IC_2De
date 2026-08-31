# Pokémon-Style 2.5D World Checkpoint - August 30, 2026

## Outcome

The testbed now inhabits true X/Y/Z world space without abandoning its 2D presentation. X/Z form the walkable ground plane, +Y represents elevation, and an engine-owned orthographic projector turns World positions into camera-facing Render2D billboards.

## Architecture

- `WorldTransform` stores a `Vec3` position, heading, and 3D scale without depending on raylib.
- `Camera25DState` owns focus, yaw, pitch, world-to-pixel scale, and zoom.
- `project_world_point()` is a pure, tested World-to-presentation seam.
- `SpriteSubmission2D::sort_depth` makes ordering explicit; it no longer infers gameplay depth from screen Y.
- `Sprite2D::normalized_origin` supports feet-anchored characters and centred ground shadows.
- Camera-relative input is converted back to an X/Z direction, so an oblique camera does not make W/A/S/D feel rotated.

The World remains unaware of projection and raylib. The raylib renderer still consumes only immutable 2D frame descriptions.

## Runtime demonstration

- W/A/S/D, arrow keys, and the left gamepad stick move over both ground axes.
- Diagonal input is normalized to prevent a speed increase.
- The camera follows X/Z with a fixed -18 degree yaw and 50 degree pitch.
- A projected world grid makes the ground-plane orientation visible.
- Billboards overlap by projected depth and stay anchored at their feet.
- Ground shadows and a raised checker crate demonstrate that Y elevation is independent from ground depth.
- The deterministic smoke moves camera-right and camera-forward for 300 fixed ticks.

## Verification

- Debug: six of six CTest suites passed with project warnings treated as errors.
- Release: six of six CTest suites passed with project warnings treated as errors.
- Unit coverage checks focus projection, X/Z/Y direction, camera yaw, camera-relative movement conversion, invalid camera rejection, finite depth, and explicit depth ordering.
- Monitor-synced runtime detected 240 Hz and completed 300 fixed ticks.
- Capped 30 Hz, capped 120 Hz, monitor-synced 240 Hz, and uncapped runs all ended at the identical camera focus X=68.699585, Z=396.605316 after 300 ticks.
- The final camera focus was approximately X=68.70, Z=396.61.
- The generated texture and GPU shader were released before the graphics context closed.
- Runtime exited with code 0 and captured `build/runtime-camera-smoke.png`.

## Manual checks still required

1. Feel-test W/A/S/D and gamepad movement with the oblique camera.
2. Walk behind and in front of every prop and confirm overlap reads naturally.
3. Resize the window while moving through X/Z.
4. Exercise pause, single-step, reset, render pacing, and the GPU-grid toggle interactively.

## Next checkpoint

Add ground-aligned tile/quad rendering and an engine-owned collision-footprint query. Use a small authored area with a wall, a trigger, and one discrete elevation change to settle the collision/elevation rules before introducing Box2D.
