# Deterministic Moving-Attacker Checkpoint

## Outcome

IC_2DE now has one independently authored moving attacker without introducing a general AI framework. The Threadbound Runner acquires the player on the fixed tick, produces copied pursuit and attack intent, moves through the same GroundMap/Physics2D collision path as other kinematic actors, drives the existing eight-way locomotion presentation, and applies damage through the existing `Health` seam. The stationary patchwork target dummy remains available for weapon testing.

This is an editor-first checkpoint. Navigation, waves, and Shipping integration were deliberately not started so the complete slice can be playtested in isolation.

## Working

- `EnemyIntent` is a narrow engine-owned deep module for acquisition, direct normalized pursuit, attack-range entry, tick-authoritative cooldown, copied acquisition/attack events, reset, and canonical snapshots.
- Definitions and perceptions use stable entity UUIDs and copied values. Registration/perception order cannot change the snapshot, and rejected ticks do not partially mutate state.
- Scene schema 9 adds an explicit `attacker` physics role. Attackers must be kinematic and entity-bound; migration from schemas 5 through 8 never invents an attacker.
- `RuntimeScene` accepts copied stable-UUID actor-motion requests and returns actual distance/blocking results while retaining GroundMap, Physics2D, World synchronization, and locomotion-animation ownership.
- The application registers the player, target dummy, and runner with `Health`. Enemy attack requests are rejected during Combat dodge invulnerability or submitted as stable hit identities otherwise.
- GameplayState digest schema v2 includes canonical EnemyIntent state, future event identity, acquisition/attack counters, tuning, distance, direction, and cooldown.
- The HUD and editor Statistics panel expose attacker state, target range, movement direction, cooldown, acquisitions, attacks, collision-resolved travel, player damage, blocked motion, and dodge-invulnerability rejections.
- `--smoke-moving-attacker` is a dedicated 150-tick editor GPU route. The combined 180-tick gameplay replay now also requires attacker acquisition, pursuit, attack, and player damage.
- `tools/package-editor.ps1 -RunMovingAttackerProbe` validates the dedicated route from staged relocatable content and removes its temporary `build` directory before archiving.

## TDD and measured verification

- Focused EnemyIntent tests cover acquisition, pursuit, attack-range stopping, immediate first attack, cooldown timing, canonical ordering, dead-target inactivity, invalid definitions/perceptions, atomic rejected ticks, and reset.
- The first implementation build exposed that `Vec2` had no default equality usable by the public snapshot; explicit value equality fixed the public contract without changing vector ownership.
- Debug: 24/24 CTest targets passed with warnings treated as errors.
- Release: 24/24 CTest targets passed with warnings treated as errors.
- Dedicated Debug 60 Hz GPU route on NVIDIA GeForce RTX 2080 Ti / OpenGL 3.3: 1 acquisition, 1 attack request, `110.699997` world units of collision-resolved travel, and `12.000000` applied player damage over 150 fixed ticks.
- Combined Debug 60 Hz route: 3 projectile spawns, 3 impacts, 1 target death/retirement, 1 completed dodge with `78.000061` units of travel, 1 attacker acquisition, 2 attack requests, `97.199913` units of attacker travel, and `24.000000` player damage over 180 fixed ticks.
- Release replay at 30, 60, 120, monitor-synced, and uncapped presentation produced identical digest schema v2 value `4259082930085396436` in all five modes.
- The relocatable Release editor passed content validation, sprite-atlas validation, live bitmap hot swap, dodge, combined gameplay replay, and dedicated moving-attacker GPU probes.
- Visual inspection of `build/runtime-moving-attacker-packaged-smoke.png` confirmed the runner at player attack range, intact pixel presentation, schema 9, stable UUID count 16, attack count 1, and player damage 12 at capture tick 138.
- `dist/IC_2DE-Editor-Windows-x64.zip` contains exactly 14 runtime entries, no staged `build` directory, and no smoke capture.
- Package size: 11,547,092 bytes. SHA256: `0845DB585BF5614CE61F006619C82C52C420C37F3907F49687E4EC259ED2535D`.

## Manual playtest

1. Launch `dist/editor-windows-x64/IC_2DE-Editor.exe` and select the Statistics tab.
2. Stand still. Confirm the Threadbound Runner changes from pursuit to attack, its walk animation matches its travel direction, and it stops near the player instead of overlapping indefinitely.
3. Confirm player health falls by 12 per successful attack and the enemy cooldown counts down between attacks.
4. Reset with F5. Confirm player/runner health, runner placement, intent counters, cooldown, damage totals, and fixed-tick state return to the authored start.
5. Let the runner approach, dodge through an attack window, and confirm an invulnerable rejection can occur without player damage.
6. Move around the crate, wall, trees, and elevation edges. Record any snagging, oscillation, incorrect facing, ground clipping, or attack-range feel issue before navigation begins.
7. Fire at the stationary Patchwork NPC and confirm the original three-hit target-dummy test still behaves independently of the runner.

## Deferred

- The runner uses direct line pursuit and cannot route around a blocking wall. NavGrid and A* are the next planned dependency, after this checkpoint is accepted.
- Attack animation, hit reaction, knockback, damage flash, player health UI, death/respawn, authored combat catalogs, multiple attacker archetypes, and loot/wave behavior remain future slices.
- EnemyIntent deliberately does not include behavior trees, utility AI, EQS, local avoidance, flow fields, crowd simulation, or WaveDirector logic.
- Shipping runtime packaging/validation remains outside this editor-only production phase.
- Runtime tuning values are compiled constants until a second attacker definition or editor/catalog consumer justifies an authored data seam.

## Learned

Enemy decision state and actor movement are stronger when separated. EnemyIntent can be proved with pure copied values and no GPU or physics dependency, while RuntimeScene remains the single owner of collision and presentation synchronization. The application performs composition rather than becoming the owner of either subsystem. Including EnemyIntent in the authoritative digest also proves that identical final positions are insufficient: cooldown, acquisition, attack count, and future event identity all affect the next fixed tick.

## Next

Stop here for owner testing. After approval, add only the NavGrid data contract and editor debug view: deterministic world/cell conversion, hard-blocked walkability, and no diagonal corner cutting. Keep it independent of the runner until focused fixtures pass; deterministic A* and EnemyIntent integration belong to the following checkpoint.
