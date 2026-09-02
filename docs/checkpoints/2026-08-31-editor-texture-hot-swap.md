# Editor Texture Hot-Swap Checkpoint

## Outcome

The development editor now reloads changed file-backed bitmap textures while it is running. Gameplay, renderer, scene, and animation code keep the same generational `TextureHandle`; the asset owner replaces the underlying GPU texture only after the replacement loads successfully.

This is deliberately an editor production tool. `IC_2DE-Editor.exe` enables polling at startup, `--editor` enables it explicitly in a development build, and the Shipping entry point remains unchanged.

## Runtime contract

- Only loaded file-backed bitmap textures are watched. Generated checker, radial, and other memory-authored textures are ignored.
- File size and last-write time must remain identical across two polls before a reload is attempted, reducing partial-write races.
- A valid replacement inherits the slot's sampling policy, increments its revision, swaps behind the stable handle, then releases the old GPU texture.
- A missing, incomplete, or invalid replacement is rejected. The current texture, dimensions, revision, handle generation, and references remain valid.
- The editor Statistics panel reports whether hot swap is active and shows watched, successful, and rejected counts.
- Closing the editor shell stops polling; showing it resumes polling.

## Production workflow

An editor launched from the repository root resolves the authored scene and PNG files in `game/assets/runtime`. A double-clicked build or packaged editor resolves its adjacent `IC_2DE.runtime` and `Content` directory, so it modifies and watches that isolated content copy.

Build only the editor:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1 -Configuration Release -EditorOnly
```

Package only the editor and its content:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\package-editor.ps1 -Configuration Release -RunHotSwapProbe
```

## Verification evidence

- The focused asset regression replaced a 1122 x 1402 player atlas with a 1536 x 1024 tree atlas behind one handle, observed revision 2, then wrote invalid bytes and proved the last valid texture remained active.
- All 18 Debug CTest suites passed, including `ic2de.assets` and packaged-content startup.
- The packaged Release editor opened a real OpenGL 3.3 window on the NVIDIA GeForce RTX 2080 Ti, loaded its adjacent Content tree, replaced the loaded tree texture at revision 2, rendered the new pixels into the editor viewport, captured the result, released every texture/framebuffer/shader, and shut down cleanly.
- The probe restored the staged tree atlas to SHA-256 `13C7B65EA5588B61A01764E644D51C07E081312800EAB51E5730C28071518220`.
- The editor-only package validated its adjacent schema-8 content without opening a window.
- `dist/IC_2DE-Editor-Windows-x64.zip` is 11,492,112 bytes with SHA-256 `55D0373B30B5E0DF2C90825DF59096D4CE99AD376A97EF2CC4F2BBE988C0617C`.

The reusable live check is `tools/verify-editor-hotswap.ps1`. It backs up the staged tree atlas, starts the editor hidden, writes a visibly different valid atlas, waits for a successful reload and capture, then restores and hashes the original even when the probe fails.

## Deferred by design

- Aseprite JSON/tag/frame metadata reload.
- Scene-document reload and conflict handling.
- Post-process shader reload.
- Native C++ DLL/module hot reload.
- Directory watchers and background decode/upload queues.

Polling five current textures every 200 ms is intentionally simpler than a platform watcher. Profiling must demonstrate a problem before the system becomes more complex.

## Next small step

Begin Loomhold G1 with data-independent logical actions for aim, fire, reload, dodge, interact, weapon swap, and extraction choice. Stop after input mapping and tests so keyboard/gamepad behavior can be verified before the Combat module is introduced.
