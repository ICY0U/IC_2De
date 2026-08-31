# Frame Pipeline and Telemetry Checkpoint - August 31, 2026

## Working

- The application renders the scene into an engine-owned framebuffer, optionally applies a packaged fragment shader into a second target, then presents the active output to either the window or editor viewport.
- `FramePipeline2D` hides raylib targets, shader objects, uniform locations, bottom-up texture orientation, fallback selection, diagnostics, and release order.
- The post effect exposes neutral exposure and saturation plus a subtle vignette. F7 toggles it in development; `--no-post-process` bypasses it at launch in every executable.
- Development telemetry reports a fixed-capacity 240-frame p50/p95/p99 distribution, submission culling, estimated batches and draws, visible vertices, render-target switches, shader passes, and total estimated GPU passes.
- Required shader content is validated with the scene before the window opens. Development staging preserves nested content directories instead of flattening them.

## Measured and verified

- Debug build: 17 of 17 CTest targets pass, including the new frame-telemetry contract and packaged-content startup validation.
- Release build: the same 17 of 17 CTest targets pass with warnings treated as errors.
- Deterministic replay: 30 Hz, 60 Hz, 120 Hz, monitor-synced, and uncapped presentation all finish at hash `7074030210802259671`.
- Real GPU smoke: OpenGL 3.3 on an NVIDIA GeForce RTX 2080 Ti compiled the external shader, created both 640 x 360 framebuffers, captured `build/runtime-smoke.png`, and released the shader, targets, textures, and window cleanly.
- Visual inspection: the captured scene is upright, the sprite/ground ordering is intact, and the vignette is subtle rather than obscuring the play area.
- Editor GPU smoke: the Release editor displayed the processed target upright in its docked viewport and shut down cleanly after 120 fixed ticks.
- Combined-folder validation: Debug and Editor validated the adjacent manifest, scene, and nested shader path; the Shipping runtime completed the full 300-tick GPU smoke with post-processing both active and bypassed.
- Package: `dist/IC_2DE-Windows-x64.zip`, 7,494,042 bytes, SHA-256 `37EBCC28F7DA0A5063E090D1FFE67BEEB829AFF1A8745CCAB7E422110FDEE073`.

## Broken or deferred

- Renderer draw calls and GPU passes are estimates, not hardware timer or pipeline-statistics queries.
- The post-process settings are global. Spatial volume blending waits for multiple authored areas that need different grading.
- There is one external post shader rather than a general shader registry. A second material or lighting consumer should justify that registry.
- Editor camera controls, viewport picking, and 2D lighting remain next.

## Learned

- The old development staging command copied recursive file matches into one directory, silently flattening paths. A shader subdirectory turned this latent packaging problem into a failing startup test; directory-preserving staging is now the invariant.
- Owning both targets in one module removes texture flips and release sequencing from the application loop and gives the editor the already-processed output without learning backend types.
- Frame-time distributions and explicit pass counters are a useful baseline, but they must remain labelled as rolling CPU frame time and estimated GPU work until backend queries exist.

## Next

Add an editor camera that is independent of the runtime follow camera, then use its viewport mapping for UUID-based entity picking. After that seam is proven, add the first 2D light submissions and activate the dormant lights debug channel.
