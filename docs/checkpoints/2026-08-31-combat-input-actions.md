# Combat Input Actions Checkpoint

## Outcome

The editor build now exposes device-independent inputs for continuous aim and six gameplay actions: fire, reload, dodge, interact, swap weapon, and choose extraction. No combat behavior consumes them yet. This checkpoint isolates device mapping and transition correctness before the Combat module owns simulation consequences.

The Input module presents one compact `GameplayAction` interface. Callers and tests iterate the same stable action list and request `ButtonState` values without knowing whether an action came from a key, mouse button, wheel, gamepad button, or trigger. The raylib adapter retains that device knowledge.

## Default mappings

| Logical input | Keyboard and mouse | Xbox-compatible controller |
|---|---|---|
| Move | W/A/S/D or arrows | Left stick or D-pad |
| Aim | Mouse pointer | Right stick |
| Fire | Left mouse | Right trigger |
| Reload | R | X |
| Dodge | Space | A |
| Interact | E | B |
| Swap weapon | Q or mouse wheel | Y |
| Choose extraction | X | Left bumper |
| Development reset | F5 or editor Statistics button | - |

F5 replaces the previous R reset binding so reload never collides with a development command.

## Input contract

- Every gameplay action reports independent `down`, `pressed`, and `released` state.
- Holding a key or button emits one pressed edge rather than repeating every render frame.
- Resetting the tracker forgets both development and gameplay history.
- Movement and right-stick aim are bounded to unit length while preserving their direction.
- Pointer coordinates remain in screen space until the next Combat/application seam projects them through the runtime or editor viewport into the X/Z world.
- The raylib adapter remembers whether mouse or right-stick aim was used most recently, preventing controller stick noise from stealing mouse aim.
- The editor Statistics panel shows the active aim source, aim values, pointer position, every action state, and the default bindings.

## Verification

- The first compile failed because the tested gameplay-action interface did not exist yet.
- The first implementation run exposed an axis bug: component clamping changed a 3:4 direction into a diagonal. The shared helper now bounds vector magnitude without changing direction.
- All 18 Debug CTest suites pass after the correction.
- `ic2de.input` covers every action independently through press, hold, release, and reset, plus aim normalization and pointer preservation.
- The Debug editor-only target compiles with project warnings treated as errors.
- The Release editor-only package validated adjacent content and completed the real GPU texture hot-swap/resource-lifetime probe.
- `dist/IC_2DE-Editor-Windows-x64.zip` is 11,493,847 bytes with SHA-256 `F0E269D7B97D2A1C49106EB5E496B97BAD28D9206B59C92128CA2BADF77B3872`.

## Manual test

Open the editor, select the Statistics tab, then exercise each mapping. `PRESSED` should appear for one frame, `DOWN` while held, and `ready` after release. Moving the mouse should select `MOUSE`; moving the right stick beyond its dead zone should select `RIGHT STICK`.

This telemetry confirms mapping and transitions only. Firing, reloading, dodging, interacting, swapping, and extraction intentionally have no gameplay effect in this checkpoint.

## Next small step

Define `CombatCommand`, `CombatEvent`, `CombatSnapshot`, and the minimal deterministic Combat interface. The command adapter will be the first consumer of these actions and will own mouse-to-world aim projection and fixed-tick action buffering.
