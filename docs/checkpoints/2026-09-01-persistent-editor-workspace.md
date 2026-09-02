# Persistent Editor Workspace Checkpoint

## Outcome

The development editor is no longer a cluttered first-run wall of panels that
forgets its arrangement. Two changes land together:

- a cleaned default workspace that gives most of the window to the viewport and
  moves dense telemetry behind tabs and collapsing sections;
- per-user dock-layout persistence, so a workspace the owner arranges by hand is
  restored on the next launch instead of being rebuilt.

Both are development-only. The shipping runtime still compiles the editor, the
debug overlay, and the live toggles out.

## Cleaned default workspace

`EditorShell::Impl::build_layout` runs only on a first run or after an explicit
reset. It splits the dockspace once and docks each panel:

| Region | Split | Panels |
|---|---|---|
| Left | 0.18 | Hierarchy |
| Right | 0.24 | Inspector |
| Bottom | 0.23 | History, Debug channels, Statistics (tabbed) |
| Centre | remainder | Viewport |

The viewport keeps the central node, so the game is the largest thing on screen
rather than one panel among equals. Three telemetry panels share one bottom dock
as tabs instead of stacking into three competing columns.

Inside the panels, the same principle applies to content:

- **Statistics** is a tab bar - Overview, Navigation, Gameplay, Scene - and the
  Gameplay tab is further divided into `Input`, `Combat & dodge`, `Projectiles`,
  `Enemy intent`, and `Health & events` collapsing headers. Only Input and
  Combat & dodge open by default; the rest stay closed until asked for.
- **Hierarchy** carries an `ImGuiTextFilter`, so a scene is searched rather than
  scrolled.
- **Debug channels** groups its switches under `World`, `Navigation`,
  `Presentation`, and `Reserved` separators, and offers `Clean viewport` (enable
  visuals, drop every channel except the stats overlay) and `Hide all`. The
  `lights` channel remains listed, disabled, and explains why on hover.

The former viewport wall of statistics text is gone; a compact HUD remains and
the detail lives in Statistics.

## Persistence contract

`engine/src/editor/editor_layout.{hpp,cpp}` owns path policy and file predicates
and knows nothing about Dear ImGui. The shell owns the decision; ImGui remains
the owner of the ini bytes and its normal dirty/save cadence.

- An empty override resolves to `%LOCALAPPDATA%\IC_2DE\Editor\layout-v1.ini`,
  falling back to `%APPDATA%` and then the temp directory. It never resolves
  into the content directory, so a workspace preference cannot be mistaken for
  game data or land in a package.
- `--editor-layout=PATH` supplies an isolated override for automated probes and
  portable runs, leaving the personal workspace untouched.
- A file counts as usable only if it contains both `[Docking][Data]` and
  `DockSpace`. A window-only ini is treated as first-run state, so a partially
  written or pre-docking file yields the complete default workspace rather than
  a half-built one.
- `prepare_layout_path` creates only the parent directory.
- `Workspace > Save layout now` forces an immediate save;
  `Workspace > Reset to default layout` removes exactly the configured file and
  rebuilds the default arrangement in place, without a restart.
- The shell logs `Built default editor layout: PATH` or
  `Restored editor layout: PATH` exactly once per launch. That line is the
  observable difference between the two paths and is what the probes assert.

The source Debug build, the source Release build, and the packaged editor share
one file, so an arrangement follows the owner between them.

## Verification evidence

- `ic2de.editor_input` covers focus policy plus the workspace predicates:
  override resolution, parent-directory creation, a window-only ini rejected as
  first-run, a dock tree accepted, and reset removing exactly the configured
  file.
- Debug build: 26/26 CTest suites pass.
- `tools/verify-editor-layout.ps1` is the reusable live check against a real
  GPU. Launch 1 must log `Built default editor layout` and must not log
  `Restored editor layout`; the probe then hand-widens one saved dock node by
  190 px; launch 2 must log `Restored editor layout`, must not log
  `Built default editor layout`, and must still carry the arranged width;
  launch 3 replaces the file with a window-only ini and must return to
  `Built default editor layout`.
- Observed on the NVIDIA GeForce RTX 2080 Ti at OpenGL 3.3, in both
  `windows-debug` and `windows-release`: launch 1 built the default workspace
  and saved a 1,248-byte ini; the arranged width went `230 -> 420`; launch 2
  restored and kept 420; launch 3 rejected the window-only ini and rebuilt. A
  hand-widened bottom dock (`166 -> 300`) survived the same way.
- `tools/package-editor.ps1 -RunEditorLayoutProbe` runs the same two-launch
  assertion against the packaged editor and copies the second-launch capture to
  `build/runtime-editor-layout-packaged-smoke.png`. The Release editor package
  is 11,569,388 bytes with SHA-256
  `882C9311E5EB8916FDBB9E37AF545E819B5CFCB4F025DA14459EDE60D296AF09`.

The width assertion is the part that matters. Both launches produce a usable
dock tree at the same window size, so checking only that an ini exists and
contains `DockSpace` cannot tell restoration from a rebuild that happens to
recompute the same arrangement. A rebuild recomputes every split from the
hard-coded default ratios, so a hand-arranged width is the one value only a real
restoration can carry forward.

## Broken or deferred

- Layout persistence is a single file with no versioned migration beyond the
  `-v1` name. A future incompatible arrangement gets a new file name rather than
  an upgrade path.
- Multi-viewport (`ImGuiConfigFlags_ViewportsEnable`) remains off, so panels
  cannot be dragged outside the main window and no out-of-window position is
  persisted.
- No named workspace presets or per-task layouts; there is one arrangement and
  one reset.
- Panel visibility itself is not persisted separately from docking - a closed
  panel is remembered by ImGui's own window state, not by an engine-owned
  preference.
- Still absent from the shell: transform gizmos, an editor camera, a content
  browser, a console log panel, and an isolated play/stop mode.

## Learned

- Splitting path policy out of the shell is what made the workspace testable.
  `resolve_layout_path`, `layout_file_is_usable`, `prepare_layout_path`, and
  `remove_layout_file` are all exercised in `ic2de.editor_input` without
  creating a GPU window.
- Treating a window-only ini as first-run state is the difference between a
  clean default workspace and a subtly broken one. ImGui will happily restore
  window positions with no dock tree, which looks like a layout and is not.
- A layout path containing a space must be quoted when it is passed through
  `Start-Process -ArgumentList`. An unquoted value is split at the space and the
  ini is written to a truncated path outside the probe directory. Both probes
  now quote it and say why.
- `Process.WaitForExit(timeout)` returns before the exit code is published;
  the parameterless `WaitForExit()` is what makes `ExitCode` readable.

## Next small step

Close the largest remaining editor gap: a viewport transform gizmo that drags a
selected placement through `SceneEditor` commands, so a drag is one undo step
and validation, rejection, and atomic saving behave exactly as they do for the
inspector's numeric fields. An editor camera decoupled from the gameplay camera
is the natural follow-on, but the gizmo is worth proving against the existing
Ctrl+left-click selection seam first.
