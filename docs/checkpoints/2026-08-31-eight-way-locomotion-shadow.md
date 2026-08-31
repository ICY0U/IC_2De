# Eight-Way Locomotion and Soft Shadow - August 31, 2026

## Outcome

IC_2DE now selects south, southwest, west, northwest, north, northeast, east, or southeast presentation states from actual physics displacement. Each facing has an idle and move state, while the simulation continues accepting normalized analog X/Z input rather than snapping authoritative movement.

The hard rectangle beneath the player has also been replaced with a soft oval shadow that travels through the ordinary texture-asset and sprite-rendering paths.

## Shadow diagnosis and repair

The focused regression initially failed with:

```text
FAIL: The player shadow must use the soft radial texture instead of an untextured quad.
```

The player atlas was confirmed transparent beneath the feet. `player-shadow`, however, had `texture-id=-`; the renderer intentionally draws untextured sprites with `DrawRectanglePro`, making the authored 20 x 7 sprite appear as a hard block.

Scene schema 5 can now declare a generated radial texture with inner/outer RGBA colors. `player-shadow` uses one 16 x 16 radial-alpha texture stretched to 20 x 7, producing a soft ellipse without a renderer-specific shadow branch or another bitmap dependency. The original scene-level regression now passes.

## Eight-way module

`locomotion_facing(Vec2)` owns eight equal 45-degree sectors. Cardinal sectors extend 22.5 degrees to either side of their axis; other vectors select a diagonal. The same module converts between each facing's idle and move state. Zero/non-finite vectors fail safely to south, while runtime retains the prior facing whenever actual displacement stops.

Scene schema 5 requires all sixteen idle/move records. The player uses independent clips for all eight directions; the placeholder enemy satisfies the same interface by sharing its existing idle/move clips.

## Diagonal player asset

- Runtime bitmap: `game/assets/runtime/player-diagonal-atlas.png`
- Metadata: `game/assets/runtime/player-diagonal-atlas.json`
- Layout: 1536 x 1024 RGBA, four columns by two rows, 384 x 512 cells
- Directions: southwest, northwest, northeast, southeast
- Rows: idle, walk-contact
- Bitmap SHA-256: `05CC379A0ACAE48CAF0C32DC83705E31EC9404953D1B334EA7F9D5A762874C47`
- Provenance: generated in built-in image-edit mode from the original cardinal atlas, then corrected in built-in background-extraction mode. The rejected intermediate RGB checkerboard output is not referenced by the project.

Alpha inspection confirmed a 32-bit ARGB output and alpha zero at corners and cell gaps. Movement clips pair each diagonal idle/contact frame and retain `footstep` on contact.

## Verification

- Thirteen of thirteen Debug CTest suites pass, including dedicated eight-sector thresholds, state conversion, schema completeness, diagonal import, and the soft-shadow regression.
- The RTX 2080 Ti/OpenGL 300-tick route selects a diagonal player clip, observes `footstep`, passes collision/elevation/contact/trigger/dynamic-prop checks, releases all textures, and retains replay hash `7074030210802259671`.
- Thirteen of thirteen Release CTest suites pass. Fixed 30 Hz, 60 Hz, 120 Hz, monitor-matched, and unlocked presentation modes all produce replay hash `7074030210802259671`.
- The final Shipping executable loaded both atlases and the generated radial texture on the RTX 2080 Ti/OpenGL path, captured `shipping-smoke.png`, completed 300 fixed ticks, repeated hash `7074030210802259671`, and shut down cleanly.
- `dist/IC_2DE-Windows-x64.zip` contains nine entries with 5,140,808 expanded bytes and 4,188,687 archive bytes. Archive SHA-256: `67B74933BB80AC6737AB8A0B8CE16509E5EE82447E51276837F3E464C85A2542`.
