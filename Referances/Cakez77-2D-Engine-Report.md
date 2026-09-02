# Cakez77 — 2D Game Engine & Current Project

**A technical research report**
Compiled: 2026-08-31
Subject: youtube.com/@Cakez77 · github.com/Cakez77 · twitch.tv/cakez77

---

## How to read this report

Every non-trivial claim below is tagged with a confidence level. Treat the tags as load-bearing — this report was compiled by cross-referencing public source code, his own written devlogs, video titles/metadata, and secondary press coverage, and several initial assumptions going in turned out to be wrong. Where something could not be verified, it is flagged rather than guessed.

| Tag | Meaning |
|---|---|
| **[VERIFIED]** | Confirmed directly from his source code, a repo he owns, or his own written words |
| **[SECONDARY]** | From press coverage, search-indexed metadata, or other people's reporting — plausible but not primary-sourced |
| **[INFERRED]** | A reasonable conclusion drawn from the evidence, not a stated fact |
| **[OPEN]** | Explicitly unresolved — flagged so it isn't mistaken for settled |

---

## Executive summary

Cakez77 is a solo indie developer who writes his own 2D game engine from scratch in **C/C++ using raw OpenGL** — not Odin/sokol, which was the working assumption at the start of this research and turned out to be incorrect. His engine (publicly known by the repo name `SchnitzelMotor`) follows a "handmade"-style, data-oriented architecture: no ECS, hand-written bump allocators, DLL-based hot code reload, and a hand-rolled immediate-mode UI, with no third-party dependencies beyond `stb_image` and FreeType.

He shipped one commercial title built on this engine — **Tangy TD**, a witch-themed tower-defense game, released on Steam March 9, 2026 after roughly four years of development, crossing $250,000 in revenue within about a week.

Since shipping Tangy TD, his devlogs have shifted to a new, unannounced project he refers to as a **"Gothic Remake"** (a fan homage to the 2001 Piranha Bytes RPG — not the unrelated official 2026 THQ Nordic remake of the same name). The devlog content is overwhelmingly about one technical challenge: simulating and rendering **100,000–200,000 enemies simultaneously**, using **GPU compute shaders** as part of a broader port of his renderer from raw OpenGL to **WebGPU** (targeting browser deployment via Emscripten/WASM). His public `WebGPU` repo confirms a working GPU-compute-driven simulate-and-render pipeline, and video titles confirm **flow-field pathfinding** as the movement model — though the actual Gothic-specific gameplay code remains private, and no transcript or written explanation of the exact technique has been found anywhere public.

---

## Part 1 — SchnitzelMotor: the engine behind Tangy TD

### 1. Language & toolchain — [VERIFIED]

The engine is written in **C/C++**. The `SchnitzelMotor` GitHub repository describes itself as "a crispy cross-platform C/C++ engine." Every related repo (`CelesteClone`, `VampireSurvivors`, `MyFirstGame`) is C or C++, and his ongoing devlog series is branded, literally, **"C++ Game Dev."**

No explicit stated rationale for choosing C++ was found. His GitHub bio describes him as a "Full Stack Web Developer" attempting his first indie game — suggesting a background outside native/systems programming, but this is biographical context, not a design justification. **[OPEN]**

A short cluster of videos from January 2025 ("Making a 3D Odin Game using Raylib," "I Made a 3D Game in Odin") shows a brief detour into Odin + Raylib for a 3D side project. It did not replace the C++/OpenGL stack — the shipped game and all subsequent devlogs remain C++. **[SECONDARY]**

### 2. The shipped game: Tangy TD — [VERIFIED / SECONDARY for commercial figures]

Originally tested on itch.io for years as **CakezTD**, rebranded and released on Steam as **Tangy TD** on **March 9, 2026**. A witch-themed tower defense game: place class-based towers (Defender, Archer, Healer), each with its own skill tree, combine 100+ items into damage builds, endless mode available.

Press coverage (80.lv, mein-mmo.de) reports roughly 3,676 day-one sales (~$25–32K net), crossing $250,000 within about a week, after approximately four years of development. **[SECONDARY]**

A minor, unrelated incident: a reused/lightly-modified "goblin" art asset triggered a DMCA claim and a brief Steam page takedown around May 12, 2026, restored shortly after.

Two public, non-commercial repos share the same engine source and function as teaching/devlog vehicles: `CelesteClone` (a Celeste-style platformer) and `VampireSurvivors` (despite the name, described in-repo as "a small and simple 2D tower defense game made with OpenGL").

### 3. Rendering architecture — [VERIFIED]

Raw **OpenGL, core profile** — no Vulkan, DirectX, Metal, or sokol-gfx. The Win32 platform layer creates a throwaway context to load WGL ARB extensions, then a real context. Source targets `#version 430 core` (**OpenGL 4.3**); separately, the shipped Steam build's stated requirement is **OpenGL 4.5**.

**Batching model:** everything funnels into one `RenderData` struct holding fixed-capacity arrays of a `Transform`, split only by opaque/transparent and world/UI:

```
transforms, transparentTransforms, uiTransforms, uiTransparentTransforms
```

Each frame these are pushed into a single **SSBO** via `glBufferSubData()`, then drawn in one call:

```c
glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
```

Six vertices form one quad; `count` is the number of sprites in the batch. A VAO is bound only because OpenGL requires one — a code comment notes it holds no actual attribute data. This is an immediate-mode-style render list, rebuilt every frame, not a retained scene graph.

**Hot-reloaded rendering assets:** shader source loads from disk and compiles at startup (with a shared header prepended). Textures are timestamp-checked against disk every frame and reloaded via `stb_image` when changed. His 2022 devlog names "hot reloading — shaders as well as assets" as a deliberate engine feature.

### 4. Entity & scene model — [VERIFIED]

**No formal ECS.** Game objects are plain structs nested directly in one monolithic `GameState`:

```cpp
struct Player {
  IVec2 pos; IVec2 prevPos; Vec2 solidSpeed; int renderOptions;
  float deathAnimTimer; float runAnimTimer; AnimationState animationState;
  SpriteID animationSprites[ANIMATION_STATE_COUNT];
};
struct GameState {
  GameStateID state; double updateTimer; bool initialized = false;
  float cameraTimer; GameInput gameInput[GAME_INPUT_COUNT];
  Player player; Level level; Sound jumpSound; Sound deathSound;
};
```

This is a deliberate choice, not an omission. His 13 October 2022 "Engine Rework" devlog explains he moved *away* from a generic Entity/component model: the old system forced everything through an "Entity," even a plain quad ("If I just wanted to draw a damn 'Quad', I had to create a damn 'Entity'"), and it fell over at scale — 500+ enemies meant scanning every entity just to find an attack target. The rework's stated direction: decouple the renderer from game logic entirely, and stop routing trivial objects through a generic entity abstraction.

### 5. Memory management — [VERIFIED]

A hand-written **bump/arena allocator**:

```c
struct BumpAllocator { size_t capacity; size_t used; char* memory; };
```

`bump_alloc()` 8-byte-aligns every allocation and asserts on overflow. Two named arenas mirror the classic Handmade-Hero permanent/transient split:

| Arena | Size | Lifetime |
|---|---|---|
| `PERSISTENT_STORAGE` | 256 MB | lives for the whole run |
| `TRANSIENT_STORAGE` | 128 MB | zeroed every single frame |

No STL containers are used — a custom fixed-capacity `Array<T, N>` template stands in for `std::vector` everywhere (`Array<Transform, MAX_TRANSFORMS>`, etc.), and text formatting uses a static buffer instead of `std::string`.

### 6. Hot reloading — [VERIFIED]

A classic Handmade-Hero-style DLL hot reload. Game logic compiles into its own DLL; the main loop checks its timestamp every frame and swaps it in live:

```c
while (running) {
  float dt = get_delta_time();
  reload_game_dll(&transientStorage);
  platform_update_window();
  update_game(gameState, input, renderData, soundState, uiState, &transientStorage, dt);
  gl_render();
  platform_update_audio(dt);
  platform_swap_buffers();
  transientStorage.used = 0;
}
```

On a newer timestamp: `FreeLibrary` on the old DLL, copy the freshly built one to a load-safe filename, `LoadLibraryA`, re-resolve `update_game` via `GetProcAddress`. Game state survives the swap because it lives in the persistent arena, owned by the exe rather than the DLL — allowing gameplay code edits and recompiles while the process keeps running. Assets hot-reload the same way, via per-frame timestamp checks in `gl_render()`.

### 7. Audio — [VERIFIED, one detail OPEN]

Custom-built, not layered on miniaudio/FMOD/OpenAL. WAV files load through a hand-rolled loader validated for 2-channel, 44,100 Hz PCM. Up to **16 concurrent voices** (`MAX_CONCURRENT_SOUNDS`), backed by a 128 MB buffer, with simple 1-second fade-in/fade-out mixing driven by frame `dt`.

Hardware output goes through the platform layer (`platform_init_audio`, `platform_update_audio(dt)`); the specific underlying Windows audio API (WASAPI / DirectSound / XAudio2) was not visible in the fetched source. **[OPEN]**

### 8. Input — [VERIFIED]

Handled directly in the Win32 window procedure. Keyboard state runs through a virtual-key lookup table with per-key `justPressed` / `justReleased` / `isDown` / `halfTransitionCount` — a standard "count transitions this frame" pattern. Mouse buttons plus `GetCursorPos()` convert screen coordinates to world space, pumped once per frame via `PeekMessageA()`.

### 9. UI system — [VERIFIED, editor scope OPEN]

Hand-rolled — no Dear ImGui anywhere in the codebase or search results. UI is declared imperatively each relevant frame:

```cpp
bool do_button(SpriteID spriteID, IVec2 pos, int ID);
void do_ui_text(char* text, Vec2 pos);
template <typename... Args>
void do_format_ui_text(char* format, Vec2 pos, Args... args);
```

`do_button`'s explicit `ID` parameter plus hover→press→release handling is the textbook **immediate-mode GUI** pattern (Casey Muratori lineage) — widgets are re-declared every frame and flushed through the same quad/SSBO pipeline as game sprites. No layout system: positioning is manual and absolute.

Whether a separate, more capable *editor* UI exists beyond this in-game one is unconfirmed — a 2022 devlog mentions "an embedded editor" without describing its toolkit, and that code is not public. **[OPEN]**

### 10. Asset pipeline — [VERIFIED, one detail OPEN]

Sprites pack into a single atlas PNG (e.g. `Texture_Atlas_01.png`, with a companion `.aseprite` source file — **Aseprite** is his pixel-art tool). Each sprite is a plain lookup entry:

```cpp
enum SpriteID { SPRITE_WHITE, SPRITE_CELESTE_01, SPRITE_CELESTE_01_BIG, /*...*/ };
struct Sprite { SpriteID ID; IVec2 atlasOffset; IVec2 size; int frameCount = 1; };
```

Animations are frame-strips within the atlas. Images load via **stb_image**; fonts (an Atari Classic TTF) render through vendored **FreeType**, drawn through the same quad pipeline as everything else.

**An unreconciled loose end:** the private `CakezTD-Beta` repo's asset folder (binary-only, no source) contains 60+ **DDS** textures and shaders compiled to **SPIR-V (.spv)** alongside GLSL source — which points toward a Vulkan path, conflicting with the shipped Steam build's stated OpenGL 4.5 requirement. Possibly an abandoned experiment, a GLSL→SPIR-V→GLSL tool in his pipeline, or leftover test files. **[OPEN]**

### 11. Editor tooling — [SECONDARY, largely OPEN]

His 13 October 2022 devlog describes an **embedded, in-engine editor** letting him edit tiled backgrounds and swap textures live while the game runs, wired into the hot-reload system, replacing an older Tiled-export-and-parse workflow. Beyond that one paragraph, nothing further was found — no walkthrough video, no confirmed UI toolkit, and the source is not public. **[OPEN]**

### 12. Physics & collision — [VERIFIED]

Deliberately lightweight: no rigid-body engine, no broad-phase spatial partitioning, no external physics library — just `point_in_rect()` and `rect_collision()` AABB tests, appropriate for a tower-defense/platformer that doesn't need real physics.

The platformer code (`CelesteClone`) shows a Celeste-style "solid vs. actor" setup — a `Solid` struct tracking `prevPos`/`pos`/`remainder` for sub-pixel movement accumulation across keyframed moving platforms — specific to that clone, not necessarily representative of Tangy TD's collision needs.

### 13. Build system & dependencies — [VERIFIED]

Shell-script build (`build.sh`) via **clang** (16.0.6 on Windows per the repo README), with a `.clangd` config and GitHub Actions CI for both Linux and Windows. Platform layer is hand-written per OS in the same repo — `win32_platform.cpp`, `linux_platform.cpp`, `mac_platform.cpp`/`.m` — raw Win32 API for window/GL-context creation, not SDL or GLFW. GL function pointers are loaded manually (`platform_load_gl_func`); no GLAD/GLEW.

Confirmed dependencies: `stb_image.h` (PNG loading), **FreeType** (vendored, font rasterization), raw OpenGL headers (`glcorearb.h`, `glext.h`, `wglext.h`, `glxext.h`). Not present anywhere: sokol, GLFW, SDL, miniaudio, Dear ImGui.

### 14. Architecture & philosophy — [VERIFIED premise, INFERRED conclusion]

The 2022 "Engine Rework" devlog is the clearest first-hand statement of his design thinking. He rebuilt the engine for three concrete reasons: the renderer and game logic were tangled together (each needed its own loop over every entity); a generic Entity/component requirement added overhead even for trivial objects; and the entity-scan approach collapsed at scale (500+ enemies). His stated fix: decouple rendering from game logic, stop forcing everything through one entity abstraction, add hot reloading, and build an in-engine editor to speed iteration.

Everything visible in the code — permanent/transient bump allocators, a hot-reloadable game DLL owned by a thin platform exe, fixed-capacity arrays instead of STL, SSBO+instanced-draw batching instead of per-object draw calls, hand-rolled immediate-mode UI — reads as straight out of the Handmade Hero "handmade"/data-oriented playbook. This is an inference from the code; no source found has him naming Casey Muratori or Handmade Hero directly. **[INFERRED]**

---

## Part 2 — Current project: "Gothic Remake" and the WebGPU port

### 15. Overview & disambiguation — [VERIFIED / SECONDARY]

Since shipping Tangy TD, his devlogs have shifted to a project he calls, in his own video titles, a **"Gothic Remake"** — a from-scratch homage to Piranha Bytes' 2001 open-world action-RPG *Gothic*.

**Important disambiguation:** this is his own personal project, entirely unrelated to the official commercial "Gothic 1 Remake" by Alkimia Interactive/THQ Nordic (released June 5, 2026). The two share a name by coincidence, and some of his videos are him reacting to/reviewing the *official* remake rather than devlogging his own — only titles carrying his usual "C++" devlog prefix are treated as his project in this report.

A stale `Cakez77/gothic-remake` GitHub repo also exists but is unrelated: Unity/C#, created January 2020, last touched May 2020. **[VERIFIED — dead end, confirmed via GitHub API metadata]**

No Steam page, store listing, or trailer exists for this project as of this report's compilation date — his Steam developer page lists only Tangy TD. No collaborators have surfaced; all signals match his established solo-dev pattern, though no explicit "solo" statement was found for this specific project. **[OPEN]**

### 16. Engine transition: OpenGL → WebGPU — [VERIFIED]

A new GitHub repository, **`Cakez77/WebGPU`**, was created **August 3, 2026** (language C, built with Emscripten per its README — three commits, all the same day). Devlog titles from late July into August 2026 ("Making A Game With WebGPU," "Making My Game Run In The Browser Again," "Maybe I'm Wrong About AI – Porting to WebGPU") confirm an active port of the renderer off raw OpenGL and onto **WebGPU**, targeting **browser deployment via Emscripten/WASM**.

He is still writing **C**, not switching languages — the "C++" devlog branding continues throughout. `SchnitzelMotor` itself has not been pushed to since November 2023, so this reads as the next stage of the same engine lineage rather than a clean-sheet rewrite, though that lineage link is not literally confirmed by a shared commit history. **[INFERRED]**

### 17. GPU compute offloading — [VERIFIED via direct source inspection]

The `WebGPU` repo's `src/main.c` and `src/web_gpu_renderer.c` (SDL3 + native WebGPU + Emscripten) contain a working, non-trivial compute example:

- A WGSL `@compute @workgroup_size(64)` shader (`cs_main`) reads and writes a `storage, read_write` buffer of `Particle { pos, vel, color, radius }` structs **entirely on the GPU** — position integration and bounds-bounce collision, dispatched via `wgpuComputePassEncoderDispatchWorkgroups`.
- That **same** storage buffer is then bound read-only to a vertex shader that draws instanced quads directly from it (`vs_particle`, indexed by `@builtin(instance_index)`), via a single `wgpuRenderPassEncoderDraw(pass, 6, currentParticleCount, 0, 0)`. Simulate-on-GPU → render-from-same-buffer, with **zero CPU involvement in the per-agent loop.**
- The buffer is sized for `MAX_PARTICLE_COUNT = 250,000` (16 MB), with a log line reading *"Example 03 initialized (Up to 250,000 particles supported)"* — confirming this is a self-authored WebGPU learning example, not final shipped game code.
- The code already contains a stub `Enemy { attack, speed, health, padding }` struct (hand-packed to 4 bytes, all `Uint8`) and an `ENEMY_SKELETON` entry — i.e., Gothic-specific enemy data is actively being wired into this exact compute/render scaffold, even though the committed compute shader as of this writing only simulates generic bouncing particles, not flow-field-guided or colliding enemies specifically.

**A correction to a natural assumption:** his *old* engine already targeted OpenGL 4.3, which has supported compute shaders (`GL_COMPUTE_SHADER` / `glDispatchCompute`) since that version — no compute-shader usage was found anywhere in the old `gl_renderer.cpp`. So the WebGPU port is not unlocking a capability OpenGL lacked; it looks more like he adopted a GPU-compute-driven simulation architecture for the first time during this rewrite — plausibly because WebGPU's ecosystem/tutorials make the storage-buffer-in/storage-buffer-out compute pattern the natural default, where his old renderer was designed around CPU-side simulation with GPU-side instanced *drawing* only. **[INFERRED]**

### 18. Enemy movement: flow-field pathfinding — [VERIFIED existence; CPU/GPU location OPEN]

Video metadata (verified via YouTube's oEmbed endpoint and page publish timestamps) confirms two devlog videos specifically about this:

| Date | Title |
|---|---|
| 2026-07-16 | "C++ Game Dev, Coding a Flow Field Defense" |
| 2026-07-17 | "C++ Game Dev, Actually Coding a Flow Field TODAY!" |

**Flow-field pathfinding** — a single shared vector field that an entire crowd follows, dramatically cheaper per-agent than individual A* pathfinding — is the confirmed movement model.

Critically, both dates **precede** any WebGPU-titled devlog (the first is July 31; the repo itself is created August 3). This means the flow field was built and running before GPU-compute work began, strongly implying the original flow-field computation was CPU-side on the old OpenGL 4.3 engine. Whether the flow-field computation itself has since moved to the GPU (as opposed to just the per-agent position-integration step reading from it, which the WebGPU demo code's architecture suggests as a natural next step) is **not confirmed anywhere accessible.** **[OPEN]**

No mention was found anywhere (repo code or search) of boids/steering behaviors or spatial-hash/grid systems by name in either public repo. The actual Gothic-specific gameplay and AI source is not public — `SchnitzelMotor`'s `game.cpp` is a generic sample with no `Enemy`, flow-field, or grid references at all.

### 19. How 100,000–200,000 enemies are feasible — [INFERRED from a dated title sequence]

No direct explanation of the technique exists in any accessible source. YouTube's caption/transcript API returned empty on direct request (anti-bot blocked), all 13 relevant video descriptions are identical channel boilerplate (verified by downloading and diffing each page), and no technical write-up surfaced on Reddit, Hacker News, or Twitter/X.

What can be reconstructed is a dated milestone sequence (verified via page `publishDate`/`uploadDate` metadata):

| Date | Title |
|---|---|
| 2026-07-28 | Coding Enemy Behavior \| Gothic Remake Later |
| 2026-07-29 | My Game Is In Trouble \| Gothic Remake Later |
| 2026-07-30 | Making My Game Run In The Browser Again \| Gothic Remake Later |
| 2026-07-31 | Making A Game With WebGPU \| Gothic Remake AFTER |
| 2026-08-03 | Maybe I'm Wrong About AI – Porting to WebGPU *(WebGPU repo created same day)* |
| 2026-08-04 | AI Is Good, But Dangerous |
| 2026-08-05 | Computing 200k Enemies TODAY! |
| 2026-08-06 | Actually 200k Enemies TODAY! |
| 2026-08-07 | The Final DAY, 200k Enemies or NOTHING! |
| 2026-08-10 | Colliding 200k Enemies Today |
| 2026-08-13 | Making 100k Enemies Attack My Base |
| 2026-08-14 | 200k Enemies ✅ Gameplay LOOP NOW! |
| 2026-08-28 | 200k Enemies In The WEB & Are 2020 Games Dead? |

The progression — "Computing" → "Colliding" → "Gameplay LOOP" — reads as incremental milestones: raw GPU position updates first, then collision resolution, then full game-loop integration. "Making 100k Enemies Attack My Base" sitting between "Colliding" and the final "Gameplay LOOP" suggests **100k** is the count at which full AI-and-combat is demonstrated, while **200k** is closer to a raw compute/render stress-test ceiling without full simulation depth. This is an inference from title wording and dates, not a stated fact. **[INFERRED]**

No source confirms LOD, culling, or spatial partitioning by name. The "Colliding 200k Enemies" title is circumstantial evidence that broad-phase collision work happened, but is not proof of a specific spatial-hash or grid approach. The August 28 title ties the 200k milestone explicitly to "the web" — i.e., the WebGPU/Emscripten browser build reaching parity with whatever the native build already demonstrated.

### 20. What could not be accessed — [OPEN, for transparency]

- **YouTube transcripts/captions:** blocked at the API level (empty response), likely an anti-bot token requirement.
- **Video descriptions:** all confirmed identical generic channel boilerplate — no per-video technical content.
- **X/Twitter:** profile metadata (bio, follower count, join date) was reachable via a third-party mirror API, but no way to search or list his tweet timeline for a technical thread; general web search surfaced only unrelated older tweets.
- **Direct YouTube channel browsing / on-page transcript UI:** not available in this research session (no browser automation connected).
- The actual Gothic Remake gameplay/AI source code is not public in any repository found.

---

## Part 3 — Sources

**Primary (source code, fetched and read directly):**
- `github.com/Cakez77/SchnitzelMotor` — `src/gl_renderer.cpp`, `src/win32_platform.cpp`, `src/schnitzel_lib.h`, `src/main.cpp`, `src/sound.h`, `src/ui.h`, `src/input.h`, `src/assets.h`, `src/game.cpp`, `build.sh`, README
- `github.com/Cakez77/WebGPU` — `src/main.c`, `src/web_gpu_renderer.c`, `src/lib.h`, README (created 2026-08-03)
- `github.com/Cakez77/CelesteClone`, `github.com/Cakez77/VampireSurvivors`, `github.com/Cakez77/gothic-remake` (dead end, 2020 Unity project)
- `github.com/Cakez77/CakezTD-Beta` — asset folder listing only (no source)

**Primary (his own words):**
- `cakez77.itch.io/cakeztd/devlog` — four posts (05.07.2022, 14.07.2022, 13.10.2022 "Engine Rework," 31.01.2023 "Big Update")

**Secondary / metadata:**
- `store.steampowered.com/app/2245620/Tangy_TD` and `store.steampowered.com/developer/cakez`
- YouTube oEmbed endpoint and page `publishDate`/`uploadDate` JSON-LD metadata for video titles and dates
- 80.lv and mein-mmo.de press coverage of Tangy TD's commercial performance
- `x.com/Cakez77` via a third-party mirror, for profile metadata only

**Not accessible:** YouTube captions/transcripts, per-video descriptions beyond boilerplate, his X/Twitter timeline, direct browser automation of his channel.

---

## Appendix — Confidence summary

| Area | Confidence |
|---|---|
| Language = C/C++, no ECS, OpenGL rendering (old engine) | **VERIFIED** |
| Bump-allocator memory model, DLL hot reload, hand-rolled UI | **VERIFIED** |
| Tangy TD identity, release date, tower-defense concept | **VERIFIED** |
| Tangy TD sales figures | **SECONDARY** |
| "Gothic Remake" = his own project, distinct from THQ Nordic's | **VERIFIED** |
| WebGPU repo's compute-shader + instanced-render pipeline | **VERIFIED** |
| Flow-field pathfinding as the movement model | **VERIFIED (existence)** |
| Flow field running on CPU vs. GPU currently | **OPEN** |
| Mechanism enabling 100k–200k simultaneous enemies | **INFERRED** |
| LOD / spatial partitioning / culling specifics | **OPEN — no source found** |
| Rationale for choosing C++ originally | **OPEN** |
| Solo vs. team on the new project | **OPEN — no explicit statement found** |
