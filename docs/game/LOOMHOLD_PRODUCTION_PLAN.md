# Loomhold Production Plan

Status: pre-production baseline  
Working title: **Loomhold**  
Genre: single-player PvE action roguelite wave-defence extraction shooter  
Technology: IC_2DE, C++20, raylib, EnTT, Box2D  
Primary platform: Windows 10/11 x64  
Presentation: pixel-art sprites in a true X/Y/Z world with an oblique orthographic camera  
Planning assumption: one developer working part-time; milestone gates outrank calendar estimates

## 1. Executive decision

Loomhold will be the reference game that directs IC_2DE development. It combines fast twin-stick combat, short scavenging windows, escalating defensive waves, voluntary extraction, and persistent hub progression.

The game is deliberately smaller than a traditional open-map extraction shooter. Each raid uses a compact, handcrafted arena with seeded variation in enemies, rewards, events, defence positions, and extraction conditions. The player chooses when to stop defending and escape. Staying longer raises both the reward tier and the chance of losing the raid inventory.

The production goal is **AAA discipline at focused independent-game scope**: stable performance, responsive controls, readable combat, safe persistence, strong content iteration, measurable quality gates, and a polished vertical slice. It is not an attempt to match the content volume, networking, or team size of a commercial AAA extraction shooter.

## 2. Product vision

### One-sentence pitch

Enter an unraveling patchwork world, activate a Loom Anchor, survive escalating enemy waves, build a temporary defence, and decide how much recovered thread to risk before fighting your way to extraction.

### Player fantasy

The player is a small stitched scavenger who survives through movement, preparation, improvised weapons, and clever use of the arena. They are not an invincible soldier. Each successful extraction should feel earned, and each decision to stay for another wave should create visible tension.

### Target experience

- Session length: 8–15 minutes for the first release target.
- First meaningful combat: within 60 seconds of deployment.
- First extraction decision: within 5–7 minutes.
- Controls: keyboard/mouse and Xbox-compatible controller.
- Camera: current oblique 2.5D camera, readable at 640 x 360 internal resolution.
- Tone: colourful patchwork fantasy with unsettling corruption rather than realistic military imagery.
- Rating target: stylized fantasy violence without graphic gore.

## 3. Design pillars

### 3.1 Movement is survival

Combat must reward positioning, dodge timing, line-of-sight use, and reading enemy intent. Movement remains responsive at every presentation rate and is never coupled to render FPS.

### 3.2 Greed creates the story

The most important decision is not which enemy to shoot; it is whether to extract now or risk one more wave. Rewards, threat, objective damage, ammunition, and route safety must make that decision legible.

### 3.3 Build a defence, not a fortress

Defensive tools use authored sockets and limited resources. The player chooses meaningful placements without turning the game into unrestricted base construction or a menu-heavy tower defence game.

### 3.4 Runs vary; rules stay trustworthy

Seeds vary waves, reward offers, elite modifiers, events, and selected arena placements. Enemy telegraphs, extraction rules, damage rules, and save outcomes remain predictable and fair.

### 3.5 Extraction earns persistence

Raid loot becomes permanent only through a successful extraction. Permanent progression expands available choices, facilities, and item pools more than it increases raw numerical power.

### 3.6 Presentation supports decisions

Animation, hit feedback, camera response, particles, lighting, UI, and later audio must communicate gameplay state. Presentation systems consume copied gameplay events and never change authoritative simulation outcomes.

## 4. Scope boundaries

### Required for the first vertical slice

- One hub room.
- One forest arena with three seeded layout variants.
- Five normal waves and one optional elite finale.
- One primary weapon, one secondary weapon, one melee fallback, and one dodge.
- Three normal enemy archetypes and one elite modifier.
- One Loom Anchor defence objective.
- Two socket-based defence types.
- A raid backpack and persistent stash.
- Six temporary perks.
- Two extraction opportunities.
- Success, death, debrief, save, reload, and repeat.
- Keyboard/mouse and controller support.
- A packaged Debug build, packaged Shipping build, and deterministic automated route.

### Candidate v0.1 content after the vertical slice proves the loop

- Two regions with distinct arena rules.
- Six arena layouts assembled from authored variants.
- Eight weapons across at least four behaviour families.
- Eight normal enemy archetypes, three elite modifiers, and two bosses.
- Twenty temporary perks.
- Three defensive structures and two utility stations.
- Ten contract templates.
- Hub facility progression and rescued NPCs.
- Difficulty modifiers and an endless holdout option.

These are capacity targets, not promises. Content expands only after the vertical-slice retention, performance, and production-cost gates pass.

### Explicitly deferred

- Multiplayer, replication, servers, anti-cheat, and PvP.
- Open-world streaming.
- Unrestricted free-placement construction.
- Fully destructible terrain.
- Procedurally generated art or arbitrary procedural geometry.
- Scripting until two measured gameplay-authoring problems justify it.
- Editor camera, transform gizmos, and broad editor expansion.
- 2D lighting until the combat silhouette and performance baseline are stable.
- Audio implementation by current owner direction; event seams remain ready for it later.
- Consoles, Linux release support, and additional graphics backends.

## 5. World and theme

### Setting

Reality is made from living thread. Protected settlements survive around enormous dormant looms, while surrounding regions slowly unravel into hostile fragments. Scavengers deploy portable Loom Anchors to recover stable thread, memories, machine parts, and living relics.

Activating an Anchor creates a strong signal. Corrupted creatures converge on it, the region destabilizes, and extraction gates become unreliable. The player must decide how long the recovery operation remains worth defending.

### Visual language

- Characters and creatures are visibly stitched, repaired, or assembled.
- Weapons combine sewing tools, salvaged machinery, wood, bone, and luminous thread.
- Healthy spaces use warm greens, cloth textures, and rounded silhouettes.
- Corruption introduces missing pixels, broken outlines, tangled growth, and hostile colour accents.
- Extraction gates and safe technology use clear gold/cyan signals.
- Enemy attacks use consistent warning colours and silhouettes independent of biome palette.

### Narrative delivery

The first version uses environmental storytelling, short NPC conversations, item descriptions, and post-raid discoveries. Long cinematic sequences and branching narrative systems are not required for the vertical slice.

## 6. Core raid loop

```text
Hub
  -> Loadout
  -> Deployment
  -> Scavenge
  -> Activate Loom Anchor
  -> Defend Wave
  -> Intermission: repair / buy / choose perk / extract or continue
  -> Extraction Assault or Next Wave
  -> Success or Death
  -> Debrief
  -> Commit or discard raid changes
  -> Hub
```

### 6.1 Hub

The hub is a safe scene and the owner of permanent progression presentation. The player can inspect the stash, choose a loadout, accept a contract, upgrade a facility, review discoveries, and deploy.

The first hub is intentionally small: stash, loadout station, contract board, Loom Gate, and one NPC. Every station must justify a player action; decorative expansion waits until the loop works.

### 6.2 Loadout

The player chooses:

- one primary weapon;
- one secondary or utility tool;
- one defensive structure blueprint;
- limited healing and ammunition;
- one insured or secured item slot, if the final loss model retains it.

Loadout validation prevents impossible combinations before deployment. Starting equipment always includes a viable fallback so a failed run cannot permanently brick the profile.

### 6.3 Deployment and scavenge

The player enters one of several authored spawn locations. A short 60–90 second scavenge phase allows them to inspect the arena, open nearby containers, activate one optional utility node, and choose a defence approach.

The player can activate the Loom Anchor early. Waiting creates preparation opportunities but should not support indefinite farming.

### 6.4 Defence wave

The Wave Director spends a deterministic threat budget on enemy groups valid for the arena and wave tier. Spawns are telegraphed, avoid the immediate player view where appropriate, respect cooldowns, and never exceed active-enemy budgets.

The wave ends when its required groups are defeated or its authored objective resolves. Hidden or unreachable enemies must not soft-lock progression; recovery rules relocate or retire invalid actors after a measured timeout.

### 6.5 Intermission

Intermissions are short decision windows, not safe pauses without pressure. The player may:

- repair the Loom Anchor;
- replenish limited ammunition;
- spend temporary scrap on a defence socket;
- choose one of three temporary perks at specified wave milestones;
- reorganize the backpack;
- activate extraction when available;
- continue to the next wave.

### 6.6 Extraction assault

Extraction is an active final encounter. Starting it commits the player to an extraction gate and begins a visible countdown. Remaining enemies stay active, a final group may spawn, and the route from the Anchor to the gate matters.

Success requires the player to be alive inside the extraction area when the gate completes. The defence objective may be abandoned once it has produced the minimum extractable reward, unless a contract says otherwise.

### 6.7 Debrief and persistence

The debrief presents:

- extracted and lost items;
- secured items;
- contract results;
- highest wave;
- damage dealt and taken;
- objective health remaining;
- elapsed raid time;
- unlocks and hub changes.

Only then does a successful raid transaction enter permanent storage. A failed or interrupted write leaves the previous valid profile intact.

## 7. Wave, threat, and extraction design

### Baseline five-wave slice

| Stage | Gameplay purpose | Reward state | Extraction state |
|---|---|---|---|
| Scavenge | Read arena and collect basics | Starting supplies | Closed |
| Wave 1 | Teach normal enemy pressure | Common resource | Closed |
| Wave 2 | Combine two enemy roles | Common item plus scrap | Closed |
| Wave 3 | First serious resource check | Uncommon reward and perk | Safe extraction opens |
| Wave 4 | Add elite modifier or hazard | Higher rarity roll | Extraction assault strengthens |
| Wave 5 | Test full build and objective health | Rare reward | Final standard extraction |
| Optional finale | Voluntary mastery challenge | Unique unlock chance | Emergency extraction only |

### Threat rules

- Threat rises by completed wave, time over target, elite modifiers, and optional objectives.
- Threat never silently changes enemy damage during an attack already in progress.
- Spawn composition changes difficulty more often than raw health inflation.
- The director respects authored minimum and maximum counts for each role.
- Every enemy wave is reproducible from raid seed plus wave index.
- The same simulation seed and command stream produces the same authoritative result on the same build/platform.

### Extraction rules

- First extraction opportunity follows wave three in the vertical slice.
- The normal countdown starts provisionally at 18 seconds.
- The active gate and its countdown are always visible in the HUD.
- Damage does not reset the countdown by default; leaving the zone pauses it.
- Downed states are deferred. Zero player health resolves the raid as death.
- Disconnect recovery is irrelevant until networking exists, but application interruption must never create duplicated loot.

## 8. Combat design

### Controls

| Action | Keyboard/mouse | Controller |
|---|---|---|
| Move | WASD | Left stick |
| Aim | Mouse world position | Right stick |
| Fire | Left mouse | Right trigger |
| Secondary / utility | Right mouse | Left trigger |
| Dodge | Space | Face button |
| Reload | R | Face button |
| Interact | E | Face button |
| Swap weapon | Q / wheel | Shoulder button |
| Pause | Escape | Menu button |

The player keeps eight-direction locomotion presentation. Aim is independent from movement. The first implementation can quantize weapon presentation to eight directions while simulation uses a normalized continuous X/Z aim vector. A later weapon-overlay sprite can remove the need to author complete body frames for every weapon.

### Combat rules

- Authoritative combat runs at the fixed 60 Hz tick.
- Projectiles move in world X/Z and carry an elevation band or Y tolerance.
- Damage is event-driven and cannot be applied twice by one hit instance.
- Dodge grants a short, explicitly timed invulnerability window and movement impulse.
- Reloading is interruptible only by defined actions.
- Player and enemy attacks have data-authored cooldown, damage, impulse, range, spread, and tags.
- Friendly fire is disabled in the initial single-player build.
- Enemy telegraphs remain readable at the minimum supported output.

### First weapon families

- Needle pistol: accurate, reliable, low burst damage.
- Scissor shotgun: short range, multiple projectiles, strong knockback.
- Spool launcher: slow projectile that pierces or tethers targets.
- Pin rifle: deliberate long-range shot with high weak-point reward.

Only the first two are required for the vertical slice.

## 9. Enemy and defence design

### Vertical-slice enemy roles

| Enemy | Role | Behaviour test |
|---|---|---|
| Runner | Player pressure | Chases, telegraphs lunge, can be dodged |
| Spitter | Ranged displacement | Maintains range and creates avoidable projectiles |
| Saboteur | Objective pressure | Prioritizes Loom Anchor and forces target switching |
| Elite modifier | Build disruption | Adds one readable modifier to a normal archetype |

Enemy logic starts with focused state machines: spawn, acquire, approach, attack, recover, stagger, die. A general behaviour tree, EQS, or ability framework is unnecessary until real enemy variety proves those state machines insufficient.

### Socket-based defences

- Barricade: blocks or redirects selected enemy paths and has repairable health.
- Thread turret: automatically attacks valid targets within a visible range.
- Later candidates: slow field, decoy, ammunition station, healing pulse.

Sockets are authored into arenas. Placement is fast, controller-friendly, deterministic, and compatible with the existing scene/prefab model. Defence ownership, cost, health, upgrade tier, and refund rules are explicit data.

## 10. Items, economy, and progression

### Inventory layers

| Inventory | Lifetime | Loss rule |
|---|---|---|
| Loadout | Selected before raid | At risk according to equipment-loss rule |
| Backpack | Current raid only | Lost on death unless secured |
| Temporary scrap | Current raid only | Spent on defences and intermission services |
| Stash | Persistent profile | Changes only through a committed transaction |
| Unlock catalog | Persistent profile | Expands possible drops, perks, and facilities |

### Economy principles

- Every item has a stable definition ID; item instances carry only necessary mutable state.
- Rarity changes behaviour combinations and opportunity, not only damage numbers.
- Starter kits are always available.
- The player should recover from a failed run within one successful low-tier extraction.
- Selling, crafting, repair, and insurance are deferred until the base acquisition/loss loop is fun.
- Duplicate protection and pity systems are considered only after loot data shows a real frustration pattern.

### Roguelite progression

Temporary perks apply only during one raid. Permanent progression unlocks weapon families, perk candidates, defence blueprints, contracts, and hub facilities. Permanent flat damage upgrades remain limited to avoid invalidating extraction risk and enemy readability.

## 11. Domain model

The following terms are canonical. Code, data, tests, telemetry, and design discussions should use them consistently.

| Term | Meaning and invariant |
|---|---|
| Profile | The last valid persistent player state. Never contains uncommitted raid loot. |
| Raid | One deployment from loadout acceptance through success or death. Has one immutable seed. |
| Raid phase | One value in the legal RaidSession transition graph. |
| Raid inventory | All mutable item instances carried during the active raid. |
| Stash | Permanent extracted item storage owned by the Profile. |
| Secured slot | Optional limited raid storage retained under the chosen death rule. |
| Loom Anchor | Defence objective that produces rewards and enables progression. Exactly one is active in the first mode. |
| Wave | A deterministic threat-budget plan with completion conditions. |
| Spawn zone | Authored arena location and constraints from which a SpawnPlan may create enemies. |
| Extraction gate | Authored exit that can become active and resolve a successful raid. |
| Threat | Director input representing pressure; not a direct hidden damage multiplier. |
| Temporary scrap | Raid-only currency for defence and intermission choices. |
| Perk | Temporary rule modifier selected during a raid. |
| Unlock | Persistent permission for content to appear; not necessarily an owned item. |
| Raid result | Immutable success/death outcome used to calculate one profile transaction. |
| Profile transaction | Atomic set of stash, unlock, contract, and facility changes derived from a Raid result. |

## 12. Architecture and ownership

Gameplay belongs in game-owned modules. IC_2DE gains a generic engine module only when the feature has reusable mechanics, a clear seam, and a reference-game requirement. The application loop must not accumulate raid, item, enemy, weapon, or extraction IDs.

### 12.1 Game-owned deep modules

| Module | Small interface | Complexity hidden in implementation |
|---|---|---|
| RaidSession | start, submit command, advance fixed tick, read snapshot, resolve result | legal phase transitions, timers, extraction rules, wave/intermission coordination, terminal outcome |
| WaveDirector | plan next wave from seed, tier, arena facts, and threat | deterministic weighted selection, budgets, role limits, spawn-zone suitability, recovery rules |
| Combat | submit actions, advance fixed tick, consume events | weapons, projectiles, cooldowns, reloads, dodge windows, damage deduplication, death |
| EnemyRoster | spawn archetype, advance intent, consume combat facts | focused state machines, target selection, navigation requests, stagger and death lifecycle |
| RaidInventory | apply inventory command, query view, produce extraction manifest | stack rules, capacity, equipment, secured slots, loot identity, validation |
| Progression | evaluate RaidResult against Profile | rewards, contracts, unlocks, facility progression, recovery kit rules |
| Defence | build/repair at socket, advance fixed tick, consume events | costs, placement validation, target selection, health, upgrades, refunds |
| GameCatalog | load and resolve stable definition IDs | item, weapon, enemy, perk, wave, contract, defence schemas and cross-reference validation |
| HudModel | build immutable HUD snapshot | prioritization and formatting of raid, combat, objective, wave, extraction, and inventory state |

Tests exercise each module through the same interface used by the game. Internal algorithms remain private and can change without forcing caller or test rewrites.

### 12.2 Engine extensions justified by the game

| Engine module | Required extension | First concrete consumer |
|---|---|---|
| Input | independent aim action, fire/reload/dodge/interact actions, rebinding-ready definitions | Combat and loadout UI |
| Physics2D | ray/shape queries and elevation-aware hit filtering behind engine-owned result types | Projectiles, enemy line of sight, defence targeting |
| World | additional authored gameplay traits and batch queries without exposing EnTT | EnemyRoster, Combat, loot pickups |
| GameplayState | validate copied completed-tick snapshots and return a schema-versioned digest | replay verification, editor telemetry, future raid-state checks |
| Scene | authored spawn zones, defence sockets, extraction gates, and raid role records | Raid arena construction |
| Persistence | versioned Profile schema, atomic save, backup recovery | Stash and Progression |
| Render2D | projectile, effect, and weapon-overlay submissions through existing frame description | Combat feedback |
| Assets | data catalogs, development file watching, safe reload where proven useful | Weapon/enemy/perk iteration |
| UI | lightweight shipped-game UI with focus and controller navigation | HUD, loadout, debrief, settings |
| Particles | pooled, event-driven 2.5D effects | hits, death, extraction, objective damage |

### 12.3 Persistence seam

Persistence has two real adapters:

- a filesystem adapter for production using atomic sibling-temporary replacement and backup recovery;
- an in-memory adapter for tests and deterministic automated raids.

The Profile module owns validation and transaction rules. Callers never edit save JSON or filesystem paths directly.

### 12.4 Event flow

```text
Input actions
  -> gameplay commands
  -> RaidSession / Combat / Defence / EnemyRoster fixed update
  -> copied gameplay events
  -> Animation / HUD / Particles / Camera / later Audio
  -> immutable render frame
```

Physics and animation adapters continue to emit copied engine-owned results. Gameplay never runs inside Box2D callbacks, renderer submission, or file-watch callbacks.

### 12.5 CPU and GPU work

- The fixed simulation and World structural mutation remain main-thread authoritative.
- Wave-plan generation may run as a pure job from immutable arena facts, but its result is applied on the fixed thread.
- Data parsing, file hashing, image decoding, and future asset preparation may use worker jobs.
- raylib/OpenGL resource creation, replacement, and submission stay on the graphics thread.
- Optimization work requires measured p50/p95/p99 evidence and a representative stress scenario.

## 13. Data strategy

The game should add versioned, strict schemas rather than hard-code catalogs into the application loop.

### Initial authored definitions

- `weapon`: stable ID, presentation asset, fire mode, projectile/hitscan rule, damage, cadence, ammunition, reload, spread, impulse, tags.
- `enemy`: stable ID, sprite/animation set, health, movement, collision, perception, attack set, role tags, director cost.
- `item`: stable ID, stack/capacity rule, rarity, value, world presentation, use/equipment link.
- `perk`: stable ID, eligibility tags, mutually exclusive tags, rule modifier and presentation text ID.
- `wave_set`: tier, budget, role limits, candidate groups, extraction unlock and reward tier.
- `arena_roles`: spawn zones, extraction gates, Loom Anchor, defence sockets, loot points, navigation hints.
- `contract`: objective conditions, validation, reward, failure state.
- `profile`: schema, stash, unlocks, facilities, settings reference, last safe hub state.

Unknown IDs, duplicate definitions, invalid ranges, impossible cross-references, unsafe paths, and unsupported schema versions fail with actionable file/line diagnostics before a raid starts.

## 14. Production roadmap

Estimates are provisional part-time development ranges. A milestone does not finish because its dates elapsed; it finishes when its automated and manual gates pass and a runnable checkpoint is packaged.

### Milestone G0 — Game charter lock (2–4 days)

Player-visible outcome: a final greybox ruleset everyone can describe consistently.

Deliver:

- confirm title direction, tone, loss model, raid length, extraction timing, and permanent-progression philosophy;
- update the engine charter to name Loomhold as the reference game;
- define input actions and the vertical-slice content budget;
- create a greybox arena diagram and encounter timing sheet.

Gate:

- no unresolved decision changes the shape of RaidSession, RaidInventory, or Combat;
- every planned engine extension names a visible game requirement;
- deferred features are recorded explicitly.

### Milestone G1 — First shot and dodge (1–2 weeks)

Player-visible outcome: the existing character can aim independently, fire, reload, dodge, damage a target, and receive readable feedback.

Deliver:

- action-mapped aim/fire/reload/dodge input;
- Combat module with one projectile weapon;
- health, hit identity, damage, death, reload, and dodge invulnerability;
- one target dummy and one moving attacker;
- temporary debug HUD for weapon and health state.

Automated gate:

- fixed-tick projectile travel and damage are identical across presentation modes;
- one hit cannot apply damage twice;
- dodge windows and reload transitions pass edge-case tests;
- no raylib or Box2D type crosses the Combat interface.

Manual gate:

- ten minutes of movement, aim, firing, reload, and dodge feel responsive on mouse and controller;
- attacks remain legible at 640 x 360.

### Milestone G2 — First deterministic wave (1–2 weeks)

Player-visible outcome: activate the Loom Anchor and survive one complete enemy wave.

Deliver:

- authored spawn zones and Loom Anchor role;
- WaveDirector with seeded budget planning;
- Runner, Spitter, and Saboteur greybox archetypes;
- spawn telegraphs and wave completion recovery;
- objective health and failure.

Automated gate:

- the same seed produces the same SpawnPlan and terminal state;
- role limits and active-enemy caps hold across 1,000 generated plans;
- invalid/blocked spawn zones do not soft-lock completion;
- 30/60/120/monitor/uncapped presentation yields one replay hash.

Manual gate:

- enemy roles are distinguishable without reading labels;
- no spawn feels unavoidable or visually unfair.

### Milestone G3 — Raid lifecycle and extraction (1–2 weeks)

Player-visible outcome: deploy, scavenge, defend three waves, choose extraction, survive the countdown, and reach a debrief—or die.

Deliver:

- RaidSession legal phase graph;
- intermission and continue/extract decision;
- extraction gate activation, pause-on-leave countdown, and assault;
- immutable RaidResult and debrief read model;
- reset/retry without restarting the executable.

Automated gate:

- illegal phase commands are rejected without mutating state;
- success and death are terminal and resolve once;
- reset starts a clean raid with the requested seed;
- automated success and death routes both complete without leaks.

Manual gate:

- the player always understands the current phase and next available action;
- extraction creates a final tension spike rather than passive waiting.

### Milestone G4 — Loot, stash, and safe persistence (2 weeks)

Player-visible outcome: pick up raid loot, extract it into the stash, lose unextracted loot on death, close the game, and reload the same valid profile.

Deliver:

- GameCatalog item definitions;
- RaidInventory capacity, equipment, pickup, drop, and secured-slot rules;
- Profile, Stash, Progression, and profile transaction;
- production filesystem and in-memory persistence adapters;
- versioned profile schema, backup, corruption diagnostics, and atomic save.

Automated gate:

- success commits exactly once; death cannot commit backpack loot;
- interrupted/failed writes preserve the previous valid profile;
- malformed profiles fail safely or recover from a valid backup;
- 100 save/load cycles retain stable IDs and item counts;
- property tests preserve inventory capacity and uniqueness invariants.

Manual gate:

- debrief makes every kept and lost item understandable;
- a failed raid does not leave the player without a viable next loadout.

### Milestone G5 — Five-wave roguelite loop (2 weeks)

Player-visible outcome: complete a five-wave run with temporary perks, escalating rewards, and two extraction opportunities.

Deliver:

- six temporary perks with eligibility/conflict validation;
- intermission reward choice;
- five authored wave tiers plus optional elite finale;
- threat display and reward preview;
- second weapon family and weapon swap;
- preliminary economy tuning table.

Automated gate:

- seeded perk offers and waves reproduce exactly;
- incompatible perks never appear together;
- temporary perks cannot enter the permanent Profile;
- weapon state remains valid through swap, reload, dodge, extraction, and reset.

Manual gate:

- wave three presents a genuine extract/continue decision;
- at least three meaningfully different builds are viable.

### Milestone G6 — Defence strategy and arena variation (2–3 weeks)

Player-visible outcome: choose and repair socket-based barricades/turrets across three arena variants.

Deliver:

- authored defence sockets;
- barricade and turret definitions;
- Defence module with spend/build/repair/destroy lifecycle;
- three seeded variants of the forest arena;
- enemy navigation response to defence state;
- off-screen and unreachable-enemy safeguards.

Automated gate:

- building never creates an invalid cost or duplicate socket occupant;
- defences reset fully between raids;
- enemy route recovery completes under blocked-path fixtures;
- representative maximum entities/projectiles/defences stay within the simulation budget.

Manual gate:

- placement decisions change tactics without creating one dominant mandatory setup;
- the player spends more time fighting and deciding than navigating build menus.

### Milestone G7 — Hub and complete vertical slice (3–4 weeks)

Player-visible outcome: launch the packaged game, prepare in a polished hub, complete or fail a raid, review the debrief, upgrade/unlock something, and repeat.

Deliver:

- shipped-game HUD, loadout, stash, debrief, pause, and settings UI;
- controller navigation and safe-area layout;
- hub stations and one NPC;
- representative final art for one arena and required actors;
- particles, camera shake, hit-stop, damage flashes, and extraction presentation;
- accessibility baseline: remapping-ready actions, aim sensitivity, shake intensity, flash reduction, readable text scale;
- one beginning state and one vertical-slice completion state.

Automated gate:

- package includes only declared runtime data;
- UI navigation has no focus traps in scripted routes;
- 100 raid restart loops show no engine-owned resource growth;
- profile compatibility and migration fixtures pass;
- Debug and Release suites, five-mode replay, and Shipping GPU smoke pass.

Manual gate:

- five external players can launch, understand the objective, extract or die, and start another raid without developer instruction;
- critical defects and save-loss defects are zero;
- player-visible frame pacing stays within budget on representative minimum hardware.

### Milestone G8 — Hardening and v0.1 decision (2–3 weeks)

Player-visible outcome: a stable, distributable build suitable for repeat playtesting.

Deliver:

- 60-minute soak and repeated raid-transition tests;
- performance capture on minimum hardware;
- difficulty/economy tuning from playtest evidence;
- crash diagnostics, release notes, licences, and clean-machine launch check;
- prioritized decision on whether to expand content, add audio, add lighting, deepen editor tooling, or revise the loop.

Gate:

- one tagged revision builds, tests, packages, launches cleanly, and completes the soak;
- no known critical crash, save corruption, item duplication, or terminal raid soft-lock;
- content-production cost supports the proposed v0.1 scope.

## 15. Immediate implementation backlog

The first development sprint should end with a packaged combat checkpoint, not architecture alone.

### Current execution status - 2026-09-01

The editor-first production prerequisite is complete: loaded file-backed bitmap textures can be replaced live while the editor is open. Stable handles, last-good fallback, revision telemetry, and a real GPU probe make character, enemy, effect, and environment iteration safer during G1. Generated textures, Aseprite metadata, shaders, scenes, and C++ code do not hot-swap yet; those paths will be added only when a production step demonstrates the need.

Tasks 2, 3, 4, 5, and 8 are complete. The first parts of tasks 6, 7, and 9 are also complete. Held LMB state produces cooldown-authoritative shots independently of render rate; projectile impacts feed stable hit identities into `Health`; three needle hits retire the 54-health target dummy exactly once. Combat-owned dodge direction drives exactly 12 fixed ticks of collision-resolved movement at 3x authored speed. The first moving attacker is complete as a narrow vertical slice: schema 9 authors a separate kinematic Threadbound Runner; `EnemyIntent` owns fixed-tick acquisition, pursue/attack choice, range, cooldown, copied requests, reset, and canonical snapshots; `NavAgentSystem` owns bounded A* route lifetime and cell-centre motion; `RuntimeScene` owns generic collision-resolved actor motion and locomotion presentation; and existing Combat/Health seams resolve dodge invulnerability and player damage. Immutable `NavGrid` data is baked from GroundMap bounds, solids, elevation, max-step policy, and attacker footprint clearance, then deterministic A* returns explicit copied status/path results using octile distance and stable tie-breaking. The test area produces a 64 x 46 grid with 2,930 walkable and 14 hard-blocked cells; its standalone obstruction route contains seven cells, costs 136.57 world units, and expands eight cells. The Runner now consumes its own copied route, replans immediately for meaningful route changes, refreshes unchanged routes after 30 ticks, and advances through cell centres with a four-world-unit tolerance. Separate opt-in grid/path overlays and read-only Statistics show the active route and aggregate agent counters. `Debug > Enemy stress test` now rebuilds an unsaved runtime copy with 10, 25, 50, 100, or 200 total real Runners placed on unique player-reachable cells; restoring the authored scene removes them. Each copy owns stable runtime identities and participates in physics, animation, Health, EnemyIntent, NavAgent, collision, and rendering. Stress actors keep acquisition, pursuit, attack state, and attack-request work active while their final player-damage hand-off is suppressed; the Debug menu and Gameplay Statistics show this mode, and authored-scene restore returns damage to normal. The former viewport wall of debug text remains replaced with a compact HUD and collapsed per-actor Statistics details. A 50-Runner packaged GPU route completes 90 fixed ticks with 50 agents, 153 bounded searches, 117 waypoint advances, and zero player damage. `GameplayState` digest schema v3 includes future-affecting navigation state. A combined 180-tick route exercises aim, held fire, three impacts, one death, one 78-unit dodge, navigation-driven attacker pursuit, and player damage; 30, 60, 120, monitor-synced, and uncapped Release runs all finish at digest `17194250339899414140`. Debug and Release pass 27/27 tests, and the relocatable editor package passes hot-swap, dodge, combined gameplay, moving-attacker, navigation-grid, navigation-path, Runner-path, enemy-stress, and layout GPU probes. Roll presentation, impact animation/VFX, authored combat catalogs, a dedicated wall-separated runtime path fixture, measured 50/100/200 frame-time baselines, and Shipping validation remain. No behavior tree, general AI framework, wave director, advanced navigation algorithm, or GPU crowd simulation has been introduced.

### Sprint objective

The player can move, aim, fire a visible projectile, damage and kill a target dummy, reload, and dodge. The result remains deterministic across render modes.

### Ordered tasks

1. Record the G0 owner decisions listed in Section 21.
2. **Complete 2026-08-31:** add logical actions for aim, fire, reload, dodge, interact, weapon swap, and extraction choice, with keyboard/mouse and Xbox-compatible mappings plus editor telemetry.
3. **Complete 2026-09-01:** define `CombatCommand`, `CombatEvent`, `CombatSnapshot`, the minimal Combat interface, viewport-aware world aim, and fixed-tick editor telemetry.
4. **Complete 2026-09-01:** reload, cooldown, held fire, release-plus-mouse-motion ordering, aim-facing hysteresis, projectile travel/lifetime, segment collision, owner rejection, hit-identity deduplication, health reduction, exactly-once death, dodge duration/invulnerability, frozen direction, cooldown rejection/readiness, swept solid contact, reset, and canonical gameplay-digest tests pass.
5. **Complete 2026-09-01:** add the narrow nearest-solid Physics2D segment query with copied engine types, category/mask filtering, sensor rejection, and ignored-body identity; Box2D remains private.
6. **Partial 2026-09-01:** needle-pistol state, visible projectile simulation, collision, impact, target health, target-dummy death/retirement, authoritative direction-carrying dodge movement, one acquire/pursue/attack enemy, immutable NavGrid, deterministic A*, and bounded Runner path consumption are implemented; dodge/attack presentation and a wall-separated runtime fixture remain.
7. **Partial 2026-09-01:** the layered pixel bolt, trail, muzzle flash, firing crosshair, target/attacker health bars, death disappearance, dodge HUD, enemy-intent HUD, focused navigation overlays, compact viewport HUD, and detailed docked telemetry provide temporary feedback; roll presentation, impact/attack animation, and gameplay VFX remain.
8. **Complete 2026-09-01:** automated projectile, held-fire, run-and-gun, crate-impact, three-hit target-death, exact-distance dodge, dedicated moving-attacker, Runner-path, and combined gameplay routes exist; the combined route reports one canonical schema-v3 digest.
9. **Partial 2026-09-01:** Debug and Release suites pass 27/27, asset validation passes, all five presentation modes produce the same combined gameplay digest, and the packaged editor passes hot-swap, dodge, combined, moving-attacker, navigation-grid, navigation-path, Runner-path, 50-Runner enemy-stress, and layout GPU routes; Shipping validation remains a later gate.
10. **Complete for each current slice:** produce a checkpoint note containing working, measured, broken/deferred, learned, and next.

## 16. Performance and capacity budgets

These are provisional gates for the representative forest arena and must be measured before content expands.

| Area | Vertical-slice target |
|---|---|
| Frame pacing | 60 FPS minimum; inspect p50/p95/p99 |
| Total frame budget | 16.67 ms at 60 Hz |
| CPU/GPU split | no more than 8 ms CPU and 8 ms GPU in representative stress capture |
| Fixed simulation | p95 no more than 4 ms in the vertical-slice stress raid |
| Active enemies | 60 normal-equivalent actors provisional; no unbounded spawning |
| Active gameplay projectiles | 300 provisional |
| Active loot/world pickups | 100 provisional |
| Visible pooled effects | 1,000 provisional particles/elements after the particle module exists |
| Spawn work | budgeted across ticks; no visible frame hitch at wave start |
| Startup | first interactive screen in under 3 seconds on minimum PC |
| Save commit | target under 50 ms locally; previous valid profile always recoverable |
| Memory | stable after 100 raid resets and 100 hub/raid transitions |
| Shutdown | zero engine-owned texture, target, body, or entity leaks |

The RTX 2080 Ti development machine is not acceptable as minimum-hardware evidence. A GTX 1050-class or equivalent target remains provisional until physically tested.

## 17. Quality and verification strategy

### Unit and property tests

- RaidSession legal/illegal transitions and terminal outcomes.
- WaveDirector determinism, budgets, role limits, and seed variation.
- Combat timing, hit identity, damage, reload, dodge, death, and reset.
- RaidInventory capacity, stacks, equipment, secured slots, and item uniqueness.
- Progression/Profile transaction success, death, and idempotency.
- Save migration, interruption, corruption, backup, and atomic replacement.
- Catalog validation and cross-reference diagnostics.
- Defence socket ownership, costs, repair, destruction, and reset.

### Headless integration tests

- Complete successful raid from seed and command replay.
- Complete failed raid and verify no extracted loot enters Profile.
- Repeat identical run and compare authoritative state hash.
- Run 1,000 WaveDirector plans for invariant violations.
- Run 100 hub/raid/debrief transitions and inspect lifetime counters.
- Load old profile fixtures through explicit migrations.

### Runtime GPU checks

- Actual projectile, enemy, objective, loot, extraction, and HUD rendering.
- Window resize and aspect-ratio presentation.
- Post-process on/off equivalence of authoritative state.
- 30, 60, 120, monitor-synced, and uncapped replay.
- Packaged Debug, Editor, and Shipping launches from the combined folder.

### Manual play checks

- Mouse and controller aim feel.
- Dodge responsiveness and readability.
- Spawn fairness and attack telegraphs.
- Wave pacing and intermission length.
- Extract/continue decision quality.
- Loot readability and debrief comprehension.
- Recovery after failed runs.
- Accessibility settings and UI focus behaviour.

Compile/headless success is never reported as proof of combat feel. Each milestone separates automated evidence from manual play evidence.

## 18. Telemetry and tuning

Development builds should record structured local raid summaries:

- seed and build identifier;
- raid duration and terminal outcome;
- wave reached and extraction opportunity chosen;
- damage dealt/taken by source;
- player, enemy, and objective health timeline samples;
- ammunition gained, spent, and remaining;
- perks offered and selected;
- items collected, extracted, and lost;
- defence purchases and damage;
- frame p50/p95/p99 and fixed-tick p95;
- maximum active enemies, projectiles, pickups, and effects.

This is local development telemetry, not remote analytics. Remote collection requires a separate privacy and product decision.

### Initial tuning hypotheses

- A new player should reach the first extraction choice in under seven minutes.
- Wave-three extraction should be attractive when health, ammunition, or objective state is poor.
- The optional finale should not be the economically correct choice for every build.
- The extraction assault should last 15–25 seconds.
- Basic enemies should die quickly enough to preserve action flow; difficulty should come from role combinations and positioning.
- One failed run should not require more than one conservative success to recover a functional loadout.

These are hypotheses to test, not hidden requirements to defend when play evidence disagrees.

## 19. Risk register

| Risk | Consequence | Control and trigger |
|---|---|---|
| Wave repetition becomes monotonous | Low retention despite solid mechanics | Vary role combinations, optional objectives, arena events, routes, and extraction pressure before adding health inflation |
| Defence creates static camping | Movement pillar collapses | Saboteurs, ranged area denial, resource locations, moving extraction, and authored sightlines |
| Extraction loss feels punitive | Players avoid valuable loadouts or quit | Starter kits, clear loss preview, optional secured slot, measured recovery time |
| Permanent upgrades erase risk | Early content becomes trivial | Prefer unlock breadth and facility choices over unlimited damage/health scaling |
| Eight-direction art multiplies weapon workload | Content production stalls | Separate continuous simulation aim from eight-way body presentation; introduce weapon overlay if needed |
| Swarms exceed CPU/physics budget | Frame pacing and input suffer | Threat budgets, active caps, pooling, simple focused enemy state, profile before parallelizing |
| Procedural arenas feel incoherent | Visual quality and tactical readability fall | Hand-author arenas/room variants; seed placements and encounters rather than raw geometry |
| Save bug duplicates or destroys loot | Extraction contract loses credibility | Immutable RaidResult, idempotent transaction ID, atomic save, backup, fault-injection tests |
| Gameplay logic leaks into application loop | Every feature becomes expensive | Game-owned deep modules; application coordinates lifecycle only |
| Premature general engine work delays game | No playable extraction loop | Every engine extension requires a visible current-milestone consumer |
| Audio remains deferred too long | Shooter feedback lacks impact | Keep events and timing ready; compensate temporarily with animation, rumble, particles, and camera feedback; schedule explicit revisit after vertical-slice loop |
| Multiplayer pressure expands scope | Architecture and schedule reset | Single-player PvE is a written v0.1 constraint; networking requires a new charter and plan |

## 20. Release gates

### Vertical slice is complete when

- a fresh player can launch the packaged Shipping build and finish the hub-to-raid-to-debrief loop;
- both success and death produce correct, understandable persistence outcomes;
- one five-wave arena supports at least three viable temporary builds;
- the player can voluntarily extract after wave three or continue for higher rewards;
- mouse/keyboard and controller are fully usable;
- no critical crash, item duplication, save loss, or raid soft-lock is known;
- all automated suites, five-mode replay, packaged GPU smoke, and resource-lifetime checks pass;
- five external playtesters understand the objective and extraction decision without developer instruction;
- representative minimum hardware meets the performance budget.

### v0.1 expansion is approved only when

- players choose both early extraction and continued defence for understandable reasons;
- production time for one weapon, enemy, perk, and arena variant is measured and sustainable;
- the economy recovers cleanly from repeated failure;
- the game remains interesting after at least ten raids with the vertical-slice content;
- content expansion is a better investment than revising the core loop.

## 21. Owner decisions required before G1 closes

Recommended defaults are shown first.

1. **Loss model:** lose backpack and unprotected deployed equipment; always retain one small secured slot and permanent unlocks.
2. **Aim presentation:** continuous simulation aim with an eight-direction visual weapon overlay.
3. **Raid target length:** 8–15 minutes for normal raids.
4. **Extraction timing:** first voluntary extraction after wave three; standard finale after wave five.
5. **Building depth:** authored sockets, two defence types, repair and one upgrade tier; no free placement.
6. **Permanent progression:** unlock breadth and hub facilities, with tightly capped raw-stat increases.
7. **Procedural mix:** handcrafted arena variants plus seeded encounters, loot, perks, and extraction selection.
8. **Tone:** colourful patchwork dark fantasy—strange and tense, not graphic or militaristic.
9. **Working title:** use Loomhold internally until a naming pass after the vertical slice.
10. **Audio:** continue deferred until the owner explicitly schedules it; retain gameplay events needed for later sound.

## 22. Working rhythm and change control

- Start each milestone with one player-visible outcome and its measurable gate.
- Keep the executable playable every day.
- Add a regression test for every reproducible engine or raid-state defect when a correct seam exists.
- Profile before and after performance changes.
- Record schema and save-compatibility decisions before production content depends on them.
- Package a runnable checkpoint at every milestone.
- End each checkpoint note with: working, measured, broken/deferred, learned, next.
- Any request for multiplayer, open world, unrestricted construction, full scripting, or new platform support requires explicit scope review rather than silent addition.

## 23. Immediate next action

Use the new editor stress presets to record uncapped p50/p95/p99 frame time, renderer counts, searches, and waypoint advances at 50, 100, and 200 actors, then author a dedicated wall-separated runtime fixture. Require the consumed route to avoid every hard-blocked cell, reach attack range, recover after meaningful route invalidation, and avoid repeated wall contacts. Keep search ownership outside `EnemyIntent`. Dynamic local avoidance, smoothing, flow fields, and WaveDirector remain decision-gated until those measurements identify the next bottleneck.
