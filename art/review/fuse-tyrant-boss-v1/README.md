# Fuse Tyrant — tall boss review set v1

Status: **approved source art with a runtime derivative in use**. The high-resolution sheets in this folder remain review/source material; the generated native-grid atlas is now assigned to the large target enemy in `test_area.scene`.

## Direction

Fuse Tyrant is the larger, more threatening counterpart to Fuse Stalker. It keeps the same tall species language while adding a substantially larger cracked bomb head, three burning fuses, a spiked crown collar, heavier forearm claws, reinforced knee pistons, and furnace-like boots. It remains long, narrow, and top-heavy instead of turning into a wide or squat boss.

The [Kamikaze Bomber pack](https://segnah.itch.io/kamikaze-bomber-asset-pack) was used as a high-level reference for tall bomb-headed proportions and gait language only. The Tyrant's construction, palette, face, armour, and motion designs are original.

## Review sheets

- `00-fuse-tyrant-turntable-8-direction.png` — eight-direction boss identity and silhouette study.
- `idle-south-6f.png` — six-pose pressure idle with eye/fuse heat.
- `walk-east-8f.png` — eight-pose slow crushing stilt walk.
- `chase-east-8f.png` — eight-pose long bounding pursuit.
- `hurt-south-5f.png` — five-pose heavy head-whip recoil and recovery.
- `death-south-10f.png` — ten-pose catastrophic mechanical fold with no blast.
- `explode-south-10f.png` — ten-pose three-fuse charge, blast, debris, and smoke study.

## Transparency

The top-level PNGs are preserved ImageGen source sheets and contain a baked pale checkerboard. The `clean/` folder contains only derivatives confirmed as 32-bit RGBA with transparent corner pixels:

- turntable
- idle
- walk
- hurt

The chase, death, and explosion sheets are deliberately absent from `clean/`: repeated extraction attempts still returned opaque checker pixels. They need a manual pixel-editor mask before production use.

## Production gate

These are high-resolution animation concepts, not native-resolution game sprites. The runtime importer therefore produces a separate transparent, native-grid derivative rather than loading these presentation sheets directly.

## Runtime integration

- Runtime atlas: `../../../game/assets/runtime/fuse-tyrant-atlas.png`
- Clip metadata: `../../../game/assets/runtime/fuse-tyrant-atlas.json`
- Deterministic importer: `../../../tools/import-fuse-enemies.ps1`
- Scene role: the existing stationary damage target, with its original collision, health, retirement behavior, and UUID preserved.
- Driven now: eight-way idle and movement bindings; the active target remains stationary under its existing behavior.
- Imported for future state-machine work: walk, hurt, death, and explosion clips.

The source set only contains a true east-facing chase strip. Other movement directions use that animated cycle or its mirrored variant until dedicated directional boss locomotion is approved.
