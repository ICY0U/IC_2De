IC_2DE Test Builds
==================

Run IC_2DE.exe for the clean shipping runtime.
Run IC_2DE-Debug.exe for the development overlay and diagnostics.
Run IC_2DE-Editor.exe for the development editor with its panels already open.

Controls
--------
W/A/S/D or arrow keys: move
F1: toggle debug visuals   (development builds only)
F2: toggle the editor      (development builds only)
Escape: quit (disabled while the editor shell is open)

The character stays drivable with the editor open. Movement keys go to the
editor only while one of its fields is being typed in or dragged.

All three executables load IC_2DE.runtime and Content/test_area.scene from this
same folder, so they can be double-clicked or moved together without relying on
the source checkout or a special working directory. Scene edits saved from the
editor are written to this folder's Content copy, not to the source checkout. The shipping runtime defaults
to the active monitor refresh rate with VSync. The complete scene is validated
before the graphics window opens.

Automated package check
-----------------------
IC_2DE.exe --shipping-smoke --uncapped
IC_2DE-Debug.exe --validate-content
IC_2DE-Editor.exe --validate-content

The check exercises authored elevation and collision, Box2D contact and sensor
events, a dynamic crate, deterministic locomotion animation and frame events,
resource shutdown, and writes shipping-smoke.png beside the executable before
returning exit 0.
