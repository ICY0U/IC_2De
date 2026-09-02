# Crosshair, Aim-facing, and Editor Input Fix

## Outcome

Holding a movement key while left-clicking the editor viewport no longer stops the character. Mouse/right-stick aim now drives a visible pixel crosshair and the character's eight-way facing independently from WASD translation.

The control model is twin-stick: movement decides where the physics body travels; aim decides where the character looks. Idle characters turn with the crosshair, and moving characters select their directional walk presentation from aim while retaining unchanged movement velocity.

## Root cause and fix

ImGui marks the viewport image as an active item during left-click. `EditorShell::blocks_gameplay_input()` previously treated every active item as keyboard capture, causing the application to replace the complete input sample with editor-only toggles. Held WASD therefore became zero for the following frame.

The editor now blocks gameplay input only while a text field requests keyboard input. Pointer ownership remains independently constrained to the actual game viewport, so clicking panels cannot fire and clicking the viewport cannot erase movement.

Ordinary left-click is reserved for gameplay fire. Editor selection is now Ctrl+left-click, preventing every shot from also changing or clearing the selected scene entity.

## Crosshair and facing

- Mouse aim places the crosshair at the correctly scaled editor/standalone canvas position and hides the operating-system cursor only while it is over gameplay.
- Controller aim places the crosshair a fixed world distance along the normalized right-stick direction.
- The crosshair is rendered into the internal 640 x 360 canvas as a small gold/cyan pixel shape with a dark outline.
- World aim is converted back into camera-relative facing before eight-way locomotion selection.
- Aim remains independent from movement, including idle turning and movement in a different direction from the crosshair.
- Releasing aim preserves the last usable facing rather than snapping south.

## Verification

- The focused regression test first failed with `A mouse-active editor item must not block held movement keys.`
- After the capture-policy fix, that exact executable passed three consecutive runs.
- New locomotion tests cover idle turning, aim-facing while moving, and retained facing after releasing aim.
- New projection tests cover world-to-camera aim conversion and transform round trips.
- All 20 Debug CTest suites pass, including the new `ic2de.editor_input` suite.
- The Release editor package passed adjacent-content validation and the live GPU texture hot-swap/resource-lifetime probe.
- A packaged `--smoke-crosshair` run completed 75 fixed ticks, moved north while aiming east, captured the crosshair in the docked viewport, and shut down with texture lifetime validation passing.
- The visual capture is `build/editor-package-probe/build/runtime-crosshair-smoke.png`.
- `dist/IC_2DE-Editor-Windows-x64.zip` is 11,497,903 bytes with SHA-256 `50CC374282CEEBC17EB33BD6F2346231D35B959CCCD2FC0F66C2AD66ACB89A9B`.

## Manual check

1. Launch the packaged editor and hold W/A/S/D in the Viewport.
2. While continuing to hold movement, press and hold left mouse. Movement must continue without a pause.
3. Move the pointer around the character. The crosshair and idle/walk facing should follow all eight directions while translation remains controlled by WASD.
4. Move the pointer over an editor panel. The system cursor should return, the game crosshair should disappear, and left-click must not emit Fire.
5. Ctrl+left-click a sprite to select it without changing the normal fire binding.
6. Repeat with a controller: left stick moves, right stick aims, and right trigger fires.

The focused policy test and packaged GPU smoke are automated. Physical simultaneous key/mouse feel remains a manual interaction check.

## Next small step

Continue Loomhold G1 task 4 with failing fixed-tick tests for needle-pistol ammunition, cooldown, reload timing, and a copied projectile-spawn event.
