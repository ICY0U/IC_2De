# Debug Channels and Development Editor Shell Checkpoint

## Outcome

Development builds now have a toggleable debug-visual layer and the first slice of a Hazel-style editor. F1 is a master switch over independent debug channels; F2 opens a docked ImGui shell with a viewport, hierarchy, inspector, command history, statistics, and debug-channel panels. The shipping runtime compiles both out and is unchanged.

The reference remains TheCherno/Hazel commit `1feb70572fa87fa1c4ba784a2cfeada5b4a500db` under Apache-2.0; no Hazel source was copied. Audio remains deferred.

## Debug visuals

`DebugVisuals` is a small policy object with no rendering knowledge: one master switch plus per-channel selection, and channels that no engine module backs report as unavailable.

| Channel | Draws |
|---|---|
| Collision shapes | Solid ground footprints and non-sensor physics bodies |
| Trigger volumes | Trigger areas and sensor bodies |
| Elevation map | Authored +Y elevation shaded low-to-high over the gameplay quad |
| World grid | Projected ground grid and walkable bounds |
| Statistics overlay | The text diagnostics drawn over the canvas |
| Lights | Nothing - listed and disabled until a lighting module exists |

Selection survives the master switch, so F1 restores exactly the channels that were chosen. With visuals off, a development build presents exactly what the shipping runtime presents, which makes "does this look right without dev aids" answerable without a second build.

The `lights` channel is deliberately present and unavailable rather than omitted. A lighting map needs lights, the engine has none yet, and pretending otherwise would hide the gap. It becomes available when the renderer stage lands.

## Editor shell

`EditorShell` hides Dear ImGui behind a pimpl. The application passes a `SceneEditor&`, a `DebugVisuals&`, read-only stats, and the canvas identity, and receives back the few actions only it can perform. No ImGui type crosses the interface.

- **Viewport** renders the existing 640x360 canvas render target into a dockable panel with aspect-preserving fit. When the shell is hidden the canvas is letterboxed to the window exactly as before.
- **Hierarchy** lists authored placements by UUID, marking prefab instances and physics-bound entries.
- **Inspector** edits name and unbound position, destroys prefab instances, and instantiates prefabs. Every mutation goes through `SceneEditor`, so validation, rejection messages, and undo behave identically to any other caller. Rename and drag commit on field deactivation, so one edit is one undo step rather than one per keystroke.
- **History** shows applied command labels with undo and redo.
- **Statistics** reports frame, tick, entity, body, texture, and batch figures, the document's saved state, and save/apply/pause/reset controls.
- **Debug channels** exposes the same switches F1 drives.

### Selection

Clicking a sprite in the viewport selects it. The shell reports the click as a canvas-space point and nothing more; the application owns the camera and the running scene, so it resolves the point and reports the placement back through `select_entity()`. The editor therefore needs no projection, camera, or renderer knowledge, and the picker cannot drift from what is drawn because it rebuilds the renderer's own destination rectangle and walks it in the renderer's layer-then-depth order.

The hit placement is outlined in the viewport, opened in the inspector, and scrolled into view in the hierarchy. Clicking empty ground clears the selection rather than leaving a stale one.

Reading the document parses its text, so the shell caches the entity and prefab views and rebuilds them only when `SceneEditor::revision()` changes. Before that the panels re-parsed the whole scene twice per frame, which was invisible at fourteen entities and would not have stayed that way.

### Input routing

The game keeps running behind the panels, so the character stays drivable with the editor open. Movement keys are withheld only while a panel field is actually collecting them - a text field being typed in, or a widget being dragged or held - which the Statistics panel reports as "Movement keys: game" or "Movement keys: editor field being edited".

Two things had to go for that to work. ImGui keyboard navigation is off, because it makes `io.WantCaptureKeyboard` true for as long as any panel is focused, which silently swallows movement; the shell reports `io.WantTextInput` and `IsAnyItemActive()` instead. Raylib's Escape-to-quit is also disabled while the shell is visible, so cancelling a text edit cannot close the window. Escape quits again as soon as the editor is hidden.

"Apply to running scene" rebuilds `RuntimeScene` from `SceneEditor::runtime_copy()`, so play mode consumes validated unsaved edits while the authored file stays untouched until an explicit save. This is the play/edit separation Hazel gets from its editor scene copy, reached through the seam that already existed.

## Dependency and build policy

Dear ImGui is pinned at `v1.91.9b-docking` and fetched only when `IC2DE_ENABLE_DEVELOPMENT_TOOLS` is on. The platform and renderer backend is engine-owned (`ImGuiRaylibBackend`): input translation, the font atlas, and rlgl draw-list submission are ours, so the editor depends on ImGui alone rather than on a second unpinnable backend repository. `io.IniFilename` is null, so development tools leave no state files beside packaged content.

The shipping preset fetches, compiles, and links none of it.

## Build outputs

The editor is a named build output rather than a hidden mode of the testbed. Both development presets produce it from the same entry point with `IC2DE_DEFAULT_START_WITH_EDITOR=1`, so the testbed and the editor cannot drift apart.

| Output | Where | Bytes | Contents |
|---|---|---|---|
| `ic2de_testbed.exe` | `build/windows-debug`, `build/windows-release` | 10,104,832 / 2,263,552 | Game plus tools; F1 debug visuals, F2 editor |
| `IC_2DE-Editor.exe` | `build/windows-debug`, `build/windows-release`, `dist/windows-x64` | 10,104,832 / 2,263,552 | Same build with the editor open at startup |
| `IC_2DE.exe` | `build/windows-shipping`, `dist/windows-x64` | 1,556,480 | Shipping runtime, all tools compiled out |
| `IC_2DE-Debug.exe` | `dist/windows-x64` | 10,104,832 | The Debug testbed, as before |

The packaged editor comes from the Release preset: development tools stay compiled in, but the tool itself stays responsive. Measured on the same scene, the Release editor ran at 1,629 FPS against the Debug build's 545.

## Verification

- New debug-visual tests cover the master switch, independent channel selection, selection surviving the master switch, unavailable channels never drawing, channel naming, and the elevation ramp including clamping and non-finite input.
- Debug CTest: 15 of 15 passed. Release CTest: 15 of 15 passed.
- Release presentation verification at 30 Hz, 60 Hz, 120 Hz, monitor-matched, and uncapped retained replay hash `7074030210802259671`.
- A 300-tick automated run with the editor open reported the same replay hash, so the shell does not perturb simulation.
- Driving the window with a synthetic click at viewport pixel (793, 310) resolved to canvas point (466.7, 214.8) and selected UUID 1004, the `near-tree` prefab instance, with the hierarchy, inspector, and viewport outline all following.
- Driving the window with a synthetic held key produced identical results with and without the editor: holding D reached camera `-102.546104/-131.836731` in both the editor build and the plain testbed, holding A moved the opposite way, and an editor run with no input stayed exactly at the `-220/-170` spawn focus.
- Captured frames confirm both states: with channels on, the elevation ramp, trigger volume, solid areas, static boundary bodies, grid, and overlay all render; with `--no-debug-visuals`, the canvas shows only shipped content.
- The packaged Shipping GPU smoke loaded schema 7 with fourteen entities, ten physics bodies, and fourteen stable UUIDs, completed 300 ticks, retained the replay hash, and shut down cleanly.
- The shipping build tree contains no ImGui dependency.
- No `imgui.ini` or other tool state files were produced.
- The packaged folder validates content through both `IC_2DE-Debug.exe` and `IC_2DE-Editor.exe` before the archive is written, and the Release editor opens its shell against the packaged scene.
- `dist/IC_2DE-Windows-x64.zip` contains eleven entries, 17,539,854 expanded bytes and 7,449,566 archive bytes. SHA-256: `4E672091266A4B39B4E105A0BFCC0835EB510F0418957CB0CEC80CAD24B4339C`.

## Known gaps

- There is no gizmo: a selected placement is moved through the inspector's numeric fields, not by dragging it in the viewport.
- The hierarchy lists authored document records. Picking resolves against the live World, but the list itself does not show runtime-only state.
- Picking and the selection outline each copy a World snapshot on the frames they run. That is bounded by the scene's entity count and only happens in the editor path, but it is not free.
- No content browser, console log panel, or profiler yet, and no isolated play/stop mode beyond "Apply to running scene".
- Multi-viewport (`ImGuiConfigFlags_ViewportsEnable`) is off; panels stay inside the main window.

## Next checkpoint

Either continue the adaptation order with typed engine events and an owned scene/layer stack, or close the editor gaps above - viewport picking, live-World inspection, and a console panel - depending on which unblocks content work sooner.
