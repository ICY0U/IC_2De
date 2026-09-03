# Fuse Maw Enemy V1

Review-only enemy concept and representative motion studies. No scene, code,
runtime asset, prefab, AI state, or player file references this folder.

## Design

Fuse Maw is an original low, non-human explosive creature: a cracked
iron-and-oxidized-teal vessel body, furnace eye, hinged teeth, asymmetrical
riveted arms, short claws, and one braided fuse. It uses the linked
[Kamikaze Bomber Enemy Asset Pack](https://segnah.itch.io/kamikaze-bomber-asset-pack)
only as high-level reference for readable idle, movement, hurt, pre-explosion,
explosion, and death state separation. It does not copy the pack's tall yellow
humanoid silhouette or individual sprites.

## Review set

- eight-direction design turntable;
- six-frame south idle;
- eight-frame east walk;
- eight-frame east chase;
- four-frame south hurt;
- eight-frame south mechanical-collapse death;
- eight-frame south pressure-build, explosion, and dissipation study.

The root files preserve the generated review sheets. `clean/` contains the
successful genuine-alpha versions. The explosion sheet resisted two alpha
extraction attempts and remains source-only; rejected derivatives are isolated
under `failed-alpha/`.

This is not yet production-ready native pixel art. After approval it needs
logical-cell redraw/cleanup, fixed pivots and baselines, exact per-frame
timings, separated blast VFX, and full directional locomotion coverage.
