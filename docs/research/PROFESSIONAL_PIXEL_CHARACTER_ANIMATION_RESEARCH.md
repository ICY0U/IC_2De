# Professional Pixel Character Animation Research

**Project:** IC_2DE  
**Research date:** 2026-09-02  
**Scope:** Small top-down / three-quarter-view player character; eight-direction standing idles, north/south seated idles, dodge variants, and firearm shooting.  
**Status:** Research and review specification only. Nothing in this document authorizes changing code, scenes, runtime assets, or the current character.

## Contents

1. [Executive conclusions](#1-executive-conclusions)
2. [Evidence base and source quality](#2-evidence-base-and-source-quality)
3. [Why the submitted idles fail](#3-why-the-submitted-idles-fail)
4. [Native-resolution pixel-art standard](#4-native-resolution-pixel-art-standard)
5. [Character identity and eight-direction turntable](#5-character-identity-and-eight-direction-turntable)
6. [Animation principles translated to this character](#6-animation-principles-translated-to-this-character)
7. [Standing idle specification](#7-standing-idle-specification)
8. [North and south seated-idle specification](#8-north-and-south-seated-idle-specification)
9. [Dodge variant specification](#9-dodge-variant-specification)
10. [Firearm aim and shooting specification](#10-firearm-aim-and-shooting-specification)
11. [Seamless loops and state transitions](#11-seamless-loops-and-state-transitions)
12. [Professional production workflow](#12-professional-production-workflow)
13. [Review and acceptance gates](#13-review-and-acceptance-gates)
14. [Recommended review-only deliverables](#14-recommended-review-only-deliverables)
15. [Checklist for a reusable Codex skill](#15-checklist-for-a-reusable-codex-skill)
16. [Sources](#16-sources)

## 1. Executive conclusions

The supplied idle images should not be polished in place. They need to be replaced by a new, authored turntable and a new set of animation keys.

The main problems are structural rather than cosmetic:

- The poses read as interrupted run or walk frames: the feet are narrow or lifted, the knees are in transit, and the torso leans without a stable support leg.
- The character's proportions, hat mass, visible face area, limb lengths, and stance width change between directions. The set does not read as one model rotated through eight views.
- Front and rear silhouettes collapse the arms into the torso and the legs into a narrow central cluster. Several diagonal views are too close to side profile.
- The current art source is not native-resolution pixel art. A representative production strip is 2079×756 and contains **197,042 distinct non-transparent ARGB values**. Its packed south frames are 188×323, while the scene declares a 24×34 display rectangle. That would require approximately 7.83:1 horizontal and 9.50:1 vertical reduction if used directly, which cannot preserve deliberate pixel clusters or the original aspect ratio.
- A professional replacement must therefore be constructed on a fixed logical pixel grid, not generated large and merely reduced until it appears pixelated.

The high-level production decision is:

> Use image generation or drawing references only to explore poses. Final review art must be redrawn and cleaned at the intended logical resolution, use a locked palette and anchor, and pass 1× pixel review before it can be described as professional pixel art.

This follows working pixel artist Pedro Medeiros's guidance that every pixel in low-resolution art must communicate something, unnecessary information must be removed, and low-resolution art cannot be treated as an ordinary illustration that is simply resized smaller ([My Thoughts on Very Low Resolution](https://teamkano.medium.com/my-thoughts-on-very-low-resolution-a2beba5deeb)). His consistency guidance likewise recommends native-resolution rendering, consistent pixel density, integer scaling, and redrawing additional directions when rotation would expose inconsistent pixels ([My Thoughts on Style Consistency](https://saint11.art/blog/consistency/)).

## 2. Evidence base and source quality

The recommendations below prioritize material written or presented by working animators, game artists, animation studios, and the developer of the production tool:

- **Walt Disney Animation Studios:** official descriptions of timing, staging, squash and stretch, anticipation, follow-through, secondary action, strong pose planning, rough animation, cleanup, and on-model consistency ([Animation](https://www.disneyanimation.com/process/animation/), [Hand-Drawn Animation](https://www.disneyanimation.com/process/hand-drawn-animation/)).
- **Jonathan Cooper:** an excerpt from the professional book *Game Anim: Video Game Animation Explained*, covering the 12 principles specifically under interactive-response constraints, including silhouette, timing, arcs, overlapping action, recoil, and regaining control before visual follow-through finishes ([The 12 Principles of Animation in Video Games](https://www.gamedeveloper.com/production/the-12-principles-of-animation-in-video-games)).
- **Mariel Cartwright:** a GDC session based on *Skullgirls* production, emphasizing keyframing, anticipation, smears, and timing under restrictive gameplay conditions ([Powerful and Effective Animation for 2D/3D Games](https://www.gdcvault.com/play/1021657/Powerful-and-Effective-Animation-for)).
- **Tyriq Plummer:** an official GDC presentation applying animation fundamentals directly to 2D game animation ([2D Animation for Games: A Primer](https://www.youtube.com/watch?v=PKZJmHrG4Yw)).
- **Pedro Medeiros:** first-party pixel-art and animation instruction from the artist behind *Celeste* and *TowerFall*, including native resolution, clusters, subtle idle movement, top-down attacks, abrupt evasions, attack responsiveness, and a pose-to-pose production sequence.
- **Aseprite:** official documentation for frame durations, cels, animation tags, previewing, onion skinning, and lossless sprite-sheet / JSON export ([Animation](https://www.aseprite.org/docs/animation/), [Sprite](https://www.aseprite.org/docs/sprite/), [Tags](https://www.aseprite.org/docs/tags/), [Onion Skinning](https://www.aseprite.org/docs/onion-skinning/), [CLI](https://www.aseprite.org/docs/cli/)).

There is no first-party source that prescribes one universal north/south seated-idle formula for a 24×34 top-down sprite. The seated design below is therefore an explicit project synthesis of body mechanics, clear posing, top-down layer separation, native-resolution construction, and transition continuity—not a claimed industry law.

## 3. Why the submitted idles fail

### 3.1 Visual diagnosis

| Observed issue | What the eye reads | Professional correction |
|---|---|---|
| One foot lifted or tucked behind the other | Mid-stride, tip-toeing, or running in place | Put both feet in credible contact; place the center of mass over the support polygon |
| Constant bent-knee forward lean | A movement pose frozen between frames | Use a relaxed but purposeful stance with a stable pelvis and a slight, controlled knee bend |
| Narrow front/back stance | Legs merge into one noisy central shape | Separate boot clusters by at least one clear background pixel wherever the final resolution allows |
| Arms merge into the coat/body | Torso becomes a single unreadable blob | Preserve negative space or a strong value boundary between hand, sleeve, and torso |
| Hat width and crown height change by view | The character appears to change head size | Lock a turntable envelope for the skull and beanie; only perspective foreshortening may alter it |
| Diagonals resemble profiles | Facing direction is ambiguous | Show both near and far shoulder/foot relationships, with purposeful overlap and foreshortening |
| Different overall height per direction | Root or body-volume pop when turning | Lock the foot baseline, standing head band, and perceived body volume across the entire turntable |
| Too many tiny shade/value changes | Crawling noise when animated or reduced | Construct a few stable clusters; make every changed pixel serve form or motion |
| Soft enlarged presentation | “Pixel-like” illustration rather than crisp pixel art | Author the logical pixels first; enlarge the finished frames only by an integer nearest-neighbour scale |
| Baked guides, crosshairs, floor marks, or background | Dirty asset and unreliable alpha | Deliver true RGBA transparency; keep pivots and guides in the authoring file, never in the art layer |

Clear poses must communicate the action without unnecessary visual information. Disney's animation process begins with strong poses and later performs a dedicated cleanup/on-model pass ([Hand-Drawn Animation](https://www.disneyanimation.com/process/hand-drawn-animation/)). Cooper similarly describes simplicity of pose and clear silhouettes as foundations of readable gameplay animation ([The 12 Principles of Animation in Video Games](https://www.gamedeveloper.com/production/the-12-principles-of-animation-in-video-games)).

### 3.2 Current-project technical diagnosis

The current scene declares the player at **24×34 logical display units** in `game/assets/runtime/test_area.scene`. The current packed south animation metadata uses **188×323** frames. A representative source-strip audit found **197,042** distinct non-transparent ARGB values across `art/production/main-character-v2/strips/player-v2-walk-south.png`.

Those numbers are incompatible with a controlled final-resolution palette and cluster language. They indicate high-resolution illustrated source material with antialiasing, translucency, and/or gradient variation. Medeiros defines pixel clusters as continuous same-colour shapes, recommends minimizing unnecessary and one-pixel clusters, and treats jagged contours as shapes that require deliberate pixel-step rhythms ([Cluster Sketching and Painting](https://medium.com/pixel-grimoire/how-to-start-making-pixel-art-2-bcd705cb04d7)). He also warns that excessive anti-aliasing produces blur and that each pixel should improve readability ([Anti-Alias and Banding](https://medium.com/pixel-grimoire/how-to-start-making-pixel-art-4-ff4bfcd2d085)).

The practical conclusion is not “use a better scaling filter.” The practical conclusion is “replace the final art construction method.”

## 4. Native-resolution pixel-art standard

### 4.1 Logical pixel grid

For the first review pass, use this project-specific working contract:

| Property | Review target |
|---|---|
| Neutral standing body scale | Authored to read at 24×34 logical pixels |
| Review/action cell | 40×40 logical pixels, transparent, same body scale in every action |
| Standing root | Fixed horizontal center; boot baseline fixed across directions and frames |
| Sitting root | Same world-ground point as standing; torso lowers around it rather than moving the root |
| Review enlargement | 8× or 10× integer nearest-neighbour only |
| Shipping source of truth | The 1× logical frame, not the enlarged review sheet |
| Palette | One locked indexed or explicitly enumerated RGBA palette |
| Alpha | Fully transparent outside the sprite; no baked checkerboard, shadow, guide, or background |

The 40×40 review cell is not permission to enlarge the character. It only leaves room for a horizontal gun, a low roll, extended limbs, and a muzzle flash while keeping the body at the same logical pixel density. If later runtime integration cannot preserve that pixel scale and anchor, the engine-facing contract must be solved after art approval; the artwork must not be non-uniformly squeezed to fit.

Medeiros recommends choosing a base resolution, keeping world elements internally consistent, avoiding anything smaller than a screen pixel, and using integer nearest-neighbour scaling for crisp output ([My Thoughts on Style Consistency](https://saint11.art/blog/consistency/)). His beginner guide also recommends keeping the editable source and using round-number export scaling instead of partial scaling that breaks pixels ([An Absolute Beginner's Guide](https://saint11.art/pixel_art_articles/article1/)).

### 4.2 Palette and clusters

The following are proposed project limits, to be validated visually rather than treated as universal pixel-art law:

- Begin with **12–16 visible colours plus transparency** for the entire character, including outline, skin, beanie, coat, patches, boots, and firearm.
- Reuse ramps between materials where readability permits. Do not add a new shade for one isolated pixel.
- Keep the darkest outline/value coherent, but allow selective internal outlines to disappear where they crush small forms.
- Use two- or three-pixel clusters for small highlights whenever a single orphan pixel does not carry a crucial feature such as an eye glint.
- Preserve the same light direction, ramp ordering, outline behaviour, and highlight density in every view.
- Do not dither the clothing at this scale. Patch identity should come from larger colour blocks, not noisy texture.
- Review contours for accidental banding, double-thick corners, broken staircase rhythms, and semitransparent fringe.

At very low resolution, Medeiros recommends selecting only the features that communicate identity, keeping lighting simple, maintaining a low purposeful colour count, and using solid contrasting shapes when outlines consume too much space ([My Thoughts on Very Low Resolution](https://teamkano.medium.com/my-thoughts-on-very-low-resolution-a2beba5deeb)). His cluster and anti-aliasing articles support a cleanup pass that removes orphan noise, jaggies, excessive halftones, and banding ([Cluster Sketching and Painting](https://medium.com/pixel-grimoire/how-to-start-making-pixel-art-2-bcd705cb04d7), [Anti-Alias and Banding](https://medium.com/pixel-grimoire/how-to-start-making-pixel-art-4-ff4bfcd2d085)).

### 4.3 Pixel-motion discipline

At a 34-pixel character height, moving the complete body by one pixel is a large visible event. Subtle idle motion should therefore be constructed mostly by changing the contour or value distribution of selected clusters while keeping the root, feet, eye line, and overall head mass stable. Medeiros specifically warns that a bouncy idle can read as dancing and recommends implied less-than-one-pixel motion for slow, delicate idles ([Character Idle](https://www.patreon.com/saint11/posts/character-idle-12464240), [Less Than One Pixel Movement](https://www.patreon.com/saint11/posts/1-pixel-movement-7652033?l=en-GB)).

Project rule: no full-sprite one-pixel bob on every frame. If a one-pixel vertical change is used at all, offset it through compression/extension inside the silhouette and return the feet to the identical baseline.

## 5. Character identity and eight-direction turntable

### 5.1 Identity lock

Every animation must preserve:

- short, rounded patchwork-undead proportions;
- oversized purple rib-knit beanie with a consistent crown, brim, and symbol placement;
- yellow-green stitched face;
- large pale eyes and small toothy expression where the face is visible;
- teal/green/yellow/purple patchwork clothing;
- compact gloves/hands and dark boots;
- one consistent outline and light-source treatment;
- one consistent weapon design, scale, grip, and handedness.

The turntable is the model sheet. Animation must follow it; each action must not reinvent the character.

### 5.2 Eight authored directions

Author all eight views rather than rotating one sprite. Medeiros explicitly recommends redrawing as many directions as the budget permits because rotating pixel art commonly exposes inconsistent pixel shapes ([My Thoughts on Style Consistency](https://saint11.art/blog/consistency/)).

Mirroring is acceptable only if all of the following are deliberately accepted:

- the weapon changes hands on screen;
- all asymmetric patches and stitches swap sides;
- the light direction remains believable after the mirror;
- the gameplay does not attach effects or equipment to a fixed hand.

For this character, genuine eight-direction art is the professional recommendation because the patchwork costume, beanie markings, and firearm handedness are visually asymmetric.

### 5.3 Directional silhouette targets

| Direction | Required read | Must not happen |
|---|---|---|
| South | Face is primary; two boots and both shoulders create a stable triangular stance | Arms/boots merge into one central column |
| Southwest | Near shoulder, near hand, and near boot dominate; far side remains visible but compressed | Becomes a pure west profile |
| West | Clear profile with one dominant eye/cheek edge; gun/hand silhouette stays separate from torso | Character leans as if already running |
| Northwest | Back-three-quarter read; near shoulder and boot define depth; only a restrained face edge if any | Face remains as exposed as the front diagonal |
| North | Beanie/back/shoulder mass leads; two boots remain separated; face is hidden | Front facial pixels leak into the rear view |
| Northeast | Mirror of spatial depth, not necessarily literal pixels; back and far/near overlaps are clear | Becomes a pure east profile |
| East | Profile matches west volume and height while preserving handedness and costume logic | Head or hat becomes a different size |
| Southeast | Front-three-quarter read; face, near hand, and both feet communicate the angle | Reads as side-on due to losing the far shoulder/foot |

Run a black-silhouette review before shading. Pixar's technical memo on character appeal reports that clean silhouettes and simplified forms retain stylized design even in extreme action ([Articulating the Appeal](https://graphics.pixar.com/library/ArticulatingAppeal/paper.pdf)). Cooper likewise emphasizes simple, readable poses and silhouettes, especially when gameplay cameras show characters at small scale ([The 12 Principles of Animation in Video Games](https://www.gamedeveloper.com/production/the-12-principles-of-animation-in-video-games)).

## 6. Animation principles translated to this character

Disney identifies timing, clear staging, squash and stretch, anticipation, follow-through, secondary action, anatomy, weight, movement, and appeal as core ingredients of character performance ([Animation](https://www.disneyanimation.com/process/animation/)). The following table converts those general principles into constraints for this sprite:

| Principle | Application here |
|---|---|
| Clear staging | A single frame must say “idle,” “sitting,” “dodging,” or “shooting” at 1× without relying on labels |
| Strong key poses | Approve silhouettes for rest, compression, action, impact/recoil, and recovery before drawing in-betweens |
| Timing and spacing | Slow, uneven spacing for breath; explosive early spacing for dodge/fire; slower controlled recovery |
| Anticipation | Restrained in player attacks and dodges to preserve responsiveness; longer only where gameplay explicitly allows it |
| Squash and stretch | Compress posture and extend limbs while preserving perceived body volume; never scale the bitmap with a filter |
| Arcs | Hat tip, hands, elbows, knees, and recoil travel through simple readable arcs, checked with onion skinning |
| Overlap | Torso leads; hands, beanie tip, and loose cloth settle slightly later instead of everything changing together |
| Follow-through | Let the beanie and arms finish the action after the main body has begun returning to the controllable pose |
| Secondary action | Hat lag and a small coat/patch response support the main action; they must not compete with it |
| Exaggeration | Enlarge the readable change in silhouette, recoil, or compression—not the amount of random pixel noise |
| Solid drawing | Preserve volume, balance, and perspective across all eight directions and every frame |
| Appeal | Use a confident, characterful pose rather than generic symmetrical stiffness or frantic bouncing |

Cooper warns that moving all body parts at the same rate looks unnatural, recommends using follow-through to sell weight, and describes natural arcs as a key polish test ([The 12 Principles of Animation in Video Games](https://www.gamedeveloper.com/production/the-12-principles-of-animation-in-video-games)). Cartwright's GDC production talk identifies keyframing, anticipation, smears, and timing as important tools specifically under gameplay constraints ([Powerful and Effective Animation for 2D/3D Games](https://www.gdcvault.com/play/1021657/Powerful-and-Effective-Animation-for)).

## 7. Standing idle specification

### 7.1 Pose brief

The new default idle should read as **relaxed readiness**, not running in place and not a rigid mannequin:

- feet planted slightly wider than the current examples;
- pelvis centered between the boots;
- knees unlocked but not deeply crouched;
- torso upright with a small direction-specific twist;
- shoulders relaxed and asymmetrical by one controlled pixel where the angle supports it;
- hands clearly separated from the coat, ready to move toward the weapon;
- head and eyes steady enough to remain the identity focal point;
- beanie tip or crown lagging the torso by one phase, not bouncing independently;
- ground/root fixed in every frame.

### 7.2 Recommended six-frame loop

This is a starting timing target at the project's 60 Hz fixed presentation rate. It is a project recommendation, not a universal industry frame count.

| Frame | Pose purpose | Duration | Pixel-motion budget |
|---:|---|---:|---|
| 1 | Neutral / exhale rest | 20 ticks (333 ms) | Fixed root, feet, eyes, and head mass |
| 2 | Inhale begins | 10 ticks (167 ms) | Chest/shoulder contour changes; no whole-body lift |
| 3 | Inhale high | 8 ticks (133 ms) | Maximum one-pixel internal extension; hat begins lag |
| 4 | High settle | 8 ticks (133 ms) | Torso nearly held; hat catches up |
| 5 | Exhale | 10 ticks (167 ms) | Reverse contour change; hands settle after torso |
| 6 | Return toward neutral | 16 ticks (267 ms) | Near-neutral, not a duplicate hold; flows into frame 1 |
| **Total** |  | **72 ticks / 1.20 s** |  |

The loop must be evaluated at 1×. If it reads as “dancing,” reduce contour changes before adding frames. Medeiros's idle tutorial makes this exact distinction between appealing bounce and accidental dance, and calls for less-than-one-pixel techniques in slow idles ([Character Idle](https://www.patreon.com/saint11/posts/character-idle-12464240)).

### 7.3 Direction-specific idle acting

Do not copy the same pixel offsets into all eight views. The rhythm is shared, but the visible anatomy differs:

- **South:** breath reads through shoulders, upper coat, and a tiny hand/forearm settle; keep eyes absolutely stable.
- **North:** breath reads through shoulder width and upper-back contour; the beanie crown can settle after the shoulders.
- **East/West:** breath reads through chest-to-back thickness and the near elbow; do not translate the entire profile vertically.
- **Front diagonals:** use near/far shoulder counterchange to reinforce three-quarter depth.
- **Rear diagonals:** use back contour, near elbow, and hat lag; do not reveal extra face just to show motion.

## 8. North and south seated-idle specification

### 8.1 Design intent

The seated north and south idles should feel like the same character lowering their center of mass onto the ground, not a smaller replacement sprite. The standing root remains the world contact reference. Hips and torso move down around that anchor; the sprite does not drift across the cell.

Because an immediate standing-to-seated swap will always pop, professional coverage ultimately needs three tags per direction:

1. `sit_down_{north|south}` — one-shot transition;
2. `seated_idle_{north|south}` — seamless loop;
3. `stand_up_{north|south}` — one-shot transition.

For the current review request, the required art is the north and south seated loop plus enough transition key poses to prove that a clean stand/sit connection is possible. No code or scene integration should be attempted.

### 8.2 South seated silhouette

Required:

- face remains the focal point and retains its established proportions;
- hips visibly lower; torso must not merely shrink;
- knees form two readable side clusters or a clear bent-leg triangle;
- boots remain distinct from knees and from each other;
- hands rest on knees or ground with unambiguous contact;
- beanie silhouette compresses only through posture/perspective, not model drift;
- the floor contact area is wider than the standing stance, communicating weight.

Reject if it reads as kneeling, squatting, or a standing character with shortened legs.

### 8.3 North seated silhouette

Required:

- upper back and shoulder line clearly face away;
- beanie/back relationship matches the north standing turntable;
- bent legs or boots create a wider grounded footprint beneath or beside the torso;
- arms separate from the back through negative space or value contrast;
- no front eye, mouth, or face patch leaks into the rear view;
- the silhouette remains asymmetrical enough to feel alive but balanced around the root.

Reject if it reads as a smaller north standing sprite or if the boots disappear entirely into the body.

### 8.4 Recommended timings

| Animation | Frames | Tick pattern at 60 Hz | Total | Notes |
|---|---:|---|---:|---|
| Sit down | 8 | `2,2,2,3,3,3,3,4` | 22 ticks / 367 ms | Keys: stand, knee bend, hand support, hip drop, seated settle |
| Seated idle | 6 | `24,12,12,12,16,20` | 96 ticks / 1.60 s | Quieter than standing; upper torso/hat only |
| Stand up | 8 | `3,3,4,4,3,3,2,2` | 24 ticks / 400 ms | Weight shifts forward before final extension |

The seated loop's root, hip contact, and floor footprint are invariant. Any breathing motion should happen through upper-back/chest clusters and delayed hat movement.

Disney's hand-drawn workflow supports beginning with strong acting poses, preserving body weight and mass, then cleaning the result back on model ([Hand-Drawn Animation](https://www.disneyanimation.com/process/hand-drawn-animation/)). The exact seated poses and timings above are project synthesis, because no authoritative source reviewed here dictates a universal seated pixel-idle cycle.

## 9. Dodge variant specification

### 9.1 Shared dodge grammar

Medeiros groups slide, roll, and dash as distinct versions of abrupt evasive movement. His shared pattern is a very short or absent preparation for player characters, an action/travel phase, and a recovery that may overshoot ([Slide / Roll / Dash](https://www.patreon.com/saint11/posts/slide-roll-dash-16562907)).

Every project dodge concept must therefore show:

- an input-readable change on the first displayed frame;
- a clear compression or push-off;
- one unmistakable maximum-action silhouette;
- a recovery contact and absorption pose;
- a final pose compatible with the destination standing idle or walk;
- a fixed sprite root while gameplay owns world translation;
- delayed hat/coat follow-through that never obscures the main body action.

### 9.2 Four visually distinct concepts

All four review concepts are normalized to the existing 12-tick / 200 ms action envelope so the user can compare style rather than different gameplay speeds.

| Variant | Frames | Suggested ticks | Signature silhouette | Character impression |
|---|---:|---|---|---|
| Quick sidestep | 6 | `1,1,2,3,3,2` | Upright lean, wide lateral boot placement, shoulders counterbalance | Agile, controlled |
| Shoulder roll | 8 | `1,1,2,2,2,2,1,1` | Compressed ball, shoulder leads, back/hat form a circular sweep | Physical, committed |
| Low slide | 8 | `1,1,1,2,3,2,1,1` | Long low body line, lead leg extended, trailing knee tucked | Scrappy, fast |
| Back-hop | 7 | `1,1,2,3,2,2,1` | Chest pulls away, both feet leave/tuck, wide landing absorption | Defensive, reactive |

### 9.3 Direction requirements

- Create one clean key-pose strip for all eight directions before producing all in-betweens.
- Side and diagonal dodges must preserve the near/far limb order of the turntable.
- North/south roll silhouettes must use body compression and shoulder/boot overlap to communicate travel because horizontal elongation is less available.
- Low slides may extend beyond the neutral 24×34 body box but must remain inside the uniform 40×40 review cell.
- The beanie may deform within its established volume, but it must not detach or change design.
- No dust, blur, trail, or shadow is baked into the character sprite. Optional effects belong on separate transparent layers/sheets.

Fast action still needs readable keys. Cartwright's GDC session emphasizes keyframing, smears, timing, and anticipation within gameplay limitations ([Powerful and Effective Animation for 2D/3D Games](https://www.gdcvault.com/play/1021657/Powerful-and-Effective-Animation-for)). Cooper notes that follow-through and held recovery poses can sell weight after the fast motion without delaying the initial response ([The 12 Principles of Animation in Video Games](https://www.gamedeveloper.com/production/the-12-principles-of-animation-in-video-games)).

## 10. Firearm aim and shooting specification

### 10.1 Required state separation

A professional shooting set should not jump directly from a hands-down idle to a full recoil frame. Produce:

- `aim_idle_{direction}` — weapon already shouldered or held ready; seamless loop;
- `shoot_{direction}` — one-shot firing/recoil/recovery returning exactly to aim idle;
- optional `aim_lower_{direction}` and `aim_raise_{direction}` transition keys for later approval;
- muzzle flash as a separate transparent effect layer or export, aligned to a documented muzzle point.

The current request is review-only: these assets are not to be bound to input, weapon logic, projectiles, code, or scene entities.

### 10.2 Responsiveness and recoil

For player attacks, Medeiros recommends showing the response immediately and placing weight in the recoil and return rather than delaying the input with a long anticipation ([Simple Attack Animation](https://www.patreon.com/posts/simple-attack-6837623)). Cooper makes the same game-animation trade-off and specifically uses exaggerated pistol kickback as a way to communicate weapon power while maintaining instant firing response ([The 12 Principles of Animation in Video Games](https://www.gamedeveloper.com/production/the-12-principles-of-animation-in-video-games)).

Recommended six-frame single-shot cycle:

| Frame | Pose/event | Duration | Visual requirement |
|---:|---|---:|---|
| 1 | Fire response | 1 tick | Muzzle flash appears immediately; hands/gun begin recoil |
| 2 | Maximum recoil | 1 tick | Barrel and shoulders move opposite firing direction; silhouette is strongest |
| 3 | Recoil hold / overlap | 1 tick | Body absorbs force; head/beanie trails slightly |
| 4 | Fast return | 2 ticks | Weapon moves toward aim line; elbows remain readable |
| 5 | Recovery settle | 2 ticks | Torso and knees finish absorbing force; hat catches up |
| 6 | Exact aim-idle match | 2 ticks | Body, hands, muzzle point, root, and palette match aim idle |
| **Total** |  | **9 ticks / 150 ms** |  |

For a heavier weapon concept, extend the recoil hold and recovery to 12–15 ticks rather than delaying frame 1.

### 10.3 Top-down construction

Medeiros recommends treating a top-down character as layered masses—legs, torso, and head—that slide in the attack direction ([Top Down Attack](https://www.patreon.com/saint11/posts/top-down-attack-21959820)). Apply that here without losing the root:

- legs brace first and remain the grounded counterforce;
- torso and shoulders shift opposite the muzzle direction during recoil;
- weapon/hands lead the fast movement;
- head follows the torso with smaller motion;
- beanie follows last;
- the recovery unwinds in the reverse overlap order.

### 10.4 Directional firearm readability

| View | Required gun read |
|---|---|
| East/West | Longest barrel silhouette; two-handed grip or supporting hand must be readable |
| Front diagonals | Near hand and barrel create a clear diagonal; far arm supports depth |
| Rear diagonals | Shoulder line and weapon direction dominate; face exposure stays consistent with turntable |
| South | Barrel is foreshortened; separate muzzle flash, hand spacing, and shoulder recoil must carry direction |
| North | Weapon points away/up-screen; back and shoulder compression must communicate recoil without showing front facial detail |

Never rotate a single side gun sprite to create all directions. Never mirror if it changes handedness or patch/lighting logic. Document a muzzle coordinate for every direction even though no runtime hookup occurs yet.

### 10.5 Muzzle flash and projectile effects

- Keep the flash on its own layer/export.
- Use a compact, high-contrast cluster that reads at 1×.
- Allow one primary flash frame and, only if needed, one dim after-frame.
- Do not let the flash permanently add colours to the character's palette.
- Do not cover the entire gun/hand silhouette.
- Do not bake the bullet trail into every character frame.

Medeiros's bullet tutorial recommends studying successful game references and separates projectile readability from character motion concerns ([Bullets](https://www.patreon.com/posts/bullets-12176568)). The exact flash size and colour count remain project art-direction decisions.

## 11. Seamless loops and state transitions

### 11.1 “Blend” means continuity, not opacity

Do not alpha-crossfade pixel sprites. Crossfades create double outlines, semitransparent fringe, muddy clusters, and extra colours. For this project, seamless blending means:

- the same root and ground baseline;
- compatible silhouettes at the outgoing and incoming poses;
- preserved support foot and center-of-mass logic;
- compatible head height and body volume;
- consistent near/far overlap for the facing direction;
- consistent weapon handedness and muzzle point;
- no palette or outline change at the boundary.

### 11.2 Transition contracts

| Transition | Required contract |
|---|---|
| Standing idle → walk | Idle feet/head align with a valid walk contact or passing pose; no root jump |
| Walk → standing idle | Select a stable support/contact phase, then settle; never freeze an arbitrary airborne foot |
| Standing idle → dodge | First frame supplies immediate compression/lean; outgoing facing and support foot remain intelligible |
| Dodge → standing idle | Final recovery pose is one small contour change from the destination idle |
| Aim idle → shoot | Frame 1 responds immediately; no hands-down snap |
| Shoot → aim idle | Last shooting frame matches the aim-idle root, hands, gun angle, and muzzle point exactly |
| Standing → seated | Use an authored sit-down transition; no scale swap or instant vertical teleport |
| Seated → standing | Hands/weight shift precedes extension; final pose matches standing idle exactly |
| Direction change while idle | Turntable volume and baseline remain stable; diagonal views do not collapse into profile |

### 11.3 Loop seam review

For every loop:

1. Play at intended timing, 1×, on light, mid-value, and dark test backgrounds.
2. Play first → last → first repeatedly and watch the root, boots, face, hat, and brightest patch.
3. Overlay first and last frames at 50% only as an authoring diagnostic, never as delivered art.
4. Check whether frame 1 and the last frame are accidental duplicates that create a double hold.
5. Check whether a one-pixel feature changes on only one frame, producing a flash.
6. Reverse preview for diagnostic purposes; asymmetries and broken arcs often become more obvious.
7. Inspect onion-skin arcs for hat, hands, elbows, knees, and gun.

Aseprite officially supports per-frame durations, animation tags, real-time preview, and configurable onion skinning, making these checks part of a reproducible authoring workflow ([Animation](https://www.aseprite.org/docs/animation/), [Sprite](https://www.aseprite.org/docs/sprite/), [Onion Skinning](https://www.aseprite.org/docs/onion-skinning/)). Medeiros treats seamless looping as a deliberate animation problem that requires dedicated practice and evaluation ([Seamless Animation Tutorial](https://www.patreon.com/posts/seamless-7346998)).

## 12. Professional production workflow

Medeiros describes his own complex-animation order as **still frame → rough sketches → keyframes → in-betweens and general fixes** ([Animation Planning](https://www.patreon.com/saint11/posts/animation-7585006)). Disney similarly distinguishes pose planning, rough animation, cleanup, and on-model in-betweens ([Hand-Drawn Animation](https://www.disneyanimation.com/process/hand-drawn-animation/)). Use that pass structure here:

### Pass 0 — Lock the contract

Record before drawing:

- logical pixel dimensions and uniform frame cell;
- fixed root, ground baseline, and nominal body envelope;
- eight directions and exact naming;
- handedness and weapon design;
- identity sheet and allowed asymmetries;
- palette and light direction;
- animation list, frame count, frame roles, and durations;
- whether the output is review-only or approved for integration.

### Pass 1 — Turntable silhouettes

- Draw one black silhouette for each of eight standing directions.
- Draw north and south seated silhouettes.
- Draw one maximum-action silhouette for each dodge concept.
- Draw aim and maximum-recoil silhouettes for all eight shooting directions.
- Review only at 1× and integer enlargement.
- Do not shade or in-between until the silhouettes are approved.

### Pass 2 — On-model stills

- Add internal colour blocks and identity features to the approved silhouettes.
- Enforce the same head, hat, torso, boot, and weapon volume.
- Remove detail that does not survive at 1×.
- Validate all directions side by side as a turntable.

### Pass 3 — Rough key animation

- Draw only rest, extreme, contact/action, and recovery keys.
- Time the keys before cleanup.
- Preview rapidly and reject weak or ambiguous action at this cheap stage.
- Do not generate in-betweens merely to make motion “smooth.”

### Pass 4 — Timing and spacing

- Use long holds and small cluster changes for idle breathing.
- Use immediate response and wide early spacing for player fire/dodge.
- Use recovery holds to communicate weight.
- Make the beanie/hands overlap the torso rather than move simultaneously.

### Pass 5 — In-betweens

- Add only frames required to clarify arcs, spacing, overlap, or transition compatibility.
- Preserve body volume and directional perspective.
- Maintain the root and baseline.
- Check that no in-between has a weaker or contradictory silhouette.

### Pass 6 — Native-pixel cleanup

- Reduce to the locked palette.
- Remove generated gradients, semitransparent antialiasing, and colour noise.
- Repair clusters, jaggies, banding, and accidental one-pixel flashes.
- Restore identical construction details and outline behaviour.
- Confirm genuine alpha and remove all guides, backgrounds, crosshairs, and baked shadows.

### Pass 7 — Export and review

- Keep the editable `.aseprite` source with named tags and per-frame durations.
- Export true-alpha PNG sprite sheets plus JSON metadata.
- Export a 1× lossless preview and an 8× nearest-neighbour review preview.
- Produce a labelled contact sheet separately from the clean art; labels must never be inside production frames.
- Store all work under a review-only folder until the owner explicitly approves integration.

Aseprite documents named animation tags, direction modes, per-frame durations, sprite-sheet export, JSON data, tag selection, and padding controls ([Tags](https://www.aseprite.org/docs/tags/), [Sprite Sheet](https://www.aseprite.org/docs/sprite-sheet/), [CLI](https://www.aseprite.org/docs/cli/)).

## 13. Review and acceptance gates

An asset fails if any mandatory item below fails. “Looks good when enlarged” is not sufficient.

### 13.1 Static/model gate

- [ ] All eight directions read correctly at 1× without labels.
- [ ] Standing character height, head mass, hat construction, torso volume, and boot scale stay on model.
- [ ] South/north and all diagonals are genuine authored views, not transformed approximations.
- [ ] Face exposure follows perspective: full at south, partial at front diagonals/profile, little or none at rear diagonals, none at north.
- [ ] Hands, arms, knees, and boots remain identifiable through negative space or value separation.
- [ ] One root and one baseline are used in every frame.
- [ ] Palette and lighting remain identical across directions.
- [ ] No unexplained orphan pixels, contour jaggies, banding, gradients, or semitransparent fringe.
- [ ] True transparent background; no checkerboard pixels, guide marks, labels, crosshair, or baked ground shadow.

### 13.2 Idle gate

- [ ] Every standing pose reads as planted and at rest, never as a walk/run frame.
- [ ] Motion is subtle at 1× and does not read as dancing.
- [ ] Feet, root, face, and eye line do not bob or slide.
- [ ] Hat and hands overlap/lag the torso by a controlled phase.
- [ ] Last-to-first transition has no pop, flash, duplicated pause, or palette change.
- [ ] All eight idles share rhythm but use direction-specific visible anatomy.

### 13.3 Seated gate

- [ ] South seated reads as sitting, not squatting or kneeling.
- [ ] North seated reads as facing away, without front facial leakage.
- [ ] Hips visibly lower while head/body volumes remain on model.
- [ ] Knees, boots, and hands create believable floor contact.
- [ ] Ground footprint and root do not move during the loop.
- [ ] Transition keys demonstrate that standing ↔ sitting can occur without scale or root pops.

### 13.4 Dodge gate

- [ ] Each concept has a distinct maximum-action silhouette.
- [ ] The first frame responds immediately.
- [ ] Compression, action, recovery contact, and absorption are readable.
- [ ] All variants fit the same 12-tick comparison envelope for review.
- [ ] Root stays fixed while the pose sells travel.
- [ ] Final frame is compatible with the destination idle/walk pose.
- [ ] No clipping in any direction; no effect baked into the body art.

### 13.5 Shooting gate

- [ ] Aim idle exists and loops cleanly before shooting is judged.
- [ ] The firing response and muzzle flash appear on the first action frame.
- [ ] Recoil travels opposite the shot direction.
- [ ] Legs, torso, shoulders, hands, gun, head, and beanie react in an ordered overlap.
- [ ] Gun model, scale, grip, handedness, and muzzle coordinate remain consistent.
- [ ] North/south foreshortened shots still read unambiguously.
- [ ] Final frame exactly reconnects to aim idle.
- [ ] Muzzle flash/effects are separate from the character sprite.

### 13.6 Technical/export gate

- [ ] Editable native-resolution source retained.
- [ ] Fixed logical frame dimensions and pivot data recorded.
- [ ] Correct frame count and per-frame duration for every tag.
- [ ] Lossless PNG with genuine RGBA transparency.
- [ ] No non-integer resizing at any production stage.
- [ ] 1×, 8× nearest-neighbour, silhouette, and multi-background review previews supplied.
- [ ] Review assets remain unreferenced by scenes/runtime until explicit owner approval.

## 14. Recommended review-only deliverables

The next art review should be staged; do not spend cleanup effort on dozens of in-betweens before the model and key poses are approved.

### Review A — Direction and stance approval

1. Eight standing idle stills, one per direction.
2. South seated still and north seated still.
3. One black-silhouette contact sheet.
4. One full-colour contact sheet.
5. 1× originals and 8× nearest-neighbour previews.

### Review B — Motion-language approval

1. One complete six-frame standing idle for south.
2. One complete six-frame standing idle for north.
3. One complete six-frame standing diagonal idle.
4. North/south seated loops.
5. Maximum-action key strip for quick sidestep, shoulder roll, low slide, and back-hop.
6. Aim → fire → recoil → recover key strip for south, east, and north.

### Review C — Full directional coverage

Only after A and B are approved:

1. Complete standing idles for all eight directions.
2. Complete selected dodge variant(s) for all eight directions.
3. Complete aim and firing cycles for all eight directions.
4. Transition keys and production cleanup.

Every deliverable stays under a new review folder and remains disconnected from code, scenes, and runtime bindings.

## 15. Checklist for a reusable Codex skill

The following condensed checklist is suitable as the operational core of a future professional pixel-character-animation skill.

### Inputs

- [ ] Read the user's reference images and the current character identity sheet.
- [ ] Confirm review-only versus integration authorization; default to review-only.
- [ ] Determine final logical display resolution from the project, not the source PNG dimensions.
- [ ] Lock cell size, body scale, root, baseline, palette, light direction, handedness, directions, frame roles, and timing.
- [ ] State all assumptions; do not silently invent equipment or redesign the character.

### Research and planning

- [ ] Apply native-resolution, cluster, silhouette, timing, anticipation, overlap, arc, and follow-through principles.
- [ ] Plan still → rough → keyframes → in-betweens → native-pixel cleanup.
- [ ] Author/approve eight-direction silhouettes before full animation.
- [ ] Define explicit transition contracts for every requested state.

### Generation

- [ ] Treat generative output as pose/concept reference unless it already passes the native-pixel gate.
- [ ] Generate only the requested review assets; do not touch original sprites or integrate them.
- [ ] Never accept a high-resolution pseudo-pixel image as final merely because it has square-looking details.
- [ ] Never accept missing/duplicated frames, inconsistent character models, baked checkerboards, guides, labels, or shadows.

### Cleanup

- [ ] Redraw on the final logical pixel grid.
- [ ] Reduce to the locked palette and repair clusters, jaggies, banding, and stray pixels.
- [ ] Restore fixed root/baseline, consistent body volume, view, costume, hat, and weapon.
- [ ] Ensure true alpha and sufficient transparent safety padding.

### Animation review

- [ ] Review every frame and loop at 1×.
- [ ] Review integer-enlarged, silhouette-only, onion-skin, and multi-background versions.
- [ ] Confirm idle is grounded and restrained; sitting is genuinely compressed; dodges have distinct action silhouettes; firing responds on frame 1 and recoils correctly.
- [ ] Confirm seamless first/last and cross-state pose continuity.

### Delivery

- [ ] Save editable source, clean PNG/JSON, 1× preview, and integer-scale review preview in a new review-only directory.
- [ ] Report exact frame counts, timings, dimensions, palette count, alpha status, and remaining risks.
- [ ] Ask the owner to review.
- [ ] Do not modify code, scene files, runtime assets, or the original character until the owner explicitly approves a later integration task.

### Automatic rejection conditions

Reject and regenerate/redraw if any of these occur:

- non-native or non-integer pixel scaling;
- inconsistent pixel density or perspective;
- arbitrary gradients or hundreds/thousands of near-duplicate colours;
- blurry antialiasing, semitransparent fringe, or colour crawl;
- character identity/proportion drift between frames or directions;
- ambiguous facing direction;
- unstable root, feet, eye line, gun, or muzzle coordinate;
- a standing idle that reads as walking/running/dancing;
- a seated pose that reads as shrinking/kneeling;
- a dodge without launch/action/recovery readability;
- shooting that delays the first response or returns to a different aim pose;
- clipped limbs/effects, baked background, checkerboard, guide, crosshair, text, or watermark;
- any unapproved edit to the current character, scene, runtime content, or code.

## 16. Sources

### Studio and game-animation principles

- Walt Disney Animation Studios, [Animation](https://www.disneyanimation.com/process/animation/).
- Walt Disney Animation Studios, [Hand-Drawn Animation](https://www.disneyanimation.com/process/hand-drawn-animation/).
- Pixar Animation Studios, [Articulating the Appeal](https://graphics.pixar.com/library/ArticulatingAppeal/paper.pdf).
- Jonathan Cooper, [The 12 Principles of Animation in Video Games](https://www.gamedeveloper.com/production/the-12-principles-of-animation-in-video-games), excerpted from *Game Anim: Video Game Animation Explained*.
- Mariel Cartwright, [Powerful and Effective Animation for 2D/3D Games](https://www.gdcvault.com/play/1021657/Powerful-and-Effective-Animation-for), GDC Vault.
- Tyriq Plummer, [2D Animation for Games: A Primer](https://www.youtube.com/watch?v=PKZJmHrG4Yw), official GDC presentation.

### Professional pixel-art and animation practice

- Pedro Medeiros, [My Thoughts on Very Low Resolution](https://teamkano.medium.com/my-thoughts-on-very-low-resolution-a2beba5deeb).
- Pedro Medeiros, [My Thoughts on Style Consistency](https://saint11.art/blog/consistency/).
- Pedro Medeiros, [An Absolute Beginner's Guide](https://saint11.art/pixel_art_articles/article1/).
- Pedro Medeiros, [Cluster Sketching and Painting](https://medium.com/pixel-grimoire/how-to-start-making-pixel-art-2-bcd705cb04d7).
- Pedro Medeiros, [A Basic Aseprite Animation](https://medium.com/pixel-grimoire/how-to-start-making-pixel-art-3-c9eb70270fa1).
- Pedro Medeiros, [Anti-Alias and Banding](https://medium.com/pixel-grimoire/how-to-start-making-pixel-art-4-ff4bfcd2d085).
- Pedro Medeiros, [Animation Planning](https://www.patreon.com/saint11/posts/animation-7585006).
- Pedro Medeiros, [Seamless Animation Tutorial](https://www.patreon.com/posts/seamless-7346998).
- Pedro Medeiros, [Character Idle](https://www.patreon.com/saint11/posts/character-idle-12464240).
- Pedro Medeiros, [Less Than One Pixel Movement](https://www.patreon.com/saint11/posts/1-pixel-movement-7652033?l=en-GB).
- Pedro Medeiros, [Slide / Roll / Dash](https://www.patreon.com/saint11/posts/slide-roll-dash-16562907).
- Pedro Medeiros, [Top Down Attack](https://www.patreon.com/saint11/posts/top-down-attack-21959820).
- Pedro Medeiros, [Simple Attack Animation](https://www.patreon.com/posts/simple-attack-6837623).
- Pedro Medeiros, [Bullets](https://www.patreon.com/posts/bullets-12176568).

### Tooling and reproducible export

- Aseprite, [Animation](https://www.aseprite.org/docs/animation/).
- Aseprite, [Sprite structure and frame durations](https://www.aseprite.org/docs/sprite/).
- Aseprite, [Tags](https://www.aseprite.org/docs/tags/).
- Aseprite, [Onion Skinning](https://www.aseprite.org/docs/onion-skinning/).
- Aseprite, [Sprite Sheet](https://www.aseprite.org/docs/sprite-sheet/).
- Aseprite, [Command-line export](https://www.aseprite.org/docs/cli/).

