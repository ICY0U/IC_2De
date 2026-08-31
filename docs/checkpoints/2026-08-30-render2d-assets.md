# Render2D and Asset-Lifetime Checkpoint - August 30, 2026

## Outcome

World entities no longer call raylib drawing functions directly. The engine builds an immutable, sorted frame description; a private raylib adapter applies camera transforms, culls invisible sprites, resolves texture handles, submits drawing, and returns diagnostics.

## Render2D frame interface

- `RenderQueue2D::begin()` starts one frame with an engine-owned camera description.
- `submit()` accepts engine-owned sprite data with no raylib types.
- `finish()` sorts by layer, bottom Y, and stable ID.
- A finished `RenderFrame2D` exposes read-only spans and remains independent when the queue is reused.
- Invalid submission order and invalid camera zoom fail immediately.

## Camera and culling

- The camera follows the player through a 1,600-unit-wide test World.
- World-space sprites are transformed through the private raylib renderer.
- Conservative axis-aligned culling is applied when the camera is not rotated.
- A deterministic 300-tick movement smoke moved the camera from X=320 to approximately X=726.
- The moving capture confirms that nearby sprites entered the view while distant sprites remained culled.

## Diagnostics

The runtime overlay now reports:

- visible and submitted sprites;
- culled sprites;
- estimated material batches;
- texture switches;
- camera X position;
- loaded texture count.

At the initial camera position, the representative scene reported 3 visible of 5 submitted sprites, 2 culled sprites, 3 estimated batches, and 2 material switches.

## Texture assets

- `TextureHandle` contains a slot index and generation so released handles become stale.
- Duplicate asset keys share one GPU texture and increment its reference count.
- The final release unloads the GPU texture, advances the generation, and returns the slot for reuse.
- Invalid and stale texture handles resolve to a pinned checkerboard fallback during rendering.
- Pixel and bilinear sampling modes are available.
- GPU resources are explicitly released before the window and OpenGL context close.

The current checker texture is procedurally generated through the asset module. The attempted PPM fixture was rejected by this raylib image configuration, so real external artwork should begin with a validated PNG fixture rather than assuming every stb_image format is enabled.

## Verification

- Debug: six of six CTest tests passed.
- Release: six of six CTest tests passed.
- Monitor-synced runtime detected 240 Hz and completed 300 fixed ticks.
- Camera ended at X=725.917847.
- The texture cache returned one handle for two acquisitions with reference count two.
- The duplicate release left the asset alive; the final release invalidated it and reduced the loaded count to zero.
- The generated 8 x 8 texture and GPU grid shader were unloaded before context shutdown.
- Runtime exited with code 0.

## Remaining manual checks

1. Move across the full World and assess camera responsiveness.
2. Resize the window while the camera is moving.
3. Toggle G and confirm the renderer remains correct with the CPU grid fallback.
4. Exercise pause, single-step, reset, and render-pacing changes while away from the World origin.

## Next checkpoint

Finish the rendering milestone with a representative sprite stress scene, measured frame-time percentiles, texture hot-reload, and a RenderDoc batching capture. After that gate, begin the Box2D adapter with explicit pixels-per-metre policy and engine-owned contact events.
