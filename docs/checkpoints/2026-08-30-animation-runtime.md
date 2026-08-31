# Deterministic Animation Runtime Checkpoint - August 30, 2026

## Outcome

IC_2DE now has a presentation-only animation module driven by the authoritative fixed simulation tick. Player and enemy locomotion are selected from actual physics displacement and rendered through authored atlas source rectangles without allowing animation to modify World or PhysicsWorld state.

## AnimationPlayer interface

Callers provide validated engine-owned clips, select a clip, advance an integer number of ticks, pause or reset playback, and read one current sample. The module hides:

- clip lookup and transition state;
- positive per-frame tick durations;
- `once`, `loop`, and `ping_pong` traversal;
- large-advance behavior and ping-pong direction;
- restart versus preserve-time clip selection;
- owned event copies emitted when a frame is entered;
- complete construction-time validation.

The interface contains no raylib, texture-library, World, JSON, or Aseprite types. This keeps a future Aseprite adapter responsible only for translating content into `AnimationClip` values.

## Authored locomotion

Scene schema 3 adds `animation_clip`, `animation_frame`, and `animation_binding` records. Every locomotion binding declares idle and move clips for south, north, west, and east plus exactly one initial state. The parser validates textures, clips, frames, events, entity references, physics bindings, and complete state maps before the window opens.

`RuntimeScene` owns one `AnimationPlayer` per animated entity. It derives movement from each body's actual current-minus-previous position, retains the last facing direction when idle, and applies only the sampled texture/source rectangle to the immutable render snapshot. The player has eight directional clips; the enemy uses the same interface with shared placeholder idle/move clips.

## Verification

- Debug and Release compile with project warnings treated as errors.
- Eleven of eleven CTest suites pass.
- Animation tests cover frame timing, event emission, once completion, ping-pong large advances, pause, reset, clip switching, unknown clips, and invalid definitions.
- Scene tests cover schema 3 animation records, the independent player/enemy fixture, and incomplete locomotion-map rejection.
- Development GPU smokes visibly render atlas-selected player frames and release all three scene textures.
- The 300-tick route observes a locomotion frame event in development and shipping builds.
- One Release binary retains replay hash `7074030210802259671` at 30 Hz, 60 Hz, 120 Hz, monitor VSync, and uncapped presentation.
- The shipping package contains exactly five runtime entries, is 565,792 bytes, and has SHA-256 `CE7316F543A661387526125BF4BAEBD87D64240DED4159FA9989509518EDC4BA`.

## Deliberate limit

The current player/enemy atlases are generated checker placeholders proving source-rectangle selection, timing, bindings, and resource lifetime. The next checkpoint is an Aseprite CLI metadata adapter plus a small real atlas; importing content is kept separate from runtime playback by design.
