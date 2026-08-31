# Foundation Checkpoint - August 30, 2026

## Outcome

The project advances from planning-only to a bootable, testable C++20/raylib foundation.

## Evidence

- Visual Studio 18 Community and MSVC 19.51 were detected through `tools/build.ps1`.
- raylib 6.0 was fetched from its pinned official Git tag.
- Debug configured and compiled with project warnings treated as errors.
- Release configured and compiled with project warnings treated as errors.
- `ic2de.core` passed through CTest in Debug and Release.
- The Debug testbed initialized a 1280 x 720 OpenGL 3.3 window on an RTX 2080 Ti.
- A rendered runtime frame was captured to the ignored `build/runtime-smoke.png` artifact.
- The 120-frame smoke run closed itself cleanly with process exit code 0.

## Verified visually

- foundation title and instructions;
- grid and floor rendering;
- placeholder player rendering;
- live FPS display;
- fixed-simulation status text.

## Remaining manual check

Keyboard movement using A/D and the arrow keys was not injected during this checkpoint because the Windows-control helper was unavailable. Launch the normal Debug testbed, move in both directions, and close with Escape before treating input behaviour as play-verified.

```powershell
.\build\windows-debug\ic2de_testbed.exe
```

## Next checkpoint

Add the Input module and virtual 640 x 360 presentation canvas, then verify stable movement at 30, 60, 120, and uncapped render rates.
