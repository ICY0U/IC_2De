# Input and Presentation Checkpoint - August 30, 2026

## Outcome

Rendering is uncapped by default while gameplay simulation remains fixed at 60 Hz. The testbed now renders through a 640 x 360 virtual canvas and consumes engine-owned logical input rather than calling raylib keys from gameplay movement code.

## Delivered

- VSync is no longer requested by the application.
- A render cap of zero means unlocked rendering and is now the default.
- `--fps=N` temporarily selects a cap for comparison runs.
- The Input module produces press, hold, and release transitions from logical samples.
- A private raylib adapter maps keyboard and gamepad state into those samples.
- The Presentation module calculates integer upscaling, centering, letterboxing, and safe downscaling.
- P toggles pause, O advances one tick while paused, and R resets the testbed.
- The runtime display reports render mode, measured FPS, fixed rate, and simulation ticks.

## Automated evidence

- Debug: all three CTest tests pass.
- The clock test covers partial ticks and bounded catch-up.
- The input test covers pressed, held, released, normalized axes, and reset history.
- The presentation test covers exact 2x scaling, 1366 x 768 letterboxing, fractional downscaling, and invalid dimensions.
- Runtime smoke runs at 30, 60, 120, and uncapped rendering each completed exactly 120 fixed ticks and exited with code 0.
- The uncapped smoke capture reported approximately 3,100 FPS on the RTX 2080 Ti during this synthetic scene. This is not a game performance benchmark.

## Visual evidence

The ignored `build/runtime-smoke.png` capture confirms the virtual canvas, testbed art, diagnostics, and unlocked-rendering label rendered correctly at 1280 x 720.

## Remaining manual input check

Physical key and gamepad injection was not automated. Before closing this checkpoint completely, verify:

1. A/D and arrows move in both directions.
2. P pauses without accumulating catch-up time.
3. O advances one simulation tick per press while paused.
4. R restores the player and tick counter.
5. Resizing the window keeps crisp integer scaling and centered letterboxing.

## Next checkpoint

Introduce the World module with entity lifetime, deferred creation/destruction, Transform2D, and Sprite data. Keep EnTT private to the module implementation.
