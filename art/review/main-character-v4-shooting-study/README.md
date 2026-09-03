# Main Character V4 Shooting Study

Review-only art. Nothing in this folder is referenced by a runtime scene,
animation binding, importer, or player state.

## Purpose

- Replace the cardinal side-view fallback with authored northeast, northwest,
  southeast, and southwest shooting poses.
- Rework north shooting without shrinking the character. Body scale is locked
  to the north standing turntable; the foreshortened weapon occupies its own
  extension above the shoulder line.
- Preserve the six-frame shooting grammar: immediate shot, maximum recoil,
  recoil hold, weapon return, body settle, exact aim-idle recovery. Proposed
  timing is `1,1,1,2,2,2` fixed ticks at 60 Hz.

## Files

The folder root contains the generated review sources. `clean/` contains the
successful genuine-alpha extraction pass. Southeast is still present as a
review source, but its two automated alpha-extraction attempts stayed opaque;
those rejected derivatives are isolated under `failed-alpha/` and must not be
treated as production art.

These remain high-resolution generated motion references. Approval should be
followed by a native-grid cleanup pass with uniform action cells, a fixed
bottom-centre root, a body-height measurement that excludes the weapon, and
separate muzzle-flash/effect sprites.
