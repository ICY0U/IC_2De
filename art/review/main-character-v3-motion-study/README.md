# Main Character V3 Motion Study

Status: **review-only; not approved for runtime integration**

This package explores a stronger, grounded stance and new motion language for
the existing patchwork-undead character. It is intentionally disconnected from
the scene, runtime assets, metadata, and code. The V2 character and its source
files remain unchanged.

Read the supporting research before choosing a direction:
[Professional Pixel Character Animation Research](../../../docs/research/PROFESSIONAL_PIXEL_CHARACTER_ANIMATION_RESEARCH.md).

## Review set

### Standing idle

- `00-idle-turntable-8-direction.png`: stance and turntable overview.
- `idle-{direction}-6f.png`: six-pose breathing studies for south, southwest,
  west, northwest, north, northeast, east, and southeast.
- Proposed timing at 60 Hz: `20,10,8,8,10,16` ticks (1.20 seconds).

The intended read is relaxed readiness: both feet planted, a centred pelvis,
separated hands and boots, stable eye line/root, restrained chest/back contour
change, then delayed beanie and hand settlement. There is no locomotion pose in
the loop.

### Seated idle

- `seated-idle-south-6f.png`
- `seated-idle-north-6f.png`
- Proposed timing at 60 Hz: `24,12,12,12,16,20` ticks (1.60 seconds).

These are redrawn compressed silhouettes rather than scaled-down standing
sprites. Floor contact, hips, root, knees and boots remain stable while the
upper torso/back and beanie provide the quiet breathing motion.

### Dodge language

| File | Frames | Proposed ticks | Read |
|---|---:|---|---|
| `dodge-quick-sidestep-east-6f.png` | 6 | `1,1,2,3,3,2` | Agile lateral displacement |
| `dodge-shoulder-roll-east-8f.png` | 8 | `1,1,2,2,2,2,1,1` | Compact committed roll |
| `dodge-low-slide-east-8f.png` | 8 | `1,1,1,2,3,2,1,1` | Long low scrappy slide |
| `dodge-back-hop-south-7f.png` | 7 | `1,1,2,3,2,2,1` | Defensive airborne retreat |

Every comparison fits a 12-tick/200-ms envelope. World translation is implied
by pose and force; a later runtime implementation would continue to own actual
movement.

### Shooting

- `shoot-south-6f.png`
- `shoot-east-6f.png`
- `shoot-north-6f.png`
- Proposed timing at 60 Hz: `1,1,1,2,2,2` ticks (150 ms).

The shot responds on the first frame, reaches maximum opposite-direction
recoil, lets the head/beanie trail, then returns to the aim-ready pose. The
compact dark-teal/brass needle pistol, two-handed grip and muzzle direction are
the shared design study.

## Current gate

These images are polished **motion concepts**, not production sprite atlases.
They use an opaque dark-teal review backdrop and retain generated high-resolution
pixel styling. Before any approved design can ship, it needs a native logical
grid redraw, locked indexed palette, genuine alpha, fixed uniform cells,
per-frame cleanup, `.aseprite` source, exported PNG/JSON, and 1x loop review.

The critical research finding is that the current game displays a roughly
24x34 character while the earlier source frames are hundreds of pixels tall.
Simply shrinking generated artwork cannot create professional pixel clusters.

## Generation prompt set

The built-in image-generation workflow used the V2 strips only as identity and
perspective references. Each prompt locked the character's proportions,
patchwork costume, beanie construction, palette and light direction; requested
one action/direction per strip; specified exact key-pose roles, stable roots,
equal baselines and seamless entry/exit poses; and excluded locomotion poses,
background clutter, labels, crosshairs, shadows and scene integration.

The two left-facing idle strips and three over-counted sheets received one
targeted correction pass for facing direction or exact six-pose layout. No
original file was edited.

