# Relocatable Debug Startup and Combined Package - August 31, 2026

## Outcome

The Debug executable can now be double-clicked from its build directory or from the packaged folder. The packaged folder contains both `IC_2DE-Debug.exe` and `IC_2DE.exe`, sharing one `IC_2DE.runtime` manifest and one `Content` tree.

## Diagnosis

The executable-folder repro failed immediately with exit code 2:

```text
Runtime content load failed: ...\build\windows-debug\game\assets\runtime\test_area.scene: File could not be opened.
```

The confirmed cause was working-directory-dependent Debug startup. The Debug entry point used the source-relative `game/assets/runtime/test_area.scene`; double-clicking made the executable directory the working directory. Visual Studio's configured source-root working directory hid the problem during IDE launches. This was not a graphics crash.

## Repair

- Debug builds stage `IC_2DE.runtime` and runtime assets beside the testbed whenever content changes.
- The Debug entry point continues using live source content when launched from the repository root, but falls back to the adjacent manifest when the source-relative scene is unavailable.
- `--validate-content` exercises startup content resolution without creating a window.
- CTest runs `ic2de.testbed_startup` from the executable directory, reproducing the double-click path.
- Packaging now builds/tests Debug and Release, installs Shipping, copies Debug as `IC_2DE-Debug.exe`, and validates Debug from the combined folder before creating the ZIP.

## Verification

- The new executable-level regression failed before the repair and passes afterward.
- Debug and Release each pass fourteen of fourteen tests.
- A direct process check launched Debug from `build/windows-debug`, confirmed it remained open after three seconds, and then closed the test process.
- The same process check passed for `dist/windows-x64/IC_2DE-Debug.exe` using the combined folder as its working directory.
- Shipping GPU smoke completed 300 fixed ticks, retained replay hash `7074030210802259671`, released resources, and exited zero.
- The ZIP contains ten entries and both executables. SHA-256: `B562EB6FAB4B7872836126433BA62756ED3D9F9388DDD5D76C9F42A67D922B41`.
