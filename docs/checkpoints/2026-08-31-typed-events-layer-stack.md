# Typed Runtime Events and Owned Layer Stack Checkpoint

## Outcome

IC_2DE now routes copied simulation notifications through a typed engine-owned event stream and an owned layer stack. Runtime contact, trigger, and animation producers return data after each fixed tick; the application no longer reaches into `RuntimeSceneTickResult` through unrelated event booleans or animation-only arrays.

This is the fourth expanded Hazel adaptation stage. The reference remains TheCherno/Hazel commit `1feb70572fa87fa1c4ba784a2cfeada5b4a500db` under Apache-2.0; no Hazel source was copied. Audio remains deferred.

## Deep module decisions

**One event batch, closed typed alternatives.** `EngineEvent` is a variant of `SceneContactEvent`, `SceneTriggerEvent`, and `SceneAnimationEvent`. Physics and animation details are copied into engine-owned values after their producing step. Animation events carry both the authored textual id and persistent `EntityUuid`; no Box2D, raylib, EnTT, or editor implementation type crosses the seam.

**The layer stack owns lifecycle and traversal safety.** `LayerStack` owns every `Layer` with `std::unique_ptr`, attaches regular layers before overlays, updates bottom-up, routes events top-down, and stops propagation only when a layer reports the event handled. Destruction detaches from the topmost overlay down.

**Structural changes are deferred automatically.** A layer may request a push or removal while receiving an update/event. The stack keeps the current traversal stable and applies requests in order afterward. Callers do not manage a second pending list or know the vector layout.

**The interface is the test surface.** Ordering, handling, ownership, invalid timing, and callback-requested transitions are tested entirely through `LayerStack`; its entry vector and transition queue remain private behind the pimpl.

## Runtime integration

`RuntimeScene::tick()` now returns one deterministic event batch. Contact begin/end and trigger enter/exit events retain their tags; trigger events identify whether the visitor is the player. Animation frame events carry stable entity identity.

The first production layer is `RuntimeObservationLayer`. It consumes the same event route future gameplay and tools will use and drives automated collision, trigger, animation, and diagonal-locomotion evidence. Its stable-identity check makes the Shipping smoke fail if animation events lose their persistent UUID.

Window and input data remain in the existing action-oriented input frame. They will join shared event routing only when another real consumer requires cross-module fan-out.

## Scene-editor reliability repair

While reviewing the previous command checkpoint, the dirty-state marker was changed from history depth to unique revision identity. Save, undo, and a different edit at the same stack depth can no longer be mistaken for the saved branch. A dedicated regression covers the divergence.

## Verification

- New flow tests cover bottom-up update order, top-down event order, handled propagation, attach/detach order, deferred push/removal from inside a callback, unknown removal, null ownership, and invalid/non-finite fixed timing.
- The scene-editor suite now covers save, undo, and divergent-edit dirty-state identity.
- Debug CTest: 16 of 16 passed.
- Release CTest: 16 of 16 passed.
- Release presentation verification at 30 Hz, 60 Hz, 120 Hz, monitor-matched, and uncapped retained replay hash `7074030210802259671`.
- The packaged Shipping GPU smoke observed contact, player-trigger, and stable-UUID animation events through the layer route, completed 300 ticks, retained the replay hash, released resources, and shut down cleanly.
- The shipping build metadata contains no Dear ImGui dependency.
- No development debug markers or leftover scene temporary files remain.
- `dist/IC_2DE-Windows-x64.zip` contains eleven entries and the Shipping, Debug, and Editor executables. It has 17,618,377 expanded bytes and 7,468,684 archive bytes. SHA-256: `42984B4CB1E5169B81FC51E922CD82EBAFCAB7445B221C6BA6D01385AF3AE205`.

## Next checkpoint

Continue the non-audio Hazel adaptation with renderer resources: asset-managed shaders and framebuffers, a post-process pass, richer renderer statistics, an editor camera independent of the playable camera, and the first 2D lighting path. The existing unavailable `lights` debug channel becomes active only when that real lighting module lands.
