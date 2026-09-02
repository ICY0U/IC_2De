# Main Character V2 Animation Review Package

Status: **approved and imported on 2026-09-02; review sources preserved here**

## Purpose

This package records the stronger motion language approved for the patchwork undead player. The generated PNGs remain the review sources; production copies, alpha cleanup, generated metadata, scene bindings, and dodge-state integration live under `game/assets/runtime` and `tools/generate-player-v2-metadata.ps1`.

## Character identity lock

Every final frame must preserve:

- the short, rounded patchwork body and friendly undead personality;
- the oversized purple rib-knit beanie and its existing symbol language;
- the yellow-green stitched face, large blank white eyes, and small toothy smile;
- the teal, green, yellow, and purple patchwork palette;
- the dark pixel outline, boots, limb proportions, and stable ground contact;
- coherent asymmetric patch placement through rotations and between frames.

No direction may introduce a different costume, body shape, face, rendering style, or character variant.

## Direction coverage

Five unique source directions cover all eight gameplay directions. Opposite horizontal directions use an exact presentation mirror so gait phase and timing cannot reverse accidentally.

| Gameplay direction | Review source | Playback |
|---|---|---|
| South | `walk-south-review.png` / `dodge-south-review.png` | authored |
| North | `walk-north-review.png` / `dodge-north-review.png` | authored |
| East | `walk-east-review.png` / `dodge-east-review.png` | authored |
| West | East source | horizontal mirror, same forward frame order |
| Southeast | `walk-southeast-review.png` / `dodge-southeast-review.png` | authored |
| Southwest | Southeast source | horizontal mirror, same forward frame order |
| Northeast | `walk-northeast-review.png` / `dodge-northeast-review.png` | authored |
| Northwest | Northeast source | horizontal mirror, same forward frame order |

Mirroring changes presentation only. Playback order must never be reversed; reversing the frames makes the feet push in the wrong direction and recreates the former backward-walk defect.

## Walk cycle

Each direction uses eight phase-compatible poses:

1. left-foot contact;
2. recoil/down;
3. passing;
4. high;
5. right-foot contact;
6. opposite recoil/down;
7. opposite passing;
8. opposite high.

Professional requirements:

- planted contacts with no visible foot slide;
- heel-to-toe roll and readable alternating knees;
- opposing arm swing with correct near/far overlap;
- restrained hip/shoulder counter-rotation;
- one-to-two-pixel compression and extension at final game resolution;
- subtle beanie lag and patchwork follow-through;
- stable eyes and head silhouette, with no excessive bobbing;
- identical root anchor and foot baseline for every frame;
- first and fifth frames must be opposite-foot contacts and the eighth-to-first transition must close cleanly.

Recommended full-speed timing at 60 fixed ticks is `[7, 5, 6, 6, 7, 5, 6, 6]` ticks, an 0.8-second cycle. Locomotion speed may scale playback within a narrow authored range; frame skipping is forbidden. Footstep presentation events occur on frames 1 and 5, while gameplay movement remains physics-authoritative.

## Dodge cycle

The dodge matches the existing 12-fixed-tick gameplay duration with eight poses and proposed durations `[1, 1, 2, 2, 2, 2, 1, 1]` ticks:

1. walk-compatible compressed anticipation;
2. explosive push-off;
3. low extension;
4. compact shoulder-led tuck;
5. fastest low transit pose;
6. lead-foot recovery contact;
7. recoil/absorption;
8. walk-compatible passing/ready exit.

The sprite remains on one root anchor because RuntimeScene owns world translation. The animation communicates force through silhouette, compression, overlap, and secondary motion rather than moving the image across its cell. Gameplay invulnerability remains Combat-owned and must not depend on animation events.

## Transition and blending rules

Pixel art should not use opacity cross-fades: they create ghost limbs, double outlines, and muddy pixel clusters. “Blend” means pose, phase, anchor, and velocity continuity.

### Walk to walk

- Preserve normalized gait phase when changing direction.
- Map contact/recoil/passing/high to the same phase in the new directional clip.
- Keep the support foot consistent when changing between cardinal and diagonal views.
- Apply the existing facing hysteresis before switching art so mouse noise cannot chatter directions.
- For a 135–180 degree reversal, wait until the nearest contact gate unless gameplay requires an immediate turn; never reverse clip playback.

### Idle to walk

- Enter the walk at the nearest contact or passing pose based on the requested acceleration.
- Preserve the last movement-facing direction while stationary.
- Match idle foot placement, root anchor, head height, and beanie silhouette to the selected walk entry pose.

### Walk or idle to dodge

- Freeze the gameplay dodge direction first, then select the matching eight-way dodge clip.
- Choose the entry pose from the outgoing gait support foot. Final production should have lead-left and lead-right entry variants or a cleaned phase-safe equivalent where the view permits it.
- Anticipation is intentionally short: one fixed tick. Input responsiveness takes priority over showing every drawing for a long duration.
- Movement-facing owns dodge presentation; aim-facing must not rotate the body during the active dodge.

### Dodge to walk or idle

- Frame 8 is the transition contract, not decorative recovery.
- If movement remains held, enter the matching walk passing/high phase using the recovery foot.
- If movement is released, finish frame 8 and settle into the matching directional idle without snapping the root or head height.
- A new dodge cannot interrupt recovery unless Combat accepts the command; animation never invents gameplay permission.

## Art-production gate before import

The generated strips are motion references, not final shippable atlases. A professional cleanup pass must:

1. redraw every pose onto a shared low-resolution pixel grid;
2. remove generated antialiasing and reduce colors to the approved character palette;
3. lock character height, head size, beanie construction, patch map, outline weight, root anchor, and baseline;
4. strengthen the southeast three-quarter read, which is currently too close to side profile;
5. create or validate opposite-lead dodge entries for phase-safe walk transitions;
6. ensure every strip has genuine alpha and no checkerboard pixels or fringe;
7. use uniform animation origins even if packed source rectangles have different widths;
8. validate every limb and hat extremity remains inside its source rectangle;
9. inspect the loops at 1x nearest-neighbour scale, not only enlarged;
10. obtain owner approval before replacing or adding any runtime atlas.

## Current technical review

| File | Frames | Motion review | Alpha/import status |
|---|---:|---|---|
| `walk-south-review.png` | 8 | stronger alternating contacts and knee lift | baked checkerboard; review only |
| `walk-north-review.png` | 8 | coherent rear gait and foot alternation | baked checkerboard; review only |
| `walk-east-review.png` | 8 | strongest side gait; clearly travels forward | genuine alpha; still requires pixel cleanup |
| `walk-southeast-review.png` | 8 | useful gait poses; three-quarter angle needs strengthening | baked checkerboard; review only |
| `walk-northeast-review.png` | 8 | strong rear-three-quarter direction and overlap | baked checkerboard; review only |
| `dodge-south-review.png` | 8 | clear anticipation, low transit, recovery | baked checkerboard; review only |
| `dodge-north-review.png` | 8 | clear away-from-camera launch and recovery | genuine alpha; still requires pixel cleanup |
| `dodge-east-review.png` | 8 | strongest readable walk-to-dodge silhouette | baked checkerboard; review only |
| `dodge-southeast-review.png` | 8 | clear diagonal lunge/tuck/recovery language | baked checkerboard; review only |
| `dodge-northeast-review.png` | 8 | strong rear-diagonal force and recovery | genuine alpha; still requires pixel cleanup |

No file in this table is engine-ready yet. The alpha defect was detected by inspecting the PNG pixel format and corner alpha, not by trusting the visual checkerboard preview.

## Proposed runtime names after approval

- `player-v2-walk-south`, `north`, `east`, `southeast`, `northeast`;
- mirrored presentation bindings for west, southwest, and northwest;
- `player-v2-dodge-south`, `north`, `east`, `southeast`, `northeast`;
- phase-safe dodge entry variants if the cleanup review confirms they are needed;
- presentation events: `footstep`, `dodge_launch`, `dodge_recover`;
- no animation event may control movement distance, invulnerability, cooldown, or collision.

## Acceptance test plan after approval

- static atlas validation: exact frame count, finite rectangles, genuine alpha, palette, bounds, stable origin, and baseline;
- direction montage: idle, walk, turn, dodge, recover for all eight directions;
- transition matrix: idle-to-walk, walk-to-walk at 45/90/135/180 degrees, walk-to-dodge, dodge-to-walk, and dodge-to-idle;
- left/west verification: mirrored presentation with forward playback order;
- fixed-tick verification: the dodge shows all eight poses across exactly 12 fixed ticks;
- replay verification: 30, 60, 120, monitor-synced, and uncapped presentation produce the same authoritative result;
- manual 1x review: no foot sliding, root pops, head-size drift, backward steps, duplicate poses, patch flicker, or diagonal perspective collapse.

## Generation method and prompt set

The built-in image-generation tool was used with the two current player atlases as identity and perspective references. Each selected strip used the same production prompt structure:

- exact existing character identity and palette;
- one horizontal row containing exactly eight sequential poses;
- fixed root, scale, cell spacing, and foot baseline;
- direction-specific camera view and travel force;
- explicit contact/recoil/passing/high walk choreography or anticipation/launch/transit/recovery dodge choreography;
- crisp limited-palette pixel art with no shadows, effects, labels, or redesign;
- genuine transparent background requested;
- preservation constraints for complete limbs, patch coherence, and seamless entry/exit poses.

Direction-specific prompts were issued for south, north, east, southeast, and northeast walks and dodges. West, southwest, and northwest are intentionally covered by phase-preserving horizontal presentation mirrors. A background-extraction edit was attempted on a checkerboard result; the built-in output still lacked genuine alpha, so no automated claim of import readiness is made.
