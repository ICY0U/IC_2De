# Aseprite Content Pipeline Checkpoint - August 31, 2026

## Outcome

IC_2DE now consumes Aseprite CLI JSON-array metadata through one adapter and renders an original transparent player atlas in development and shipping content. JSON and Aseprite field details stop at the import boundary; `AnimationPlayer`, `SceneDefinition`, `RuntimeScene`, and rendering continue to consume engine-owned paths, clips, rectangles, tick durations, and events.

## Adapter and schema

`import_aseprite_json(path, fixed_update_hz)` returns only an atlas path and owned `AnimationClip` values. It translates ordered frame rectangles, millisecond durations, inclusive tag ranges, forward/reverse/ping-pong/ping-pong-reverse directions, and the optional `ic2d_events` extension. Duration conversion uses nearest integer ticks with a one-tick minimum.

Scene schema 4 adds:

```text
aseprite=texture_id|relative/metadata.json|sampling|fixed_update_hz
```

The record supplies the file texture and imported clips together. Validation rejects JSON-hash exports, empty/malformed tags, unsafe paths, missing atlases, unsupported trimmed/rotated frames, invalid rectangles, duplicate IDs, and bad timing before a window is created.

## Player asset

- Runtime bitmap: `game/assets/runtime/player-atlas.png`
- Metadata: `game/assets/runtime/player-atlas.json`
- Layout: 1536 x 1024 RGBA, four columns by two rows, 384 x 512 cells
- Bitmap SHA-256: `3A6438BD27CD047A1090DDA063B5AA8391828EDEFDC7D59F64C51E59ED3F7B3A`
- Provenance: generated with OpenAI's built-in image generation mode for this project; the source output remains in the local generated-images store.

The four columns are south, north, west, and east. Idle frames occupy the top row and walk-contact frames the bottom row. Movement clips use the idle/contact pair for their direction; contact frames carry `footstep`.

## Verification

- Debug and Release compile with project warnings treated as errors.
- Twelve of twelve CTest suites pass, including a permanent direction/timing/error fixture and the actual schema-4 runtime scene.
- The generated bitmap was inspected as 32-bit RGBA; unused corner and cell-gap samples have alpha zero.
- The RTX 2080 Ti/OpenGL idle and 300-tick movement smokes load the 1536 x 1024 atlas, visibly render the player, observe `footstep`, pass collision/elevation/contact/trigger/dynamic-prop checks, release all texture resources, and exit zero.
- One Release binary retains replay hash `7074030210802259671` at 30 Hz, 60 Hz, 120 Hz, monitor VSync, and uncapped presentation.
- The installed shipping runtime loads `Content/player-atlas.json` and `Content/player-atlas.png`, completes the same 300-tick route with replay hash `7074030210802259671`, renders the player without development overlays, and exits zero.
- The ZIP contains exactly seven runtime files, is 2,395,706 bytes, and has SHA-256 `8204BC7C17000FB0B2FFE180E91B2DF74182243C5F2A6A43EF5C811EA90F05BD`.

## Deliberate limits

The importer currently requires untrimmed, unrotated JSON-array sheets with at least one named tag. The enemy remains a checker atlas. Aseprite repeat counts, pivots/slices, live asset reload, and editor-driven importing are deferred until their runtime consumers exist.
