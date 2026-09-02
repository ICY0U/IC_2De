# Application Loop Decomposition Checkpoint

## Outcome

`run_application()` had grown to 571 lines in a single function. The Application module's stated interface is "run, request quit, change scene" with "window loop, timestep, lifecycle, error handling" hidden inside, but the body had also accumulated the automated-run verdict, camera-follow policy, observation latching, debug submission, and editor wiring. Every system added since Milestone 2 was appended to the same function.

This checkpoint pulls out the parts that were doing their own job, and makes the one piece of pure policy testable for the first time. No behavior changed: the five-mode replay hash is unchanged.

## Automated-run verdict is now a tested contract

The ~90 lines of shutdown checks that decide a run's exit code moved to `ic2d::evaluate_automated_run()` in `IC2DE::Core`, with `AutomatedRunObservations` in and `AutomatedRunExit` out. It depends on nothing but the log, so it is unit-testable without a window or a GPU - which is why it had never been tested despite packaging, CTest, and the replay scripts all branching on its exit codes.

| Exit | Meaning |
|---|---|
| 0 | Every enabled check passed |
| 6 | Texture lifetime failed at shutdown |
| 7 | Automated run never collided or never climbed |
| 8 | Smoke capture could not be written |
| 9 | Automated run missed a contact, trigger, or prop movement |
| 10 | Automated run saw no stable-identity frame event |
| 11 | Automated run saw no diagonal locomotion clip |

The tests pin those numbers, the evaluation order, and the rule that gameplay checks are skipped when nothing drove the character while resource checks still apply.

## Loop phases

- `SceneObservations` groups the five loose flags the loop was latching by hand. Which facts persist for a run and which describe only the current tick is now decided in one `observe()` method instead of five inline expressions, and `forget()` replaces a hand-written reset that had already drifted once.
- `advance_fixed_step()` owns one authoritative tick: movement, layer dispatch, layer update, then camera easing. The camera-follow rate is a named constant instead of a bare `8.0F` in the middle of the loop.
- `submit_world_ground()`, `submit_scene_sprites()`, and `submit_debug_ground()` split frame submission by ownership. Development channels now use their own ground-id range, so gameplay submission no longer threads an id counter through `#if`-guarded debug code.

`run_application()` is 454 lines, down from 571. What remains is genuinely sequential: content load, window and pipeline creation, scene construction, the loop skeleton, and teardown.

## Behavior preservation

Ground quads render in submission order, so moving development channels into their own pass draws them after every authored surface rather than interleaved. For overlapping elevation areas that is a visible difference; the test area has one elevation area, and the captured frame is unchanged.

## Verification

- Debug build clean; Release build clean; project warnings still errors.
- Release presentation verification at 30 Hz, 60 Hz, 120 Hz, monitor-matched, and uncapped retained replay hash `7074030210802259671`, unchanged by the refactor.
- New automated-run verdict tests pass inside `ic2de.core`.
- 16 of 17 CTest suites pass. `ic2de.scene` fails on five assertions belonging to in-flight schema 8 content work: the packaged scene has five textures where the tests expect six, eighteen clips where they expect nineteen including `tree-sway`, no `animation_auto` records, and player atlases with two frames per tag where the tests expect eight. Those five failures are identical before and after this checkpoint.
- A 300-tick automated run reports `Ground 10/16` submitted and visible quads, matching the count before the submission split, with every debug channel rendering as before.

## Known gaps

- `scene.cpp` and `scene_document.cpp` still carry two independent implementations of `trim`, field splitting, `valid_id`, and number parsing, and state every record's field count twice: as magic numbers in `require_field_count()` calls and as named constants in the document. Adding a field to a record requires remembering both, or UUID-addressed edits silently stop matching. A shared internal text-record module would single-source it. Left alone here because both files are mid-edit by the schema 8 work.
- A packaged checkpoint could not be produced: `package.ps1` stops at the red scene suite until the schema 8 content lands.
