# IC_2DE Engine Charter

Status: provisional foundation charter, created August 30, 2026.

## Mission

Build a focused, production-quality 2D-first 2.5D engine in C++20 and prove it through a polished top-down adventure testbed. The World is true 3D space, while the visual identity stays sprite-led and Pokémon-like. Professional quality is measured through frame pacing, iteration speed, reliable content data, diagnostics, packaging, and player-visible feel.

The engine is not initially intended to compete with the breadth of Unity, Godot, or Unreal. It should be unusually good at producing its reference game.

## Reference game

The first engine customer is a compact top-down 2.5D adventure testbed with:

- responsive camera-relative keyboard and Xbox-compatible controller movement over an X/Z ground plane;
- a player, one enemy, one dynamic physics prop, and one trigger;
- sprite animation with gameplay frame events;
- 2D billboard characters and props positioned in true X/Y/Z space;
- a short interaction with hit-stop, particles, shake, audio, and rumble;
- one scene transition, settings, pause, and save/load;
- an editable room that can be packaged as a standalone Windows build.

This is a rendering and world-structure direction, not a commitment to copying Pokémon's mechanics or art. It is narrow enough to expose timing, movement, collision, animation, tooling, and game-feel requirements early.

## Presentation target

- Pixel-art-friendly 640 x 360 internal canvas.
- Orthographic oblique camera over X/Z, with +Y reserved for elevation.
- Camera-facing 2D sprites, ground-aligned terrain, and projected-depth ordering.
- Integer scaling where the chosen window size permits it.
- 1280 x 720 minimum supported output.
- 1920 x 1080 and 2560 x 1440 primary outputs.
- Correct frame pacing from 60 Hz through 144 Hz displays.
- Safe layout at 16:9 first; ultrawide uses a protected gameplay and UI safe area until explicitly designed.

## Platform target

- Primary: 64-bit Windows 10 and Windows 11.
- Development compiler: MSVC with C++20.
- Provisional minimum PC: four x86-64 CPU cores, 8 GB RAM, and a GTX 1050-class GPU or equivalent with current drivers.
- Development PC: RTX 2080 Ti; it is not acceptable as the only performance test machine.
- Linux should remain an inexpensive future port, but Windows delivery takes priority during v0.1.

The minimum GPU is a planning baseline and must be confirmed on representative hardware before the vertical-slice content pass.

## Technology decisions

- raylib 6.0 is pinned as the initial platform/render/audio dependency.
- EnTT 4.0.0 implements World storage behind engine-owned interfaces; Box2D 3.1.1 implements ground-plane rigid-body simulation behind `PhysicsWorld`.
- Physics maps engine X/Z pixels onto Box2D XY metres at an explicit 32 pixels per metre. Discrete World Y elevation remains engine-owned in `GroundMap`.
- Third-party types remain inside engine implementations and adapters.
- Fixed simulation runs at 60 Hz; render rate is independent and interpolated.
- CMake presets define reproducible Debug and Release builds.
- Lua is deferred until two real gameplay behaviours demonstrate that data and C++ iteration are insufficient.
- Networking, Web, consoles, visual scripting, and multiple render backends are outside v0.1.

## Initial quality budgets

- 60 FPS minimum on the provisional minimum PC.
- Inspect p95 and p99 frame times, not average FPS alone.
- Target no more than 8 ms CPU and 8 ms GPU in the representative stress scene.
- Reach the first interactive screen in less than 3 seconds from a packaged build.
- Stable engine-owned memory after 100 scene reloads.
- Identical replay hashes across repeated runs of the same build and platform.
- Debug and Release compile with zero warnings in project-owned code.
- Failed saves never destroy the previous valid save.

## Working rules

1. Every new engine module must unlock a visible reference-game requirement.
2. Every milestone ends in a runnable checkpoint.
3. Performance changes require before-and-after evidence.
4. Reproducible bugs receive regression tests when practical.
5. Runtime behaviour and game feel require a play check; a successful build is not enough.
6. Authored data is versioned before production content depends on it.

## Decisions still requiring owner confirmation

- Keep pixel art or change to a high-resolution/hand-drawn pipeline.
- Decide whether elevation is authored in discrete levels, continuous ramps, or both before physics integration.
- Confirm access to a representative minimum-spec Windows machine.
- Confirm whether ultrawide is a v0.1 requirement.
- Confirm the working engine name before public-facing assets and package metadata are created.
