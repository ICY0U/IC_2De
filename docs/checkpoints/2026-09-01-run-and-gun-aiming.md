# Run-and-gun Aiming Checkpoint

## Outcome

WASD movement and mouse aim remain independent while the player holds LMB. Eight-way body presentation is now stable near sector boundaries, projectiles leave a forward muzzle point, and the temporary needle-pistol presentation has a readable pixel trail, core, and muzzle flash.

A fast mouse update immediately after LMB release can no longer replace the ordered release transition in Combat's render-to-fixed-tick buffer. This closes a case that could leave automatic fire latched after the physical button was released.

## Working

- Continuous world X/Z aim still follows the exact mouse/controller direction; only eight-way body presentation uses hysteresis.
- The prior facing sector gets a 7.5-degree retention margin, preventing animation chatter around each 22.5-degree boundary.
- Movement remains camera-relative and authoritative from WASD while facing and firing follow aim independently.
- Fire press/release transitions remain FIFO even when later aim-only samples are coalesced before the next fixed tick.
- The projectile origin is seven world pixels forward along normalized aim and fourteen pixels above the ground anchor.
- Each projectile renders through Render2D as a dark trail, gold bolt, pale core, and first-tick muzzle flash.
- The crosshair expands and changes centre colour while fire is held.

## Measured

- The facing regression was red when a small cursor move across the east/southeast boundary immediately selected southeast. It now retains east until the pointer clearly leaves the widened sector.
- The LMB ordering regression was red because a later aim-only command replaced the queued release and produced another automatic shot on tick 9. It now retains the release and keeps the total at one shot.
- All 21 Debug CTest targets pass.
- The real Debug `--smoke-run-and-gun` route moved north, aimed east, held fire through tick 18, captured the editor viewport, and completed with three projectile spawns.
- The packaged Release editor repeated the run-and-gun route at 30 FPS, 60 FPS, and unlocked presentation. All three ended at player/camera X/Z `-208.142715/-206.492783` with three spawns and clean texture lifetime.
- The packaged Release projectile-impact route still resolved one real crate impact after the muzzle-origin change.
- Content validation and the NVIDIA/OpenGL texture hot-swap/resource-lifetime probe pass.
- `dist/IC_2DE-Editor-Windows-x64.zip` is 11,513,888 bytes with SHA-256 `AF7043F0ABF4D4DF7591B5985AB7278DFB6AF1FCC8512ED95BF28CF122A3B03E`.

## Manual check

1. Launch `dist/editor-windows-x64/IC_2DE-Editor.exe` and keep the pointer inside the Viewport.
2. Hold two movement keys for a diagonal strafe and circle the pointer around the character. Movement should continue while the body turns toward the crosshair.
3. Hover near a cardinal/diagonal boundary. Tiny mouse motion should not rapidly swap animation clips; a deliberate direction change should still turn promptly.
4. Hold LMB while moving, then release LMB and continue moving the pointer. Shooting must stop without a later stray shot.
5. Check that bolts originate in front of the body and that the trail/muzzle flash remain crisp at 30 FPS, 60 FPS, and unlocked presentation.

## Broken or deferred

- Dedicated strafe and backpedal animation clips do not exist. The current character keeps aim-facing while the shared movement cycle plays.
- The muzzle point is a temporary geometric offset. Authored per-direction weapon sockets and a separate weapon overlay remain planned.
- Recoil, camera impulse, impact particles, hit-stop, and audio remain deferred.
- Automated routes verify deterministic state and captures, not subjective mouse feel; the manual check above is still required.

## Learned

Aim-only coalescing is safe only when every state transition that changes future fixed-tick behavior is treated as ordered input. Presentation hysteresis belongs after continuous aim resolution so it can stabilize art without reducing projectile accuracy.

## Next small step

Add a target-health module behind a copied damage-command/event interface. Begin with failing fixed-tick tests for health reduction, duplicate projectile rejection, and one death event, then bind the existing patchwork NPC as the editor target dummy.
