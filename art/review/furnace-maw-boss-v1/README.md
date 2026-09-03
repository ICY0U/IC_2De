# Furnace Maw Boss V1

Review-only boss concept and representative motion studies. Nothing here is
connected to gameplay, scenes, animation bindings, enemy AI, or the player.

## Design

Furnace Maw is the larger, more threatening evolution of Fuse Maw. It keeps
the vessel species language while adding layered riveted armour, two horn-like
exhausts, three fuses, asymmetric furnace eyes, huge crushing gauntlets,
heavier claws, and a broader glowing crack network. The intended runtime body
envelope is roughly 1.7 times the regular enemy, with a separate larger canvas
for death and explosion effects.

## Review set

- eight-direction boss turntable;
- five-frame south pressure-breathing idle;
- eight-frame east crushing walk;
- eight-frame east bounding chase;
- five-frame south heavy hurt reaction;
- ten-frame south mechanical-collapse death;
- ten-frame south multi-stage boss explosion.

The root files preserve the generated review sheets. `clean/` contains the
successful genuine-alpha versions. The walk sheet resisted two alpha
extraction attempts and remains source-only; rejected derivatives are isolated
under `failed-alpha/`.

After approval this needs a native-grid cleanup pass, uniform cells and roots,
exact timings, separate explosion VFX, and direction coverage chosen from the
final encounter design. It is deliberately not imported or bound yet.
