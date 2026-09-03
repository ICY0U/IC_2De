#include "ic2d/application.hpp"

#include "ic2d/aiming.hpp"
#include "ic2d/interaction.hpp"

#include "core/renderdoc_capture.hpp"
#include "ic2d/actor_debug.hpp"
#include "ic2d/assets.hpp"
#include "ic2d/combat.hpp"
#include "ic2d/core/automated_run.hpp"
#include "ic2d/core/fixed_step_clock.hpp"
#include "ic2d/core/frame_telemetry.hpp"
#include "ic2d/core/log.hpp"
#include "ic2d/debug_visuals.hpp"
#include "ic2d/enemy_intent.hpp"
#include "ic2d/gameplay_state.hpp"
#include "ic2d/health.hpp"
#include "ic2d/input.hpp"
#include "ic2d/jobs.hpp"
#include "ic2d/layer_stack.hpp"
#include "ic2d/nav_agent.hpp"
#include "ic2d/nav_grid.hpp"
#include "ic2d/nav_pathfinding.hpp"
#include "ic2d/presentation.hpp"
#include "ic2d/projectiles.hpp"
#include "ic2d/projection25d.hpp"
#include "ic2d/render2d.hpp"
#include "ic2d/runtime_project.hpp"
#include "ic2d/runtime_scene.hpp"
#include "ic2d/scene.hpp"
#include "input/raylib_input_adapter.hpp"
#include "render/frame_pipeline.hpp"
#include "render/gpu_backdrop.hpp"
#include "render/raylib_renderer2d.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <raylib.h>

#ifndef IC2DE_ENABLE_DEVELOPMENT_TOOLS
#define IC2DE_ENABLE_DEVELOPMENT_TOOLS 1
#endif

// Crowd separation, the flow field and the run state are gameplay, not tooling:
// the shipping runtime steers its actors with the first two and reads the third
// to decide whether its fixed clock advances, so all three are included
// unconditionally.
#include "ic2d/crowd_separation.hpp"
#include "ic2d/flow_field.hpp"
#include "ic2d/run_state.hpp"

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
#include "ic2d/editor.hpp"
#include "ic2d/editor_camera.hpp"
#include "ic2d/scene_document.hpp"
#include "ic2d/scene_editor.hpp"
#endif

namespace ic2d {
namespace {

// Gameplay facts the loop accumulates for the overlay and the automated
// verdict. Latching lives here so no call site has to remember which facts
// persist for the whole run and which describe only the current tick.
struct SceneObservations {
    bool movement_blocked{false};
    std::optional<std::uint32_t> active_trigger{};
    bool collision_observed{false};
    bool elevation_observed{false};
    bool dynamic_prop_moved{false};

    void observe(const RuntimeSceneTickResult& tick) noexcept {
        movement_blocked = tick.player_blocked;
        active_trigger = tick.active_trigger;
        collision_observed = collision_observed || tick.player_blocked;
        elevation_observed = elevation_observed || tick.player_elevated;
        dynamic_prop_moved = dynamic_prop_moved || tick.primary_prop_moved;
    }

    void forget() noexcept { *this = SceneObservations{}; }
};

struct CombatObservations {
    std::uint64_t intent_count{0};
    std::optional<CombatIntentEvent> last_intent;
    std::uint64_t projectile_count{0};
    std::optional<ProjectileSpawnedEvent> last_projectile;
    std::optional<DodgeStartedEvent> last_dodge;
    bool dodge_invulnerability_observed{false};
    float dodge_distance_travelled{0.0F};
    bool dodge_movement_blocked{false};

    void observe(const std::vector<CombatEvent>& events) noexcept {
        for (const CombatEvent& event : events) {
            if (const auto* intent = std::get_if<CombatIntentEvent>(&event)) {
                ++intent_count;
                last_intent = *intent;
            } else if (const auto* projectile = std::get_if<ProjectileSpawnedEvent>(&event)) {
                ++projectile_count;
                last_projectile = *projectile;
            } else if (const auto* dodge = std::get_if<DodgeStartedEvent>(&event)) {
                last_dodge = *dodge;
            }
        }
    }

    void reset() noexcept { *this = CombatObservations{}; }
};

struct ProjectileObservations {
    std::optional<ProjectileExpiredEvent> last_expired;
    std::optional<ProjectileImpactEvent> last_impact;

    void observe(std::vector<ProjectileExpiredEvent> events) noexcept {
        if (!events.empty()) {
            last_expired = events.back();
        }
    }

    void observe(std::vector<ProjectileImpactEvent> events) noexcept {
        if (!events.empty()) {
            last_impact = events.back();
        }
    }

    void reset() noexcept { *this = ProjectileObservations{}; }
};

struct HealthObservations {
    std::optional<DamageAppliedEvent> last_damage;
    std::optional<ActorDiedEvent> last_death;
    std::uint64_t retired_actor_count{0};
    std::uint64_t retired_crowd_actor_count{0};

    void observe(const std::vector<HealthEvent>& events) noexcept {
        for (const HealthEvent& event : events) {
            if (const auto* damage = std::get_if<DamageAppliedEvent>(&event)) {
                last_damage = *damage;
            } else if (const auto* death = std::get_if<ActorDiedEvent>(&event)) {
                last_death = *death;
            }
        }
    }

    void reset() noexcept { *this = HealthObservations{}; }
};

struct EnemyIntentObservations {
    std::optional<EnemyAcquiredTargetEvent> last_acquisition;
    std::optional<EnemyAttackRequestedEvent> last_attack;
    float distance_travelled{0.0F};
    float damage_applied_to_player{0.0F};
    bool movement_blocked{false};
    std::uint64_t invulnerable_attacks_rejected{0};

    void observe(const std::vector<EnemyIntentEvent>& events) noexcept {
        for (const EnemyIntentEvent& event : events) {
            if (const auto* acquisition = std::get_if<EnemyAcquiredTargetEvent>(&event)) {
                last_acquisition = *acquisition;
            } else if (const auto* attack = std::get_if<EnemyAttackRequestedEvent>(&event)) {
                last_attack = *attack;
            }
        }
    }

    void observe(const RuntimeSceneTickResult& tick) noexcept {
        for (const RuntimeSceneActorMotionResult& motion : tick.actor_motions) {
            distance_travelled += motion.distance_moved;
            movement_blocked = movement_blocked || motion.blocked;
        }
    }

    void observe_player_damage(const std::vector<HealthEvent>& events,
                               const EntityUuid player) noexcept {
        for (const HealthEvent& event : events) {
            const auto* damage = std::get_if<DamageAppliedEvent>(&event);
            if (damage != nullptr && damage->target == player) {
                damage_applied_to_player += damage->applied_damage;
            }
        }
    }

    void reset() noexcept { *this = EnemyIntentObservations{}; }
};

constexpr float player_maximum_health = 100.0F;
constexpr float target_dummy_maximum_health = 54.0F;
constexpr float attacker_maximum_health = 36.0F;
constexpr float attacker_movement_speed = 54.0F;
constexpr float attacker_acquisition_range = 180.0F;
constexpr float stress_attacker_acquisition_range = 600.0F;

// Stress spawns ring the arena, so their acquisition has to reach at least as
// far as they are placed or they idle where they spawned instead of
// converging. Derived from the same bounds the spawn ring is derived from.
// World-space bound on what the camera can show. Screen X follows the camera's
// right axis and screen Y follows its forward axis foreshortened by pitch, so
// the visible ground is a rotated rectangle; this returns an axis-aligned box
// that contains it. The margin covers a sprite taller than its footprint, an
// elevated one drawn higher up the screen, and a body that has moved since the
// last fixed tick, so nothing that should be visible is dropped.
[[nodiscard]] RectXZ visible_world_region(const Camera25DState& camera, const int canvas_width,
                                          const int canvas_height, const float margin) noexcept {
    const float scale = camera.pixels_per_world_unit * camera.zoom;
    if (!(scale > 0.0F)) {
        return {};
    }
    constexpr float degrees_to_radians = 3.14159265F / 180.0F;
    const float pitch_sine = std::sin(camera.pitch_degrees * degrees_to_radians);
    const float half_right = static_cast<float>(canvas_width) * 0.5F / scale;
    const float half_forward = pitch_sine > 0.01F
                                   ? static_cast<float>(canvas_height) * 0.5F / (scale * pitch_sine)
                                   : static_cast<float>(canvas_height) * 0.5F / scale;
    const float yaw = camera.yaw_degrees * degrees_to_radians;
    const float yaw_cosine = std::abs(std::cos(yaw));
    const float yaw_sine = std::abs(std::sin(yaw));
    const float half_x = yaw_cosine * half_right + yaw_sine * half_forward + margin;
    const float half_z = yaw_sine * half_right + yaw_cosine * half_forward + margin;
    return {
        .x = camera.focus.x - half_x,
        .z = camera.focus.z - half_z,
        .width = half_x * 2.0F,
        .depth = half_z * 2.0F,
    };
}

[[nodiscard]] float stress_acquisition_range(const NavGridSnapshot& topology) noexcept {
    const float arena_reach = std::min(topology.bounds.width, topology.bounds.depth) * 0.5F;
    const float diagonal = std::sqrt(topology.bounds.width * topology.bounds.width +
                                     topology.bounds.depth * topology.bounds.depth);
    return std::max({stress_attacker_acquisition_range, arena_reach * 1.6F, diagonal});
}
constexpr float attacker_attack_range = 20.0F;
constexpr std::uint32_t attacker_attack_cooldown_ticks = 45;
constexpr float attacker_attack_damage = 12.0F;
constexpr float navigation_cell_size = 20.0F;
// Above this many attackers converging on one target, steering switches from a
// route per actor to one shared flow field. A search is paid per actor and a
// field is paid per map, so the field wins as soon as a crowd is large enough
// to be interesting; the threshold sits above the authored encounter sizes so
// small fights keep their individual routes, waypoints and repath behaviour.
constexpr std::size_t crowd_flow_field_threshold = 128;

// Which attackers the router carries, and whether the rest are field-steered.
// Resolving the attacker list is a scene-build operation, not a per-tick one,
// so both are decided once and carried alongside the scene.
struct CrowdSteeringPlan {
    bool active{false};
    // Ascending actor identity, so membership is a binary search. Small
    // whenever the crowd is field-steered, which is exactly when it is hot.
    std::vector<EntityUuid> routed_actors;

    [[nodiscard]] bool routes_individually(const EntityUuid actor) const {
        return std::ranges::binary_search(routed_actors, actor);
    }
};

[[nodiscard]] CrowdSteeringPlan plan_crowd_steering(const RuntimeScene& scene) {
    const std::vector<EntityUuid> attackers = scene.actor_uuids(ScenePhysicsRole::attacker);
    CrowdSteeringPlan plan{
        .active = attackers.size() >= crowd_flow_field_threshold &&
                  static_cast<bool>(scene.player_uuid()),
    };
    for (const EntityUuid actor : attackers) {
        if (plan.active && scene.is_crowd_actor(actor)) {
            continue;
        }
        plan.routed_actors.push_back(actor);
    }
    std::ranges::sort(plan.routed_actors);
    return plan;
}
// Fraction of pursuit speed an attacking Runner uses purely to unstack itself.
constexpr float separation_shuffle_scale = 0.45F;

[[nodiscard]] NavGridBakeSettings navigation_grid_settings(const SceneDefinition& definition) {
    Vec2 clearance{};
    bool found = false;
    for (const ScenePhysicsBodyDefinition& body : definition.physics_bodies()) {
        if (body.role != ScenePhysicsRole::attacker) {
            continue;
        }
        clearance.x = std::max(clearance.x, body.box.half_extents.x);
        clearance.y = std::max(clearance.y, body.box.half_extents.y);
        found = true;
    }
    if (!found) {
        for (const ScenePhysicsBodyDefinition& body : definition.physics_bodies()) {
            if (body.role == ScenePhysicsRole::player) {
                clearance = body.box.half_extents;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        throw std::logic_error{"Navigation grid requires an attacker or player footprint."};
    }
    return {
        .cell_size = navigation_cell_size,
        .agent_half_extents = clearance,
    };
}

[[nodiscard]] NavPathResult navigation_reference_path(const GroundMapDefinition& ground,
                                                      const NavGrid& grid) {
    const NavGridSnapshot snapshot = grid.snapshot();
    for (const GroundArea& area : ground.areas) {
        if (area.kind != GroundAreaKind::solid) {
            continue;
        }
        const float center_z = area.bounds.z + area.bounds.depth * 0.5F;
        const NavPathResult path = find_nav_path_world(
            grid, {area.bounds.x - snapshot.cell_size * 2.0F, center_z},
            {area.bounds.x + area.bounds.width + snapshot.cell_size * 2.0F, center_z});
        if (path.status == NavPathStatus::found) {
            return path;
        }
    }

    const auto first = std::find_if(snapshot.cells.begin(), snapshot.cells.end(),
                                    [](const NavGridCell& cell) { return cell.walkable; });
    const auto last = std::find_if(snapshot.cells.rbegin(), snapshot.cells.rend(),
                                   [](const NavGridCell& cell) { return cell.walkable; });
    if (first == snapshot.cells.end() || last == snapshot.cells.rend()) {
        return {.status = NavPathStatus::unreachable};
    }
    return find_nav_path(grid, first->cell, last->cell);
}

void register_scene_health_targets(Health& health, const RuntimeScene& scene) {
    if (!health.register_target({
            .target = scene.player_uuid(),
            .maximum_health = player_maximum_health,
        })) {
        throw std::logic_error{"Runtime scene provided an invalid player health target."};
    }
    for (const EntityUuid actor : scene.actor_uuids(ScenePhysicsRole::enemy)) {
        if (!health.register_target({
                .target = actor,
                .maximum_health = target_dummy_maximum_health,
            })) {
            throw std::logic_error{"Runtime scene provided a duplicate or invalid health target."};
        }
    }
    for (const EntityUuid actor : scene.actor_uuids(ScenePhysicsRole::attacker)) {
        if (!health.register_target({
                .target = actor,
                .maximum_health = attacker_maximum_health,
            })) {
            throw std::logic_error{
                "Runtime scene provided a duplicate or invalid attacker health target."};
        }
    }
}

void register_scene_enemy_intents(EnemyIntent& intent, const RuntimeScene& scene,
                                  const float acquisition_range = attacker_acquisition_range) {
    for (const EntityUuid actor : scene.actor_uuids(ScenePhysicsRole::attacker)) {
        if (!intent.register_actor({
                .actor = actor,
                .target = scene.player_uuid(),
                .movement_speed = attacker_movement_speed,
                .acquisition_range = acquisition_range,
                .attack_range = attacker_attack_range,
                .attack_cooldown_ticks = attacker_attack_cooldown_ticks,
                .attack_damage = attacker_attack_damage,
            })) {
            throw std::logic_error{
                "Runtime scene provided a duplicate or invalid attacker intent."};
        }
    }
}

struct CrowdSteeringPlan;

// A field-steered crowd never consumes an individual route, so registering it
// with the router would cost a request, a map traversal and a returned motion
// per actor every tick for a result that is discarded. The plan decides both
// registration and steering, so the two cannot disagree.
void register_scene_navigation_agents(NavAgentSystem& navigation,
                                      const std::vector<EntityUuid>& routed_actors) {
    for (const EntityUuid actor : routed_actors) {
        if (!navigation.register_agent(actor)) {
            throw std::logic_error{
                "Runtime scene provided a duplicate or invalid navigation agent."};
        }
    }
}

// Resolves authored interactables against the entities they attach to. Pickups
// do not move, so their authored position is their world position for the whole
// run and nothing has to be re-resolved per tick.
[[nodiscard]] std::vector<Interactable> build_interactables(const SceneDefinition& definition) {
    std::vector<Interactable> resolved;
    resolved.reserve(definition.interactables().size());
    for (const SceneInteractableDefinition& authored : definition.interactables()) {
        const auto entity = std::ranges::find(definition.entities(), authored.entity_id,
                                              &SceneEntityDefinition::id);
        if (entity == definition.entities().end()) {
            continue;
        }
        resolved.push_back({
            .entity = entity->uuid,
            .position = entity->position,
            .kind = authored.kind,
            .amount = authored.amount,
            .radius = authored.radius,
        });
    }
    return resolved;
}

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
struct EnemyStressSpawnSummary {
    std::size_t requested_total{0};
    std::size_t authored{0};
    std::size_t spawned{0};

    [[nodiscard]] std::size_t total() const noexcept { return authored + spawned; }
};

[[nodiscard]] EnemyStressSpawnSummary
spawn_editor_stress_runners(RuntimeScene& scene, const NavGrid& grid,
                            const std::size_t requested_total) {
    const std::vector<EntityUuid> authored_actors = scene.actor_uuids(ScenePhysicsRole::attacker);
    EnemyStressSpawnSummary summary{
        .requested_total = requested_total,
        .authored = authored_actors.size(),
    };
    if (requested_total == 0 || requested_total <= summary.authored) {
        return summary;
    }

    const Vec3 player_position_3d = scene.player_position();
    const Vec2 player_position{player_position_3d.x, player_position_3d.z};
    // Spawns must share a connected component with the actor being copied, not
    // with the player: a scene may deliberately seal the player inside walls no
    // Runner can path through, and seeding from the player there would leave
    // only the handful of cells inside that enclosure.
    Vec2 seed_position = player_position;
    if (!authored_actors.empty()) {
        if (const std::optional<Vec3> authored = scene.actor_position(authored_actors.front())) {
            seed_position = {authored->x, authored->z};
        }
    }
    const std::optional<NavCell> seed_cell = grid.cell_at(seed_position);
    const NavGridSnapshot snapshot = grid.snapshot();
    if (!seed_cell || snapshot.columns <= 0 || snapshot.rows <= 0) {
        throw std::logic_error{
            "Enemy stress spawning requires its seed actor on a valid navigation cell."};
    }

    const auto cell_offset = [&snapshot](const NavCell cell) {
        return static_cast<std::size_t>(cell.row) * static_cast<std::size_t>(snapshot.columns) +
               static_cast<std::size_t>(cell.column);
    };
    std::vector<bool> reachable(snapshot.cells.size(), false);
    std::deque<NavCell> frontier;
    reachable[cell_offset(*seed_cell)] = true;
    frontier.push_back(*seed_cell);
    while (!frontier.empty()) {
        const NavCell cell = frontier.front();
        frontier.pop_front();
        for (const NavGridNeighbor& neighbor : grid.neighbors(cell)) {
            const std::size_t offset = cell_offset(neighbor.cell);
            if (!reachable[offset]) {
                reachable[offset] = true;
                frontier.push_back(neighbor.cell);
            }
        }
    }

    std::set<NavCell> occupied_cells;
    const auto exclude_position = [&grid, &occupied_cells](const Vec3 position) {
        if (const std::optional<NavCell> cell = grid.cell_at({position.x, position.z})) {
            occupied_cells.insert(*cell);
        }
    };
    exclude_position(scene.player_position());
    exclude_position(scene.primary_prop_position());
    for (const ScenePhysicsRole role : {
             ScenePhysicsRole::enemy,
             ScenePhysicsRole::attacker,
         }) {
        for (const EntityUuid actor : scene.actor_uuids(role)) {
            if (const std::optional<Vec3> position = scene.actor_position(actor)) {
                exclude_position(*position);
            }
        }
    }

    std::vector<NavGridCell> candidates;
    candidates.reserve(snapshot.walkable_cell_count);
    // Spawn distances scale with the arena instead of being fixed world units,
    // so a large performance arena puts the crowd off screen while the smaller
    // authored scene keeps the close-quarters layout its checks expect.
    const float arena_reach = std::min(snapshot.bounds.width, snapshot.bounds.depth) * 0.5F;
    const float minimum_spawn_distance = std::max(100.0F, arena_reach * 0.45F);
    const float minimum_spawn_distance_squared = minimum_spawn_distance * minimum_spawn_distance;
    for (std::size_t index = 0; index < snapshot.cells.size(); ++index) {
        const NavGridCell& cell = snapshot.cells[index];
        const float delta_x = cell.center.x - player_position.x;
        const float delta_z = cell.center.y - player_position.y;
        if (reachable[index] && cell.walkable && !occupied_cells.contains(cell.cell) &&
            delta_x * delta_x + delta_z * delta_z >= minimum_spawn_distance_squared) {
            candidates.push_back(cell);
        }
    }

    const std::size_t desired_copies = requested_total - summary.authored;
    const std::size_t spawn_count = std::min(desired_copies, candidates.size());
    std::vector<bool> selected(candidates.size(), false);
    std::vector<Vec2> positions;
    positions.reserve(spawn_count);
    constexpr float golden_angle_radians = 2.39996323F;
    constexpr float first_angle_radians = 0.812F;
    // Fractions of the arena reach rather than absolute distances, for the
    // same reason the minimum above is derived: the ring has to sit outside
    // the view in a large arena without collapsing onto a small one.
    constexpr std::array<float, 5> spawn_ring_fractions{
        0.62F, 0.75F, 0.68F, 0.85F, 0.95F,
    };
    for (std::size_t spawn_index = 0; spawn_index < spawn_count; ++spawn_index) {
        const float angle =
            first_angle_radians + static_cast<float>(spawn_index) * golden_angle_radians;
        const float radius =
            arena_reach * spawn_ring_fractions[spawn_index % spawn_ring_fractions.size()];
        const Vec2 desired{
            player_position.x + std::cos(angle) * radius,
            player_position.y + std::sin(angle) * radius,
        };
        std::size_t best_index = candidates.size();
        float best_distance_squared = std::numeric_limits<float>::max();
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            if (selected[index]) {
                continue;
            }
            const float delta_x = candidates[index].center.x - desired.x;
            const float delta_z = candidates[index].center.y - desired.y;
            const float distance_squared = delta_x * delta_x + delta_z * delta_z;
            if (distance_squared < best_distance_squared) {
                best_distance_squared = distance_squared;
                best_index = index;
            }
        }
        if (best_index == candidates.size()) {
            break;
        }
        selected[best_index] = true;
        positions.push_back(candidates[best_index].center);
    }

    summary.spawned = scene.spawn_actor_copies(ScenePhysicsRole::attacker, positions).size();
    return summary;
}

struct EditorRuntimeSceneCandidate {
    std::unique_ptr<RuntimeScene> scene;
    std::unique_ptr<NavGrid> navigation_grid;
    NavGridSnapshot navigation_grid_snapshot;
    NavPathResult navigation_path;
    Health health;
    EnemyIntent enemy_intent;
    NavAgentSystem navigation_agents;
    CrowdSteeringPlan crowd_plan;
    EnemyStressSpawnSummary stress;
    // Resolved before the definition is consumed by the scene, because the
    // replacement's items are part of what is being swapped in.
    std::vector<Interactable> interactables;
    bool enemy_attack_damage_enabled{true};
};

[[nodiscard]] EditorRuntimeSceneCandidate
make_editor_runtime_scene_candidate(SceneDefinition definition, TextureAssets& textures,
                                    const std::size_t stress_target_count) {
    auto grid =
        std::make_unique<NavGrid>(definition.ground(), navigation_grid_settings(definition));
    NavGridSnapshot grid_snapshot = grid->snapshot();
    NavPathResult reference_path = navigation_reference_path(definition.ground(), *grid);
    std::vector<Interactable> candidate_interactables = build_interactables(definition);
    auto runtime_scene = std::make_unique<RuntimeScene>(std::move(definition), textures);
    EnemyStressSpawnSummary stress =
        spawn_editor_stress_runners(*runtime_scene, *grid, stress_target_count);
    Health candidate_health;
    EnemyIntent candidate_enemy_intent;
    NavAgentSystem candidate_navigation_agents;
    register_scene_health_targets(candidate_health, *runtime_scene);
    register_scene_enemy_intents(candidate_enemy_intent, *runtime_scene,
                                 stress_target_count > 0
                                     ? stress_acquisition_range(grid->topology())
                                     : attacker_acquisition_range);
    CrowdSteeringPlan candidate_crowd_plan = plan_crowd_steering(*runtime_scene);
    register_scene_navigation_agents(candidate_navigation_agents,
                                     candidate_crowd_plan.routed_actors);
    return {
        .scene = std::move(runtime_scene),
        .navigation_grid = std::move(grid),
        .navigation_grid_snapshot = std::move(grid_snapshot),
        .navigation_path = std::move(reference_path),
        .health = std::move(candidate_health),
        .enemy_intent = std::move(candidate_enemy_intent),
        .navigation_agents = std::move(candidate_navigation_agents),
        .crowd_plan = std::move(candidate_crowd_plan),
        .stress = stress,
        .interactables = std::move(candidate_interactables),
        .enemy_attack_damage_enabled = stress_target_count == 0,
    };
}
#endif

[[nodiscard]] NavPathResult displayed_navigation_path(const NavPathResult& reference,
                                                      const NavAgentSnapshot& navigation) {
    const NavAgentStateSnapshot* active = nullptr;
    for (const NavAgentStateSnapshot& actor : navigation.actors) {
        if (!actor.active || actor.path_status != NavPathStatus::found || actor.path.empty()) {
            continue;
        }
        if (active == nullptr || actor.path.size() > active->path.size()) {
            active = &actor;
        }
    }
    if (active == nullptr) {
        return reference;
    }
    return {
        .status = active->path_status,
        .cells = active->path,
        .total_distance = active->path_distance,
        .expanded_cell_count = active->expanded_cell_count,
    };
}

// Health, enemy-intent and navigation snapshots are all emitted by iterating
// UUID-keyed ordered maps, so each arrives sorted by actor. Looking an actor up
// with a linear scan is quadratic across the whole crowd and, at a few thousand
// actors, costs more than every other part of the tick combined.
template <typename Range, typename Projection>
[[nodiscard]] auto find_by_actor(const Range& range, const EntityUuid actor,
                                 Projection projection) {
    const auto last = std::ranges::end(range);
    const auto found = std::ranges::lower_bound(range, actor, {}, projection);
    return found != last && std::invoke(projection, *found) == actor ? found : last;
}

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
// Sub-phase accumulators for the fixed step. A single "fixed update" number
// says the simulation is slow but not which stage to attack, and these stages
// differ by orders of magnitude once the crowd is large.
struct FixedStepPhaseTotals {
    double perception{0.0};
    double intent{0.0};
    double navigation{0.0};
    double separation{0.0};
    double scene_tick{0.0};
    double remainder{0.0};
    std::uint64_t ticks{0};
};
FixedStepPhaseTotals fixed_step_phases;

[[nodiscard]] std::chrono::steady_clock::time_point phase_clock() noexcept {
    return std::chrono::steady_clock::now();
}

[[nodiscard]] double phase_elapsed(std::chrono::steady_clock::time_point& marker) noexcept {
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>{now - marker}.count();
    marker = now;
    return seconds;
}

// Moving and sizing a window the platform no longer decorates.
//
// The anchor is what makes this stable. Resolving a drag against the previous
// frame would feed the window's own movement back into the next delta, so a
// window under a still pointer would drift; anchoring the pointer and the rect
// once, at the press, means a still pointer always resolves to the rect it
// started from.
struct WindowChromeDrag {
    EditorWindowDrag gesture{EditorWindowDrag::none};
    Vector2 pointer{};
    Vector2 position{};
    Vector2 size{};
};

// Small enough to tuck a window away, large enough that the docked layout
// still has somewhere to put every panel.
constexpr float minimum_window_width = 720.0F;
constexpr float minimum_window_height = 480.0F;

[[nodiscard]] Vector2 desktop_pointer() noexcept {
    const Vector2 window = GetWindowPosition();
    const Vector2 pointer = GetMousePosition();
    return {window.x + pointer.x, window.y + pointer.y};
}

void apply_window_chrome(const EditorWindowActions& request, WindowChromeDrag& drag) {
    if (request.minimize) {
        MinimizeWindow();
    }
    if (request.toggle_maximize) {
        if (IsWindowMaximized()) {
            RestoreWindow();
        } else {
            MaximizeWindow();
        }
    }
    if (request.drag == EditorWindowDrag::none) {
        drag.gesture = EditorWindowDrag::none;
        return;
    }
    if (request.drag_started || drag.gesture != request.drag) {
        drag = {
            .gesture = request.drag,
            .pointer = desktop_pointer(),
            .position = GetWindowPosition(),
            .size = {static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())},
        };
        return;
    }

    const Vector2 pointer = desktop_pointer();
    const float moved_x = pointer.x - drag.pointer.x;
    const float moved_y = pointer.y - drag.pointer.y;
    if (request.drag == EditorWindowDrag::move) {
        SetWindowPosition(static_cast<int>(std::lround(drag.position.x + moved_x)),
                          static_cast<int>(std::lround(drag.position.y + moved_y)));
        return;
    }

    const bool west = request.drag == EditorWindowDrag::resize_left ||
                      request.drag == EditorWindowDrag::resize_top_left ||
                      request.drag == EditorWindowDrag::resize_bottom_left;
    const bool east = request.drag == EditorWindowDrag::resize_right ||
                      request.drag == EditorWindowDrag::resize_top_right ||
                      request.drag == EditorWindowDrag::resize_bottom_right;
    const bool north = request.drag == EditorWindowDrag::resize_top ||
                       request.drag == EditorWindowDrag::resize_top_left ||
                       request.drag == EditorWindowDrag::resize_top_right;
    const bool south = request.drag == EditorWindowDrag::resize_bottom ||
                       request.drag == EditorWindowDrag::resize_bottom_left ||
                       request.drag == EditorWindowDrag::resize_bottom_right;

    float x = drag.position.x;
    float y = drag.position.y;
    float width = drag.size.x;
    float height = drag.size.y;
    if (east) {
        width = std::max(minimum_window_width, drag.size.x + moved_x);
    } else if (west) {
        // Clamped on the width, then the origin is derived from it, so a
        // window pulled past its minimum stops growing instead of walking its
        // left edge across the desktop.
        width = std::max(minimum_window_width, drag.size.x - moved_x);
        x = drag.position.x + (drag.size.x - width);
    }
    if (south) {
        height = std::max(minimum_window_height, drag.size.y + moved_y);
    } else if (north) {
        height = std::max(minimum_window_height, drag.size.y - moved_y);
        y = drag.position.y + (drag.size.y - height);
    }
    SetWindowSize(static_cast<int>(std::lround(width)), static_cast<int>(std::lround(height)));
    SetWindowPosition(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
}
#endif

[[nodiscard]] bool health_target_alive(const HealthSnapshot& health,
                                       const EntityUuid target) noexcept {
    const auto found = find_by_actor(health.targets, target, &HealthTargetSnapshot::target);
    return found != health.targets.end() && found->alive;
}

[[nodiscard]] const char* enemy_intent_state_name(const EnemyIntentState state) noexcept {
    switch (state) {
    case EnemyIntentState::unaware:
        return "UNAWARE";
    case EnemyIntentState::pursuing:
        return "PURSUING";
    case EnemyIntentState::attacking:
        return "ATTACKING";
    case EnemyIntentState::inactive:
        return "INACTIVE";
    }
    return "UNKNOWN";
}

// Translates render-rate logical input into copied simulation commands. Aim is
// submitted only when it changes; action edges remain independently buffered
// by Combat until the next fixed tick.
struct CombatCommandAdapter {
    EntityUuid actor{};
    std::optional<Vec2> submitted_aim;
    std::optional<Vec2> retained_dodge_direction;
    bool submitted_fire_held{false};

    [[nodiscard]] CombatCommand translate(const EntityUuid next_actor, const InputFrame& input,
                                          const std::optional<Vec2> resolved_aim,
                                          const Vec2 world_movement_direction,
                                          const Vec2 world_facing_fallback,
                                          const bool pointer_over_gameplay) noexcept {
        if (actor != next_actor) {
            reset();
            actor = next_actor;
        }

        CombatCommand command;
        command.actor = next_actor;
        const auto normalized_direction = [](const Vec2 direction) -> std::optional<Vec2> {
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
            if (!std::isfinite(length) || !(length > 0.0001F)) {
                return std::nullopt;
            }
            return Vec2{direction.x / length, direction.y / length};
        };
        if (resolved_aim) {
            constexpr float changed_epsilon_squared = 0.000001F;
            const float delta_x = submitted_aim ? resolved_aim->x - submitted_aim->x : 1.0F;
            const float delta_y = submitted_aim ? resolved_aim->y - submitted_aim->y : 1.0F;
            if (!submitted_aim || delta_x * delta_x + delta_y * delta_y > changed_epsilon_squared) {
                command.aim_direction = resolved_aim;
                submitted_aim = resolved_aim;
            }
        }

        // Movement wins while held; aim becomes the stationary fallback. The
        // retained value lets an idle press preserve the last useful facing.
        if (const std::optional<Vec2> movement = normalized_direction(world_movement_direction)) {
            retained_dodge_direction = movement;
        } else if (resolved_aim) {
            retained_dodge_direction = normalized_direction(*resolved_aim);
        }

        const bool allow_fire = !input.gameplay.aim.pointer_active || pointer_over_gameplay;
        const ButtonState fire = input.gameplay.action(GameplayAction::fire);
        const bool fire_held = allow_fire && fire.down;
        if (fire_held != submitted_fire_held) {
            command.set_fire_held(fire_held);
            submitted_fire_held = fire_held;
        }
        command.request(CombatIntent::fire, allow_fire && fire.pressed);
        command.request(CombatIntent::reload,
                        input.gameplay.action(GameplayAction::reload).pressed);
        const bool dodge_pressed = input.gameplay.action(GameplayAction::dodge).pressed;
        command.request(CombatIntent::dodge, dodge_pressed);
        if (dodge_pressed) {
            command.dodge_direction = retained_dodge_direction.value_or(world_facing_fallback);
        }
        command.request(CombatIntent::swap_weapon,
                        input.gameplay.action(GameplayAction::swap_weapon).pressed);
        return command;
    }

    void reset() noexcept {
        actor = {};
        submitted_aim.reset();
        retained_dodge_direction.reset();
        submitted_fire_held = false;
    }

    [[nodiscard]] std::optional<Vec2> aim_direction() const noexcept { return submitted_aim; }
};

[[nodiscard]] std::optional<Vec2> pointer_canvas_point(const AimInput& aim,
                                                       const bool editor_visible,
                                                       const std::optional<Vec2> editor_pointer,
                                                       const int canvas_width,
                                                       const int canvas_height) noexcept {
    if (!aim.pointer_active) {
        return std::nullopt;
    }
    if (editor_visible) {
        return editor_pointer;
    }

    const CanvasViewport viewport =
        compute_canvas_viewport(GetScreenWidth(), GetScreenHeight(), canvas_width, canvas_height);
    if (!(viewport.scale > 0.0F) || aim.pointer_screen_x < viewport.x ||
        aim.pointer_screen_y < viewport.y || aim.pointer_screen_x > viewport.x + viewport.width ||
        aim.pointer_screen_y > viewport.y + viewport.height) {
        return std::nullopt;
    }
    return Vec2{
        (aim.pointer_screen_x - viewport.x) / viewport.scale,
        (aim.pointer_screen_y - viewport.y) / viewport.scale,
    };
}

// Rotates the raw stick from camera space into world X/Z while preserving its
// deflection, which the aim module needs: the length is what scales the turn
// rate, so normalizing here would throw away fine control.
[[nodiscard]] Vec2 stick_aim_world(const AimInput& aim, const Camera25DState& camera) noexcept {
    const float length = std::sqrt(aim.horizontal * aim.horizontal + aim.depth * aim.depth);
    if (!(length > 0.0001F)) {
        return {};
    }
    const Vec3 world =
        camera_ground_direction_to_world({aim.horizontal / length, aim.depth / length}, camera);
    return {world.x * length, world.z * length};
}

// Projects a world position into canvas space, returning nothing when it falls
// outside the canvas. Used for the crosshair and the assist marker, which are
// both world positions that must not be drawn off-screen.
[[nodiscard]] std::optional<Vec2> canvas_position_of(const Vec3& world_position,
                                                     const int canvas_width,
                                                     const int canvas_height,
                                                     const Camera25DState& camera) noexcept {
    const ProjectedPoint25D projected = project_world_point(world_position, camera);
    const Vec2 canvas{
        projected.position.x + static_cast<float>(canvas_width) * 0.5F,
        projected.position.y + static_cast<float>(canvas_height) * 0.5F,
    };
    if (canvas.x < 0.0F || canvas.y < 0.0F || canvas.x >= static_cast<float>(canvas_width) ||
        canvas.y >= static_cast<float>(canvas_height)) {
        return std::nullopt;
    }
    return canvas;
}

// A ring on the actor the aim is being helped toward. Assist moves the shot,
// not the cursor, so the crosshair stays under the player's hand and this is
// what tells them the help is happening.
// The target the aim is being helped toward. Four corner brackets rather than a
// ring: brackets read as a deliberate lock at this pixel scale, where a thin
// circle turns into a jagged blob, and they leave the actor's silhouette
// unobscured. Drawn in canvas coordinates like the crosshair, because this is
// inside the scene render target.
void draw_assist_marker(const Vec2 canvas_position, const bool firing) {
    const int x = static_cast<int>(std::round(canvas_position.x));
    const int y = static_cast<int>(std::round(canvas_position.y));

    // Tightens on the target while firing, which is the whole readout: the
    // player sees the lock close rather than a static decoration.
    const int reach = firing ? 9 : 11;
    constexpr int arm = 4;
    constexpr int thickness = 1;
    constexpr Color shadow{13, 30, 29, 190};
    const Color bracket = firing ? Color{248, 194, 89, 255} : Color{117, 238, 211, 225};

    // Each corner is two strokes over a one-pixel shadow, so the mark stays
    // legible against both the bright and the dark parts of a sprite.
    for (int corner = 0; corner < 4; ++corner) {
        const int sx = (corner & 1) == 0 ? -1 : 1;
        const int sy = (corner & 2) == 0 ? -1 : 1;
        const int cx = x + sx * reach;
        const int cy = y + sy * reach;
        const int left = sx < 0 ? cx : cx - arm + 1;
        const int top = sy < 0 ? cy : cy - arm + 1;
        DrawRectangle(left, cy - (sy < 0 ? 0 : thickness - 1) + 1, arm, thickness, shadow);
        DrawRectangle(cx - (sx < 0 ? 0 : thickness - 1) + 1, top, thickness, arm, shadow);
        DrawRectangle(left, cy - (sy < 0 ? 0 : thickness - 1), arm, thickness, bracket);
        DrawRectangle(cx - (sx < 0 ? 0 : thickness - 1), top, thickness, arm, bracket);
    }
}

[[nodiscard]] std::optional<Vec2>
crosshair_canvas_position(const AimInput& input, const std::optional<Vec2> pointer_canvas,
                          const std::optional<Vec3> aim_point, const int canvas_width,
                          const int canvas_height, const Camera25DState& camera) noexcept {
    if (input.pointer_active) {
        return pointer_canvas;
    }
    if (!aim_point) {
        return std::nullopt;
    }

    const ProjectedPoint25D projected = project_world_point(*aim_point, camera);
    const Vec2 canvas{
        projected.position.x + static_cast<float>(canvas_width) * 0.5F,
        projected.position.y + static_cast<float>(canvas_height) * 0.5F,
    };
    if (canvas.x < 0.0F || canvas.y < 0.0F || canvas.x >= static_cast<float>(canvas_width) ||
        canvas.y >= static_cast<float>(canvas_height)) {
        return std::nullopt;
    }
    return canvas;
}

void draw_pixel_crosshair(const Vec2& canvas_position, const int canvas_width,
                          const int canvas_height, const bool firing) {
    const int x = std::clamp(static_cast<int>(std::round(canvas_position.x)), 0, canvas_width - 1);
    const int y = std::clamp(static_cast<int>(std::round(canvas_position.y)), 0, canvas_height - 1);
    constexpr Color outline{13, 30, 29, 230};
    constexpr Color thread_gold{248, 194, 89, 255};
    constexpr Color centre{117, 238, 211, 255};
    const int spread = firing ? 2 : 0;
    const Color active_centre = firing ? thread_gold : centre;

    DrawRectangle(x - 9 - spread, y - 2, 7, 3, outline);
    DrawRectangle(x + 3 + spread, y - 2, 7, 3, outline);
    DrawRectangle(x - 2, y - 9 - spread, 3, 7, outline);
    DrawRectangle(x - 2, y + 3 + spread, 3, 7, outline);
    DrawRectangle(x - 8 - spread, y - 1, 6, 1, thread_gold);
    DrawRectangle(x + 3 + spread, y - 1, 6, 1, thread_gold);
    DrawRectangle(x - 1, y - 8 - spread, 1, 6, thread_gold);
    DrawRectangle(x - 1, y + 3 + spread, 1, 6, thread_gold);
    DrawRectangle(x - 1, y - 1, 2, 2, active_centre);
}

// One authoritative simulation tick: move the character, let layers observe the
// results, then ease the camera toward the player.
// A prompt over the item the player can use. It is drawn in canvas space like
// the crosshair, so it scales with the presentation rather than the window.
void draw_interaction_prompt(const Vec2 canvas_position) {
    // Canvas coordinates, not screen: this is drawn inside the scene render
    // target, exactly like the crosshair.
    const int x = static_cast<int>(std::round(canvas_position.x));
    const int y = static_cast<int>(std::round(canvas_position.y));
    constexpr Color shell{8, 14, 18, 215};
    constexpr Color rim{117, 238, 211, 235};
    DrawCircle(x, y, 8.0F, shell);
    DrawCircleLines(x, y, 8.0F, rim);
    constexpr int font_size = 10;
    DrawText("E", x - MeasureText("E", font_size) / 2, y - font_size / 2, font_size, rim);
}

// Returns whether the tick spent the pending interact request, so the caller
// knows whether to keep offering it.
[[nodiscard]] bool advance_fixed_step(
    RuntimeScene& scene, const NavGrid& navigation_grid, NavAgentSystem& navigation_agents,
    Combat& combat, EnemyIntent& enemy_intent, ProjectileSimulation& projectiles, Health& health,
    LayerStack& layers, SceneObservations& observations, CombatObservations& combat_observations,
    EnemyIntentObservations& enemy_observations, ProjectileObservations& projectile_observations,
    HealthObservations& health_observations, Camera25DState& camera, const Vec2 camera_movement,
    const Vec2 player_facing_direction, const float fixed_step_seconds,
    const std::uint64_t completed_ticks, const bool enemy_attack_damage_enabled,
    const MuzzleGeometry muzzle, Interaction& interaction, const bool interact_requested,
    FlowField& crowd_field, const CrowdSteeringPlan& crowd_plan,
    const ActorDebugOverrides& actor_overrides, JobSystem* const jobs) {
    const std::uint64_t tick = completed_ticks + 1;
    // Applied before the tick consumes anything, so a shot taken this tick is
    // the shot the refilled magazine pays for.
    for (const ActorDebugStateSnapshot& override_state : actor_overrides.snapshot().actors) {
        if (override_state.enabled(ActorDebugFlag::infinite_ammo)) {
            static_cast<void>(combat.replenish(override_state.actor));
        }
    }
    combat.fixed_update(tick);
    const DodgeSnapshot dodge = combat.snapshot().dodge;
    const std::vector<CombatEvent> combat_events = combat.drain_events();
    combat_observations.observe(combat_events);
    combat_observations.dodge_invulnerability_observed =
        combat_observations.dodge_invulnerability_observed ||
        combat.invulnerable(scene.player_uuid());
    for (const CombatEvent& event : combat_events) {
        const auto* spawn = std::get_if<ProjectileSpawnedEvent>(&event);
        if (spawn == nullptr || spawn->actor != scene.player_uuid()) {
            continue;
        }
        // The same geometry the aim resolved its origin from, so the shot
        // leaves exactly where the crosshair said it would.
        static_cast<void>(projectiles.spawn(
            *spawn, muzzle_origin(scene.player_position(), spawn->aim_direction, muzzle)));
    }
    // Interaction runs on the tick so a press is resolved against simulated
    // positions, not against whatever the last rendered frame happened to show.
    const bool interact_spent = interaction.fixed_update(
        tick, scene.player_uuid(), scene.player_position(), interact_requested);
    for (const InteractionPerformedEvent& used : interaction.drain_events()) {
        bool accepted = false;
        switch (used.kind) {
        case InteractionKind::pickup_ammo:
            // Combat owns ammo, so it decides how much of the offer it can
            // take. Nothing here knows what a magazine is.
            accepted = combat.resupply(used.actor, static_cast<std::uint32_t>(used.amount)) > 0;
            break;
        }
        if (!accepted) {
            // The item could not be used, so it stays in the world rather than
            // vanishing for nothing.
            static_cast<void>(interaction.decline(used.entity));
            continue;
        }
        static_cast<void>(scene.retire_entity(used.entity));
    }

    projectiles.fixed_update(tick, fixed_step_seconds);

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    auto phase_marker = phase_clock();
#endif
    const EnemyIntentSnapshot enemy_before = enemy_intent.snapshot();
    const HealthSnapshot health_before = health.snapshot();
    std::vector<EnemyPerception> enemy_perceptions;
    enemy_perceptions.reserve(enemy_before.actors.size());
    // A crowd shares one target, so resolving that target's position and
    // liveness per actor repeats the same two lookups thousands of times a
    // tick. Remembering the last one keeps that correct for a scene whose
    // actors do chase different targets while collapsing the common case.
    EntityUuid cached_target{};
    Vec3 cached_target_position{};
    bool cached_target_alive = false;
    for (const EnemyActorIntentSnapshot& actor : enemy_before.actors) {
        const std::optional<Vec3> actor_position = scene.actor_position(actor.actor);
        if (!actor_position) {
            throw std::logic_error{"EnemyIntent actor lost its RuntimeScene binding."};
        }
        if (actor.target != cached_target) {
            const std::optional<Vec3> target_position = scene.actor_position(actor.target);
            if (!target_position) {
                throw std::logic_error{"EnemyIntent target lost its RuntimeScene binding."};
            }
            cached_target = actor.target;
            cached_target_position = *target_position;
            cached_target_alive = health_target_alive(health_before, actor.target);
        }
        enemy_perceptions.push_back({
            .actor = actor.actor,
            .target = actor.target,
            .actor_position = {actor_position->x, actor_position->z},
            .target_position = {cached_target_position.x, cached_target_position.z},
            // A frozen actor is presented to intent as one that cannot act,
            // which is exactly what intent already does with a dead one: no
            // pursuit, no attack, no crowd shuffle. Its health is untouched,
            // so it stays solid and shootable and resumes when released.
            .actor_alive = health_target_alive(health_before, actor.actor) &&
                           !actor_overrides.enabled(actor.actor, ActorDebugFlag::frozen),
            .target_alive = cached_target_alive,
        });
    }
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    fixed_step_phases.perception += phase_elapsed(phase_marker);
#endif
    enemy_intent.fixed_update(tick, enemy_perceptions);
    const std::vector<EnemyIntentEvent> enemy_events = enemy_intent.drain_events();
    enemy_observations.observe(enemy_events);
    const EnemyIntentSnapshot enemy_step = enemy_intent.snapshot();

    // One field serves every attacker converging on the player, so the whole
    // crowd costs one pass over the map rather than one search each.
    const bool crowd_steering = crowd_plan.active;
    if (crowd_steering) {
        const Vec3 target = scene.player_position();
        const std::optional<NavCell> goal = navigation_grid.cell_at({target.x, target.z});
        if (goal && (!crowd_field.built() || crowd_field.goal() != *goal)) {
            // Only when the goal moves to another cell: within one cell the
            // existing field already points the right way.
            static_cast<void>(crowd_field.rebuild(navigation_grid, *goal, {target.x, target.z}));
        }
    }
    const auto field_direction = [&](const EnemyActorIntentSnapshot& actor,
                                     const Vec2 position) -> Vec2 {
        if (!crowd_steering || actor.target != scene.player_uuid()) {
            return {};
        }
        return crowd_field.direction_at(position);
    };

    std::vector<NavAgentRequest> navigation_requests;
    navigation_requests.reserve(enemy_step.actors.size());
    for (const EnemyActorIntentSnapshot& actor : enemy_step.actors) {
        // Crowd actors are not registered with the router when the field is
        // steering them, so they contribute no request either.
        if (!crowd_plan.routes_individually(actor.actor)) {
            continue;
        }
        const auto perception =
            find_by_actor(enemy_perceptions, actor.actor, &EnemyPerception::actor);
        if (perception == enemy_perceptions.end()) {
            throw std::logic_error{"Enemy navigation lost its copied perception request."};
        }
        navigation_requests.push_back({
            .actor = actor.actor,
            .target = actor.target,
            .actor_position = perception->actor_position,
            .target_position = perception->target_position,
            // A crowd-steered actor needs no route of its own, and asking for
            // one would reintroduce exactly the per-actor search the field
            // exists to remove.
            .active = actor.state == EnemyIntentState::pursuing && !crowd_steering,
        });
    }
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    fixed_step_phases.intent += phase_elapsed(phase_marker);
#endif
    const std::vector<NavAgentMotion> navigation_motions =
        navigation_agents.fixed_update(tick, navigation_grid, navigation_requests);
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    fixed_step_phases.navigation += phase_elapsed(phase_marker);
#endif
    // Routes are planned per actor and know nothing about each other, so
    // without a local separation term every Runner converges on the same
    // point and the crowd collapses into one stack of sprites.
    std::vector<CrowdAgent> crowd_agents;
    crowd_agents.reserve(enemy_step.actors.size());
    for (const EnemyActorIntentSnapshot& actor : enemy_step.actors) {
        if (actor.state == EnemyIntentState::inactive) {
            continue;
        }
        const bool field_steered = !crowd_plan.routes_individually(actor.actor);
        auto navigation = navigation_motions.end();
        if (!field_steered) {
            navigation = find_by_actor(navigation_motions, actor.actor, &NavAgentMotion::actor);
            if (navigation == navigation_motions.end()) {
                throw std::logic_error{
                    "Enemy navigation returned no motion for a registered attacker."};
            }
        }
        const auto perception =
            find_by_actor(enemy_perceptions, actor.actor, &EnemyPerception::actor);
        if (perception == enemy_perceptions.end()) {
            throw std::logic_error{"Enemy separation lost its copied perception request."};
        }
        // The field seeds every region, including ones walled off from the
        // target, so a crowd locked out still has a direction. Steering
        // straight at the target is the last resort for an actor the field
        // cannot answer for at all, such as one off the grid entirely; using
        // it more widely makes a crowd converge in radial lines and stack up
        // wherever those lines meet, rather than gathering along the barrier.
        Vec2 desired{};
        if (actor.state == EnemyIntentState::pursuing) {
            desired = field_steered ? field_direction(actor, perception->actor_position)
                                    : navigation->movement_direction;
        }
        if (actor.state == EnemyIntentState::pursuing && desired.x == 0.0F && desired.y == 0.0F) {
            const Vec2 to_target{
                perception->target_position.x - perception->actor_position.x,
                perception->target_position.y - perception->actor_position.y,
            };
            const float length = std::sqrt(to_target.x * to_target.x + to_target.y * to_target.y);
            if (length > 0.0F) {
                desired = {to_target.x / length, to_target.y / length};
            }
        }
        // Routes and fields decide which cells to cross; neither says how
        // close to a wall to pass, and a smoothed route cuts corners on
        // purpose. Bending the direction away from solid ground here is what
        // turns that into rounding the corner instead of scraping it.
        crowd_agents.push_back({
            .actor = actor.actor,
            .position = perception->actor_position,
            .desired_direction =
                nav_avoid_obstacles(navigation_grid, perception->actor_position, desired),
        });
    }
    // Padding, not merely anti-overlap. The authored attacker body is twenty
    // units across, so a radius of a little under two body widths keeps a
    // converging group readable as individuals rather than one stack of
    // sprites, and still collapses to nothing when actors are far apart.
    // The authored attacker body is twenty units across and kinematic, so
    // nothing else keeps two of them apart. Padding a little wider than a body
    // is what makes a converging group read as individuals rather than one
    // overlapping mass.
    constexpr CrowdSeparationSettings enemy_separation{
        .radius = 34.0F,
        .strength = 1.5F,
        .personal_space = 26.0F,
        .contact_strength = 8.0F,
    };
    const std::vector<CrowdSteer> crowd_steers =
        resolve_crowd_separation(crowd_agents, enemy_separation, jobs);
    std::vector<RuntimeSceneActorMotion> actor_motions;
    actor_motions.reserve(crowd_steers.size());
    std::size_t crowd_index = 0;
    for (const EnemyActorIntentSnapshot& actor : enemy_step.actors) {
        if (actor.state == EnemyIntentState::inactive) {
            continue;
        }
        const CrowdSteer& steer = crowd_steers[crowd_index];
        ++crowd_index;
        const bool pursuing = actor.state == EnemyIntentState::pursuing;
        // A Runner that has stopped to attack still shuffles out of an
        // overlap, at a fraction of its pursuit speed, rather than standing
        // inside the actor beside it.
        const float speed = pursuing          ? actor.movement_speed
                            : steer.separated ? actor.movement_speed * separation_shuffle_scale
                                              : 0.0F;
        actor_motions.push_back({
            .actor = steer.actor,
            .world_direction = speed > 0.0F ? steer.direction : Vec2{},
            .speed = speed,
        });
    }
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    fixed_step_phases.separation += phase_elapsed(phase_marker);
#endif
    const GroundMovement25D movement = camera_ground_movement(camera_movement, camera);
    const RuntimeSceneTickResult scene_tick = scene.tick(
        {
            .world_direction = dodge.active ? dodge.direction : movement.world_direction,
            .presentation_direction =
                dodge.active ? world_ground_direction_to_camera(dodge.direction, camera)
                             : player_facing_direction,
            .speed_multiplier = dodge.active ? player_dodge.movement_speed_multiplier : 1.0F,
            .dodging = dodge.active,
            .shot_sequence = combat.snapshot().spawned_projectile_count,
        },
        actor_motions, fixed_step_seconds, jobs);
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    fixed_step_phases.scene_tick += phase_elapsed(phase_marker);
    ++fixed_step_phases.ticks;
#endif
    if (dodge.active) {
        combat_observations.dodge_distance_travelled += scene_tick.player_distance_moved;
        combat_observations.dodge_movement_blocked =
            combat_observations.dodge_movement_blocked || scene_tick.player_blocked;
    }
    observations.observe(scene_tick);
    enemy_observations.observe(scene_tick);
    layers.dispatch(scene_tick.events);
    const ProjectileSimulationSnapshot projectile_step = projectiles.snapshot();
    for (const ProjectileStateSnapshot& projectile : projectile_step.active) {
        const std::optional<RuntimeSceneSegmentHit> hit =
            scene.cast_segment({projectile.previous_position.x, projectile.previous_position.z},
                               {projectile.position.x, projectile.position.z}, projectile.actor);
        if (!hit) {
            continue;
        }
        const float impact_y =
            projectile.previous_position.y +
            (projectile.position.y - projectile.previous_position.y) * hit->fraction;
        static_cast<void>(projectiles.resolve_impact({
            .tick = tick,
            .projectile_id = projectile.projectile_id,
            .target = hit->entity,
            .position = {hit->point.x, impact_y, hit->point.y},
            .normal = hit->normal,
            .tag = hit->tag,
        }));
    }
    projectile_observations.observe(projectiles.drain_expired_events());
    const std::vector<ProjectileImpactEvent> projectile_impacts = projectiles.drain_impact_events();
    projectile_observations.observe(projectile_impacts);
    for (const ProjectileImpactEvent& impact : projectile_impacts) {
        if (!impact.target) {
            continue;
        }
        // Discarded here rather than absorbed by health, so an invulnerable
        // actor leaves no damage event behind for a reader to puzzle over.
        if (actor_overrides.enabled(impact.target, ActorDebugFlag::invulnerable)) {
            continue;
        }
        static_cast<void>(health.submit({
            .tick = tick,
            .hit = {.source = impact.actor, .value = impact.projectile_id},
            .target = impact.target,
            .damage = impact.damage,
        }));
    }
    for (const EnemyIntentEvent& event : enemy_events) {
        const auto* attack = std::get_if<EnemyAttackRequestedEvent>(&event);
        if (attack == nullptr) {
            continue;
        }
        // Stress-test actors still acquire, pursue, reach attack state, and
        // emit cooldown-authoritative attack requests. Only the final damage
        // hand-off is suppressed so performance tests cannot kill the player.
        if (!enemy_attack_damage_enabled) {
            continue;
        }
        if (combat.invulnerable(attack->target)) {
            ++enemy_observations.invulnerable_attacks_rejected;
            continue;
        }
        if (actor_overrides.enabled(attack->target, ActorDebugFlag::invulnerable)) {
            continue;
        }
        static_cast<void>(health.submit({
            .tick = tick,
            .hit = {.source = attack->actor, .value = attack->sequence},
            .target = attack->target,
            .damage = attack->damage,
        }));
    }
    health.fixed_update(tick);
    const std::vector<HealthEvent> health_events = health.drain_events();
    health_observations.observe(health_events);
    enemy_observations.observe_player_damage(health_events, scene.player_uuid());
    for (const HealthEvent& event : health_events) {
        if (const auto* damage = std::get_if<DamageAppliedEvent>(&event)) {
            if (damage->target != scene.player_uuid() && damage->health_after > 0.0F) {
                static_cast<void>(scene.play_actor_hurt(damage->target));
            }
        } else if (const auto* death = std::get_if<ActorDiedEvent>(&event)) {
            // Crowd actors carry runtime identities allocated above the
            // authored space. Counting their retirements separately is what
            // proves a body-less actor can still be hit, killed and removed.
            if (scene.is_crowd_actor(death->target)) {
                ++health_observations.retired_crowd_actor_count;
            }
            // Gameplay retirement remains immediate, but the presentation is
            // allowed to finish its collapse and explosion one-shots first.
            static_cast<void>(scene.begin_actor_death(death->target));
            if (scene.retire_actor(death->target)) {
                ++health_observations.retired_actor_count;
            }
        }
    }
    layers.fixed_update({
        .tick = tick,
        .seconds = fixed_step_seconds,
    });

    constexpr float camera_follow_rate = 8.0F;
    const Vec3 player_position = scene.player_position();
    const float follow_weight = std::min(1.0F, camera_follow_rate * fixed_step_seconds);
    camera.focus.x += (player_position.x - camera.focus.x) * follow_weight;
    camera.focus.z += (player_position.z - camera.focus.z) * follow_weight;
    return interact_spent;
}

[[nodiscard]] bool valid(const ApplicationConfig& config) noexcept {
    const bool valid_pacing = config.render_pacing.mode != RenderPacingMode::fixed_hz ||
                              config.render_pacing.fixed_hz > 0;
    const bool valid_automated_direction =
        !config.automated_movement || (std::isfinite(config.automated_movement_direction.x) &&
                                       std::isfinite(config.automated_movement_direction.y));
    const bool valid_automated_aim =
        !config.automated_aim ||
        (std::isfinite(config.automated_aim_direction.x) &&
         std::isfinite(config.automated_aim_direction.y) &&
         (config.automated_aim_direction.x != 0.0F || config.automated_aim_direction.y != 0.0F));
    return config.window_width > 0 && config.window_height > 0 && config.canvas_width > 0 &&
           config.canvas_height > 0 && valid_pacing && std::isfinite(config.fixed_update_hz) &&
           std::isfinite(config.expected_automated_dodge_distance) &&
           std::isfinite(config.minimum_automated_enemy_distance) &&
           std::isfinite(config.minimum_automated_player_damage) &&
           config.expected_automated_dodge_distance >= 0.0F &&
           config.minimum_automated_enemy_distance >= 0.0F &&
           config.minimum_automated_player_damage >= 0.0F && valid_automated_direction &&
           valid_automated_aim && config.fixed_update_hz > 0.0 && config.max_frames >= 0 &&
           config.capture_frame >= 0;
}

void apply_render_pacing(const RenderPacingConfig& pacing) {
    if (pacing.mode == RenderPacingMode::monitor_synced) {
        SetTargetFPS(0);
        SetWindowState(FLAG_VSYNC_HINT);
        return;
    }

    ClearWindowState(FLAG_VSYNC_HINT);
    SetTargetFPS(pacing.mode == RenderPacingMode::fixed_hz ? pacing.fixed_hz : 0);
}

[[nodiscard]] RenderPacingConfig next_render_pacing(const RenderPacingConfig& current) noexcept {
    if (current.mode == RenderPacingMode::uncapped) {
        return {.mode = RenderPacingMode::monitor_synced, .fixed_hz = 0};
    }
    if (current.mode == RenderPacingMode::monitor_synced) {
        return {.mode = RenderPacingMode::fixed_hz, .fixed_hz = 60};
    }
    if (current.fixed_hz == 60) {
        return {.mode = RenderPacingMode::fixed_hz, .fixed_hz = 120};
    }
    if (current.fixed_hz == 120) {
        return {.mode = RenderPacingMode::fixed_hz, .fixed_hz = 144};
    }
    return {.mode = RenderPacingMode::uncapped, .fixed_hz = 0};
}

[[nodiscard]] std::string render_pacing_description(const RenderPacingConfig& pacing) {
    if (pacing.mode == RenderPacingMode::uncapped) {
        return "UNLOCKED";
    }
    if (pacing.mode == RenderPacingMode::monitor_synced) {
        const int refresh_hz = GetMonitorRefreshRate(GetCurrentMonitor());
        return "MONITOR " + std::to_string(refresh_hz) + " HZ VSYNC";
    }
    return "CAPPED " + std::to_string(pacing.fixed_hz) + " HZ";
}

[[nodiscard]] bool draw_background(const ApplicationConfig& config,
                                   const bool gpu_background_enabled,
                                   const GpuBackdrop& gpu_backdrop) {
    const bool gpu_backdrop_active = gpu_background_enabled && gpu_backdrop.available();
    if (gpu_backdrop_active) {
        gpu_backdrop.draw(config.canvas_width, config.canvas_height);
    } else {
        ClearBackground(Color{18, 24, 36, 255});
    }
    return gpu_backdrop_active;
}

// The first production layer consumes copied simulation events without
// reaching into RuntimeScene. It currently owns smoke/diagnostic observations;
// gameplay and tool layers can use the same typed route later.
class RuntimeObservationLayer final : public Layer {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "Runtime observations"; }

    [[nodiscard]] bool on_event(const EngineEvent& event) override {
        std::visit([this](const auto& typed_event) { observe(typed_event); }, event);
        return false;
    }

    void reset() noexcept {
        physics_contact_observed = false;
        trigger_observed = false;
        animation_event_observed = false;
        animation_identity_observed = false;
        diagonal_animation_observed = false;
    }

    bool physics_contact_observed{false};
    bool trigger_observed{false};
    bool animation_event_observed{false};
    bool animation_identity_observed{false};
    bool diagonal_animation_observed{false};

private:
    void observe(const SceneContactEvent& event) noexcept {
        physics_contact_observed = physics_contact_observed || event.began;
    }

    void observe(const SceneTriggerEvent& event) noexcept {
        trigger_observed = trigger_observed || (event.entered && event.player_visitor);
    }

    void observe(const SceneAnimationEvent& event) noexcept {
        animation_event_observed = true;
        animation_identity_observed =
            animation_identity_observed || static_cast<bool>(event.entity_uuid);
        const bool diagonal_clip =
            event.clip_id == "player-move-southwest" || event.clip_id == "player-move-northwest" ||
            event.clip_id == "player-move-northeast" || event.clip_id == "player-move-southeast";
        diagonal_animation_observed =
            diagonal_animation_observed || (event.entity_id == "player" && diagonal_clip);
    }
};

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
[[nodiscard]] Vector2 canvas_point(const ProjectedPoint25D& projected,
                                   const ApplicationConfig& config) noexcept {
    return {
        static_cast<float>(config.canvas_width) * 0.5F + projected.position.x,
        static_cast<float>(config.canvas_height) * 0.5F + projected.position.y,
    };
}

// The picker and the selection outline mirror the renderer's destination
// rectangle exactly, so what the pointer hits is what the viewport shows.
struct CanvasRect {
    float left{0.0F};
    float top{0.0F};
    float width{0.0F};
    float height{0.0F};
};

[[nodiscard]] CanvasRect sprite_canvas_rect(const EntityBlueprint& entity,
                                            const Camera25DState& camera,
                                            const ApplicationConfig& config) {
    const Vector2 anchor =
        canvas_point(project_world_point(entity.transform.position, camera), config);
    const Sprite2D& sprite = *entity.sprite;
    const float width = std::abs(sprite.size.x * entity.transform.scale.x);
    const float height = std::abs(sprite.size.y * entity.transform.scale.y);
    return {
        anchor.x - sprite.normalized_origin.x * width,
        anchor.y - sprite.normalized_origin.y * height,
        width,
        height,
    };
}

// Resolves a viewport click to the placement the renderer drew on top, using
// the same layer-then-depth order the frame was sorted with.
[[nodiscard]] std::optional<EntityUuid>
pick_entity(const RuntimeScene& scene, const WorldSnapshot& snapshot, const Camera25DState& camera,
            const ApplicationConfig& config, const Vec2 canvas_position) {
    std::optional<EntityUuid> picked;
    std::int32_t picked_layer = 0;
    float picked_depth = 0.0F;
    for (const EntityBlueprint& entity : snapshot.entities) {
        // A dead actor and a used pickup are still in the snapshot so a reset
        // can bring them back, but nothing is drawn where they stood. Clicking
        // the empty ground they left has to reach whatever really is there.
        if (!entity.sprite || !scene.is_entity_presented(entity.uuid)) {
            continue;
        }
        const CanvasRect rect = sprite_canvas_rect(entity, camera, config);
        if (canvas_position.x < rect.left || canvas_position.x > rect.left + rect.width ||
            canvas_position.y < rect.top || canvas_position.y > rect.top + rect.height) {
            continue;
        }
        const float depth = project_world_point(entity.transform.position, camera).depth;
        const bool in_front = !picked || entity.sprite->layer > picked_layer ||
                              (entity.sprite->layer == picked_layer && depth >= picked_depth);
        if (in_front) {
            picked = entity.uuid;
            picked_layer = entity.sprite->layer;
            picked_depth = depth;
        }
    }
    return picked;
}

void draw_selection_outline(const WorldSnapshot& snapshot, const Camera25DState& camera,
                            const ApplicationConfig& config, const EntityUuid selection) {
    for (const EntityBlueprint& entity : snapshot.entities) {
        if (entity.uuid != selection || !entity.sprite) {
            continue;
        }
        const CanvasRect rect = sprite_canvas_rect(entity, camera, config);
        DrawRectangleLinesEx(
            Rectangle{rect.left - 1.0F, rect.top - 1.0F, rect.width + 2.0F, rect.height + 2.0F},
            1.0F, Color{248, 194, 89, 235});
        return;
    }
}

// The viewport shows the running scene, but the gizmo edits the authored
// document, so a drag has nothing to move on screen until it is applied. The
// preview is what closes that gap: the selection's own outline, drawn again at
// the offset the drag has accumulated.
void draw_selection_drag_preview(const WorldSnapshot& snapshot, const Camera25DState& camera,
                                 const ApplicationConfig& config, const EntityUuid selection,
                                 const Vec2 canvas_offset) {
    for (const EntityBlueprint& entity : snapshot.entities) {
        if (entity.uuid != selection || !entity.sprite) {
            continue;
        }
        const CanvasRect rect = sprite_canvas_rect(entity, camera, config);
        DrawRectangleLinesEx(Rectangle{rect.left + canvas_offset.x, rect.top + canvas_offset.y,
                                       rect.width, rect.height},
                             1.0F, Color{117, 238, 211, 245});
        return;
    }
}

void draw_projected_ground_grid(const ApplicationConfig& config, const Camera25DState& camera,
                                const RectXZ& bounds) {
    constexpr float spacing = 64.0F;
    const float first_x = std::ceil(bounds.x / spacing) * spacing;
    const float first_z = std::ceil(bounds.z / spacing) * spacing;
    const float maximum_x = bounds.x + bounds.width;
    const float maximum_z = bounds.z + bounds.depth;
    const Color minor{58, 91, 88, 150};
    const Color major{77, 126, 116, 190};

    int index = 0;
    for (float x = first_x; x <= maximum_x; x += spacing, ++index) {
        const auto start = canvas_point(project_world_point({x, 0.0F, bounds.z}, camera), config);
        const auto end = canvas_point(project_world_point({x, 0.0F, maximum_z}, camera), config);
        DrawLineV(start, end, index % 4 == 0 ? major : minor);
    }

    index = 0;
    for (float z = first_z; z <= maximum_z; z += spacing, ++index) {
        const auto start = canvas_point(project_world_point({bounds.x, 0.0F, z}, camera), config);
        const auto end = canvas_point(project_world_point({maximum_x, 0.0F, z}, camera), config);
        DrawLineV(start, end, index % 4 == 0 ? major : minor);
    }
}
#endif

[[nodiscard]] GroundQuadSubmission2D
project_ground_quad(const std::uint64_t stable_id, const RectXZ& bounds, const float elevation,
                    const ColorRgba8 tint, const Camera25DState& camera) {
    const float maximum_x = bounds.x + bounds.width;
    const float maximum_z = bounds.z + bounds.depth;
    return {
        .stable_id = stable_id,
        .points = {{
            project_world_point({bounds.x, elevation, bounds.z}, camera).position,
            project_world_point({maximum_x, elevation, bounds.z}, camera).position,
            project_world_point({maximum_x, elevation, maximum_z}, camera).position,
            project_world_point({bounds.x, elevation, maximum_z}, camera).position,
        }},
        .tint = tint,
    };
}

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
[[nodiscard]] GroundQuadSubmission2D project_physics_footprint(const PhysicsFootprint& footprint,
                                                               const ColorRgba8 tint,
                                                               const Camera25DState& camera) {
    const float cosine = std::cos(footprint.rotation_radians);
    const float sine = std::sin(footprint.rotation_radians);
    const std::array<Vec2, 4> local_points{{
        {-footprint.half_extents.x, -footprint.half_extents.y},
        {footprint.half_extents.x, -footprint.half_extents.y},
        {footprint.half_extents.x, footprint.half_extents.y},
        {-footprint.half_extents.x, footprint.half_extents.y},
    }};
    GroundQuadSubmission2D projected{
        .stable_id = 50'000ULL + (static_cast<std::uint64_t>(footprint.body.generation) << 32U) +
                     footprint.body.slot,
        .tint = tint,
    };
    for (std::size_t index = 0; index < local_points.size(); ++index) {
        const Vec2 local = local_points[index];
        const float world_x = footprint.center.x + cosine * local.x - sine * local.y;
        const float world_z = footprint.center.y + sine * local.x + cosine * local.y;
        projected.points[index] = project_world_point({world_x, 0.35F, world_z}, camera).position;
    }
    return projected;
}
#endif

// Authored ground: the walkable plane and every elevation surface. Ground
// quads render in submission order, so gameplay surfaces go down first.
void submit_world_ground(RenderQueue2D& queue, const GroundMapDefinition& ground,
                         const Camera25DState& camera) {
    queue.submit_ground(
        project_ground_quad(1, ground.walkable_bounds, 0.0F, {22, 61, 57, 218}, camera));
    std::uint64_t ground_id = 2;
    for (const GroundArea& area : ground.areas) {
        if (area.kind == GroundAreaKind::elevation) {
            queue.submit_ground(project_ground_quad(ground_id++, area.bounds, area.elevation,
                                                    {48, 103, 78, 238}, camera));
        }
    }
}

void submit_scene_sprites(RenderQueue2D& queue, const std::vector<RenderItem2D>& render_items,
                          const Camera25DState& camera) {
    for (const RenderItem2D& item : render_items) {
        // A sprite with no depth span is one billboard at one depth. A spanned
        // sprite is a surface running away from the camera: it is submitted as
        // overlapping slices so that an actor standing beside its middle sorts
        // in front of the near half and behind the far half. Slices share the
        // entity's stable id because their depths already order them, and the
        // id only ever breaks ties between different entities.
        const DepthSlicePlan plan =
            plan_depth_slices(item.transform.position.z, item.sprite.depth_span,
                              item.sprite.size.y * item.transform.scale.y, camera.pitch_degrees);
        for (int slice = 0; slice < plan.count; ++slice) {
            const Vec3 position{
                item.transform.position.x,
                item.transform.position.y,
                plan.first_center_z + plan.step * static_cast<float>(slice),
            };
            const ProjectedPoint25D projected = project_world_point(position, camera);
            queue.submit(SpriteSubmission2D{
                .stable_id = item.entity.value,
                .texture = item.sprite.texture,
                .source = item.sprite.source,
                .flip_x = item.sprite.flip_x,
                .position = projected.position,
                .size = item.sprite.size,
                .scale = {item.transform.scale.x, item.transform.scale.y},
                .normalized_origin = item.sprite.normalized_origin,
                .rotation_degrees = item.sprite.rotation_degrees,
                .sort_depth = projected.depth,
                .tint = item.sprite.tint,
                .layer = item.sprite.layer,
            });
        }
    }
}

void submit_projectile_sprites(RenderQueue2D& queue, const ProjectileSimulationSnapshot& snapshot,
                               const Camera25DState& camera, const float interpolation_alpha) {
    constexpr std::uint64_t projectile_stable_id_base = std::uint64_t{1} << 63U;
    for (const ProjectileStateSnapshot& projectile : snapshot.active) {
        const Vec3 position{
            projectile.previous_position.x +
                (projectile.position.x - projectile.previous_position.x) * interpolation_alpha,
            projectile.previous_position.y +
                (projectile.position.y - projectile.previous_position.y) * interpolation_alpha,
            projectile.previous_position.z +
                (projectile.position.z - projectile.previous_position.z) * interpolation_alpha,
        };
        const ProjectedPoint25D projected = project_world_point(position, camera);
        const ProjectedPoint25D projected_previous =
            project_world_point(projectile.previous_position, camera);
        const float rotation = std::atan2(projected.position.y - projected_previous.position.y,
                                          projected.position.x - projected_previous.position.x) *
                               (180.0F / 3.14159265358979323846F);
        const float screen_delta_x = projected.position.x - projected_previous.position.x;
        const float screen_delta_y = projected.position.y - projected_previous.position.y;
        const float screen_length =
            std::sqrt(screen_delta_x * screen_delta_x + screen_delta_y * screen_delta_y);
        const float inverse_screen_length = screen_length > 0.0001F ? 1.0F / screen_length : 0.0F;
        const Vec2 trail_position{
            projected.position.x - screen_delta_x * inverse_screen_length * 3.0F,
            projected.position.y - screen_delta_y * inverse_screen_length * 3.0F,
        };
        const std::uint64_t stable_id = projectile_stable_id_base + projectile.projectile_id * 4U;
        queue.submit(SpriteSubmission2D{
            .stable_id = stable_id,
            .position = trail_position,
            .size = {std::clamp(screen_length + 2.0F, 7.0F, 14.0F), 5.0F},
            .normalized_origin = {0.5F, 0.5F},
            .rotation_degrees = rotation,
            .sort_depth = projected.depth,
            .tint = {13, 30, 29, 190},
            .layer = 50,
        });
        queue.submit(SpriteSubmission2D{
            .stable_id = stable_id + 1U,
            .position = projected.position,
            .size = {8.0F, 3.0F},
            .normalized_origin = {0.5F, 0.5F},
            .rotation_degrees = rotation,
            .sort_depth = projected.depth,
            .tint = {248, 194, 89, 255},
            .layer = 50,
        });
        queue.submit(SpriteSubmission2D{
            .stable_id = stable_id + 2U,
            .position = projected.position,
            .size = {5.0F, 1.0F},
            .normalized_origin = {0.5F, 0.5F},
            .rotation_degrees = rotation,
            .sort_depth = projected.depth,
            .tint = {195, 255, 240, 255},
            .layer = 50,
        });
        if (projectile.weapon == WeaponKind::needle_pistol &&
            projectile.lifetime_ticks_remaining + 1U == needle_pistol.projectile_lifetime_ticks) {
            queue.submit(SpriteSubmission2D{
                .stable_id = stable_id + 3U,
                .position = projected_previous.position,
                .size = {6.0F, 6.0F},
                .normalized_origin = {0.5F, 0.5F},
                .rotation_degrees = 45.0F,
                .sort_depth = projected_previous.depth,
                .tint = {248, 194, 89, 235},
                .layer = 51,
            });
        }
    }
}

[[nodiscard]] std::vector<Interactable> build_interactables(const SceneDefinition& definition);

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
// Development channels draw over authored ground using their own id range, so
// gameplay submission never has to thread an id counter through debug code.
void submit_navigation_grid(RenderQueue2D& queue, const NavGridSnapshot& navigation,
                            const Camera25DState& camera, const DebugVisuals& debug_visuals) {
    if (!debug_visuals.draws(DebugChannel::navigation_grid)) {
        return;
    }
    constexpr std::uint64_t navigation_id_base = 20'000;
    constexpr float inset = 0.55F;
    const float maximum_x = navigation.bounds.x + navigation.bounds.width;
    const float maximum_z = navigation.bounds.z + navigation.bounds.depth;
    for (std::size_t index = 0; index < navigation.cells.size(); ++index) {
        const NavGridCell& cell = navigation.cells[index];
        const float left =
            std::max(navigation.bounds.x, cell.center.x - navigation.cell_size * 0.5F) + inset;
        const float near_z =
            std::max(navigation.bounds.z, cell.center.y - navigation.cell_size * 0.5F) + inset;
        const float right =
            std::min(maximum_x, cell.center.x + navigation.cell_size * 0.5F) - inset;
        const float far_z =
            std::min(maximum_z, cell.center.y + navigation.cell_size * 0.5F) - inset;
        if (!(right > left) || !(far_z > near_z)) {
            continue;
        }
        const ColorRgba8 tint =
            cell.walkable ? ColorRgba8{74, 222, 190, 65} : ColorRgba8{224, 74, 86, 150};
        queue.submit_ground(project_ground_quad(navigation_id_base + index,
                                                {left, near_z, right - left, far_z - near_z},
                                                cell.elevation + 0.20F, tint, camera));
    }
}

void submit_navigation_path(RenderQueue2D& queue, const NavGridSnapshot& navigation,
                            const NavPathResult& path, const Camera25DState& camera,
                            const DebugVisuals& debug_visuals) {
    if (!debug_visuals.draws(DebugChannel::navigation_path) ||
        path.status != NavPathStatus::found) {
        return;
    }
    constexpr std::uint64_t path_id_base = 30'000;
    constexpr float half_size_scale = 0.28F;
    for (std::size_t index = 0; index < path.cells.size(); ++index) {
        const NavCell path_cell = path.cells[index];
        if (path_cell.column < 0 || path_cell.row < 0 || path_cell.column >= navigation.columns ||
            path_cell.row >= navigation.rows) {
            continue;
        }
        const std::size_t cell_index =
            static_cast<std::size_t>(path_cell.row) * static_cast<std::size_t>(navigation.columns) +
            static_cast<std::size_t>(path_cell.column);
        const NavGridCell& cell = navigation.cells[cell_index];
        const float half_size = navigation.cell_size * half_size_scale;
        const bool start = index == 0;
        const bool goal = index + 1 == path.cells.size();
        const ColorRgba8 tint = start  ? ColorRgba8{74, 222, 190, 235}
                                : goal ? ColorRgba8{224, 90, 205, 235}
                                       : ColorRgba8{248, 194, 89, 205};
        queue.submit_ground(
            project_ground_quad(path_id_base + index,
                                {cell.center.x - half_size, cell.center.y - half_size,
                                 half_size * 2.0F, half_size * 2.0F},
                                cell.elevation + 0.35F, tint, camera));
    }
}

void submit_debug_ground(RenderQueue2D& queue, const GroundMapDefinition& ground,
                         const Camera25DState& camera, const DebugVisuals& debug_visuals,
                         const std::vector<PhysicsFootprint>& footprints) {
    constexpr std::uint64_t debug_ground_id_base = 10'000;
    std::uint64_t ground_id = debug_ground_id_base;
    float maximum_authored_elevation = 0.0F;
    for (const GroundArea& area : ground.areas) {
        maximum_authored_elevation = std::max(maximum_authored_elevation, area.elevation);
    }

    for (const GroundArea& area : ground.areas) {
        if (area.kind == GroundAreaKind::elevation) {
            if (debug_visuals.draws(DebugChannel::elevation_map)) {
                queue.submit_ground(project_ground_quad(
                    ground_id++, area.bounds, area.elevation + 0.12F,
                    debug_elevation_tint(area.elevation, maximum_authored_elevation), camera));
            }
            continue;
        }
        const bool solid_area = area.kind == GroundAreaKind::solid;
        if (!debug_visuals.draws(solid_area ? DebugChannel::collision_shapes
                                            : DebugChannel::trigger_volumes)) {
            continue;
        }
        const ColorRgba8 debug_tint =
            solid_area ? ColorRgba8{128, 58, 49, 155} : ColorRgba8{49, 164, 184, 155};
        queue.submit_ground(
            project_ground_quad(ground_id++, area.bounds, 0.15F, debug_tint, camera));
    }

    for (const PhysicsFootprint& footprint : footprints) {
        if (!debug_visuals.draws(footprint.sensor ? DebugChannel::trigger_volumes
                                                  : DebugChannel::collision_shapes)) {
            continue;
        }
        const ColorRgba8 tint =
            footprint.sensor                                        ? ColorRgba8{55, 204, 222, 80}
            : footprint.motion == PhysicsMotionType::dynamic_body   ? ColorRgba8{248, 194, 89, 115}
            : footprint.motion == PhysicsMotionType::kinematic_body ? ColorRgba8{74, 222, 190, 95}
                                                                    : ColorRgba8{170, 112, 220, 70};
        queue.submit_ground(project_physics_footprint(footprint, tint, camera));
    }
}
#endif

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
void draw_compact_hud(const ApplicationConfig& config, const RunState run_state,
                      const bool dropped_time, const std::string_view pacing_description) {
    DrawText("IC_2DE", 18, 16, 18, Color{74, 222, 190, 255});
    DrawText(TextFormat("%d FPS | %s", GetFPS(), pacing_description.data()),
             config.canvas_width - 218, 17, 10, LIME);
    DrawText("F2 TOOLS  |  F1 WORLD DEBUG", 18, config.canvas_height - 24, 9,
             Color{166, 178, 198, 225});

    // Only a paused run is dimmed and labelled. An editor sitting in edit mode
    // is showing the authored scene, which is the thing an author opened it to
    // look at; covering it with a banner would hide exactly that.
    if (run_state == RunState::paused) {
        DrawRectangle(0, 0, config.canvas_width, config.canvas_height, Fade(BLACK, 0.42F));
        DrawText("PAUSED", config.canvas_width / 2 - 43, config.canvas_height / 2 - 18, 22,
                 RAYWHITE);
        DrawText("Press O to advance one simulation tick", config.canvas_width / 2 - 124,
                 config.canvas_height / 2 + 12, 10, Color{248, 194, 89, 255});
    }

    if (dropped_time) {
        DrawText("FRAME STALL CLAMPED", config.canvas_width - 142, 36, 9, ORANGE);
    }
}
#endif

} // namespace

int run_application(const ApplicationConfig& requested_config) {
    if (!valid(requested_config)) {
        log(LogLevel::error, "Invalid application configuration.");
        return 2;
    }

    ApplicationConfig config = requested_config;
    std::optional<RuntimeProject> runtime_project;
    std::optional<SceneDefinition> scene_definition;
    std::filesystem::path post_process_shader_path;
    try {
        std::filesystem::path scene_path = config.development_scene_path;
        if (!config.runtime_project_manifest.empty()) {
            runtime_project = RuntimeProject::load(config.runtime_project_manifest);
            config.title = runtime_project->name();
            scene_path = runtime_project->start_scene_path();
            log(LogLevel::info, "Runtime project loaded: " + scene_path.string());
        }
        if (scene_path.empty()) {
            throw std::invalid_argument{"No development or packaged scene path was provided."};
        }
        scene_definition = SceneDefinition::load(scene_path);
        post_process_shader_path =
            runtime_project
                ? runtime_project->resolve_asset("shaders/post_process.fs")
                : scene_definition->source_path().parent_path() / "shaders/post_process.fs";
        if (!std::filesystem::is_regular_file(post_process_shader_path)) {
            throw std::runtime_error{"Required post-process shader is missing: " +
                                     post_process_shader_path.string()};
        }
        log(LogLevel::info, "Authored scene validated: " + scene_definition->id() + " (schema " +
                                std::to_string(scene_definition->schema_version()) + ").");
    } catch (const std::exception& error) {
        log(LogLevel::error, std::string{"Runtime content load failed: "} + error.what());
        return 2;
    }
    if (config.validate_content_only) {
        log(LogLevel::info, "Runtime content validation completed without opening a window.");
        return 0;
    }

    unsigned int window_flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE;
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    // The editor supplies its own title bar, so the platform one is dropped
    // rather than left sitting above a dark tool window in the desktop's
    // colours. A run without the editor keeps ordinary window decorations:
    // nothing would be drawing the replacement.
    const bool custom_window_chrome = config.interactive_editor_session;
    if (custom_window_chrome) {
        window_flags |= FLAG_WINDOW_UNDECORATED;
    }
#endif
    if (config.render_pacing.mode == RenderPacingMode::monitor_synced) {
        window_flags |= FLAG_VSYNC_HINT;
    }
    SetConfigFlags(window_flags);
    InitWindow(config.window_width, config.window_height, config.title.c_str());
    if (!IsWindowReady()) {
        log(LogLevel::error, "Raylib could not create the application window.");
        return 3;
    }

    apply_render_pacing(config.render_pacing);
    log(LogLevel::info, "Application window initialized with " +
                            render_pacing_description(config.render_pacing) + ".");

    FramePipeline2D frame_pipeline{
        config.canvas_width,
        config.canvas_height,
        post_process_shader_path,
    };
    if (!frame_pipeline.available()) {
        log(LogLevel::error, "Raylib could not create the off-screen frame pipeline.");
        frame_pipeline.release();
        CloseWindow();
        return 4;
    }
    if (frame_pipeline.post_process_available()) {
        log(LogLevel::info, "External post-process shader and ping-pong target initialized.");
    } else {
        log(LogLevel::warning,
            "Post-process shader could not initialize; presentation will use the scene target.");
    }

    const double fixed_step_seconds = 1.0 / config.fixed_update_hz;
    FixedStepClock clock{fixed_step_seconds};
    InputTracker input_tracker;
    RaylibInputAdapter input_adapter;
    // Worker threads are not a development tool: the crowd work they carry is
    // exactly what a shipping build has to afford too.
    JobSystem jobs;
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    FrameTimeSeries frame_times{240U};
    // Phase accumulators. Frame time alone says a frame was slow; it never
    // says which stage to work on, and at high actor counts the candidates
    // (search, steering, physics, submission, sort, draw) differ by orders of
    // magnitude. Totals are logged once at exit.
    struct PhaseTotals {
        double fixed_update{0.0};
        double submission{0.0};
        double queue_finish{0.0};
        double draw{0.0};
        double digest{0.0};
        double editor_ui{0.0};
        double present{0.0};
        double render_items{0.0};
        double frame_snapshots{0.0};
        std::uint64_t visible_sprites{0};
        std::uint64_t peak_visible_sprites{0};
        std::uint64_t frames{0};
    } phase_totals;
    const auto phase_now = [] { return std::chrono::steady_clock::now(); };
    const auto phase_seconds = [](const std::chrono::steady_clock::time_point start) {
        return std::chrono::duration<double>{std::chrono::steady_clock::now() - start}.count();
    };
#endif
    TextureAssets textures;
    std::unique_ptr<RuntimeScene> scene;
    std::unique_ptr<NavGrid> navigation_grid;
    NavGridSnapshot navigation_grid_snapshot;
    NavPathResult navigation_path;
    NavAgentSystem navigation_agents;
    FlowField crowd_flow_field;
    CrowdSteeringPlan crowd_plan;
    Health health;
    EnemyIntent enemy_intent;
    bool enemy_attack_damage_enabled = true;
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    std::filesystem::path authored_scene_path = scene_definition->source_path();
    // Every .scene beside the loaded one is offered in the editor Debug menu,
    // so adding a scene file needs no shell or application change.
    std::vector<std::filesystem::path> selectable_scenes;
    try {
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator{authored_scene_path.parent_path()}) {
            if (entry.is_regular_file() && entry.path().extension() == ".scene") {
                selectable_scenes.push_back(entry.path());
            }
        }
        std::ranges::sort(selectable_scenes);
    } catch (const std::exception& error) {
        log(LogLevel::warning, std::string{"Authored scene discovery failed: "} + error.what());
    }
#endif
    // Resolved before the definition is moved into the scene, because that move
    // leaves nothing behind to read the authored items from.
    std::vector<Interactable> interactable_places;
    try {
        navigation_grid = std::make_unique<NavGrid>(scene_definition->ground(),
                                                    navigation_grid_settings(*scene_definition));
        navigation_grid_snapshot = navigation_grid->snapshot();
        navigation_path = navigation_reference_path(scene_definition->ground(), *navigation_grid);
        interactable_places = build_interactables(*scene_definition);
        scene = std::make_unique<RuntimeScene>(std::move(*scene_definition), textures);
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        if (config.initial_editor_enemy_stress_count > 0) {
            const EnemyStressSpawnSummary stress = spawn_editor_stress_runners(
                *scene, *navigation_grid, config.initial_editor_enemy_stress_count);
            enemy_attack_damage_enabled = false;
            log(LogLevel::info,
                "Enemy stress scene initialized with " + std::to_string(stress.total()) +
                    " total Runner(s), including " + std::to_string(stress.spawned) +
                    " runtime copy/copies; player damage is disabled.");
        }
#endif
        const WorldSnapshot initial_world = scene->world_snapshot();
        if (initial_world.entities.size() != scene->entity_count()) {
            throw std::logic_error{"Runtime scene World snapshot is incomplete."};
        }
        if (!scene->player_uuid()) {
            throw std::logic_error{"Runtime scene has no stable controllable-player identity."};
        }
        register_scene_health_targets(health, *scene);
        register_scene_enemy_intents(enemy_intent, *scene,
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
                                     config.initial_editor_enemy_stress_count > 0
                                         ? stress_acquisition_range(navigation_grid->topology())
                                         : attacker_acquisition_range
#else
                                     attacker_acquisition_range
#endif
        );
        crowd_plan = plan_crowd_steering(*scene);
        register_scene_navigation_agents(navigation_agents, crowd_plan.routed_actors);
        log(LogLevel::info,
            "Runtime scene instantiated: " + scene->id() + " with " +
                std::to_string(scene->entity_count()) + " entities and " +
                std::to_string(scene->physics_body_count()) + " physics bodies; " +
                std::to_string(scene->animation_binding_count()) + " animated entities; " +
                std::to_string(initial_world.entities.size()) + " stable UUIDs; navigation " +
                std::to_string(navigation_grid_snapshot.columns) + "x" +
                std::to_string(navigation_grid_snapshot.rows) + " with " +
                std::to_string(navigation_grid_snapshot.blocked_cell_count) +
                " hard-blocked cells; reference path " +
                std::string{nav_path_status_name(navigation_path.status)} + " with " +
                std::to_string(navigation_path.cells.size()) + " cells, distance " +
                std::to_string(navigation_path.total_distance) + ", expanded " +
                std::to_string(navigation_path.expanded_cell_count) + ".");
    } catch (const std::exception& error) {
        log(LogLevel::error, std::string{"Runtime scene construction failed: "} + error.what());
        textures.shutdown();
        frame_pipeline.release();
        CloseWindow();
        return 5;
    }

    RaylibRenderer2D renderer{textures};
    GpuBackdrop gpu_backdrop;
    RenderQueue2D render_queue;
    // Aim is a gameplay system, present in every build. The candidate buffer is
    // reused because a stress scene can hold thousands of actors and a fresh
    // allocation every frame would be pure waste.
    bool restart_recovery_performed = false;
    // A tick-named automated dodge is requested exactly once per run.
    bool automated_dodge_requested = false;
    Aiming aiming;
    std::vector<AimTarget> aim_targets;
    // The resolved set is kept beside the module because presentation needs an
    // item's world position to place its prompt, and Interaction deliberately
    // reports identity rather than geometry.
    Interaction interaction;
    static_cast<void>(interaction.load(interactable_places));
    // A press lands on a frame but is resolved on a fixed tick, and a frame
    // often runs no tick at all: above the fixed rate most frames do not. A
    // frame-local flag therefore threw most presses away, which is what made
    // the interact key feel like it needed spamming. The latch holds the press
    // until a tick actually consumes it.
    bool interact_requested = false;
    bool interact_offered = false;
    Camera25DState world_camera = scene->initial_camera();
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    // Presentation and pointer work go through the editor view, which is the
    // gameplay camera itself until someone moves it. Simulation always keeps
    // using world_camera, so looking around never changes what the game does.
    EditorCamera editor_camera;
    // Captured when a gizmo drag begins, so the committed move is one command
    // from the authored start rather than a chain of per-frame nudges.
    std::optional<Vec3> gizmo_drag_origin;
    std::optional<Vec2> gizmo_preview_offset;
    constexpr float gizmo_snap_step = 8.0F;
#endif
    const Camera2DState render_camera{
        .center = {0.0F, 0.0F},
        .rotation_degrees = 0.0F,
        .zoom = 1.0F,
    };
    // An editor session opens in edit mode: the first frame an author sees is
    // the authored scene itself, and the run only begins when they ask for it.
    // Every other run is simulating from its first tick, as it always was.
    RunState run_state = config.interactive_editor_session ? RunState::editing : RunState::running;
    bool gpu_background_enabled = true;
    // Empty in any run nobody has opened the editor on, and free while it is.
    ActorDebugOverrides actor_overrides;
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    // The editor draws its own window chrome, so only a development build has a
    // drag to track. Unlike the run state above, nothing outside the editor
    // reads this.
    WindowChromeDrag window_chrome_drag;
#endif
    bool close_requested = false;
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    // Kills are damage, and damage is deduplicated by hit identity, so each
    // one needs an identity of its own or a second kill would be discarded as
    // a repeat of the first. Only the editor retires an actor by hand.
    std::uint64_t editor_kill_sequence = 0;
    // The editor and its document are created on first request so an ordinary
    // development or smoke run pays nothing for tools it never opens.
    DebugVisuals debug_visuals;
    debug_visuals.set_enabled(config.start_with_debug_visuals);
    debug_visuals.set_channel_selected(DebugChannel::navigation_grid,
                                       config.start_with_navigation_grid_debug);
    debug_visuals.set_channel_selected(DebugChannel::navigation_path,
                                       config.start_with_navigation_path_debug);
    std::optional<EditorShell> editor;
    std::optional<SceneEditor> scene_editor;
#endif
    SceneObservations scene_observations;
    Combat combat;
    CombatObservations combat_observations;
    CombatCommandAdapter combat_command_adapter;
    EnemyIntentObservations enemy_observations;
    ProjectileSimulation projectiles;
    ProjectileObservations projectile_observations;
    HealthObservations health_observations;
    LayerStack runtime_layers;
    auto observation_layer = std::make_unique<RuntimeObservationLayer>();
    RuntimeObservationLayer& observations = *observation_layer;
    static_cast<void>(runtime_layers.push_layer(std::move(observation_layer)));
    int rendered_frames = 0;
    std::uint64_t simulated_ticks = 0;
    std::optional<GameplayStateDigest> latest_gameplay_digest;
    bool gameplay_digest_failed = false;
    bool captured_smoke_frame = false;
    bool smoke_capture_failed = false;
    bool editor_texture_hot_reload_observed = false;
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    std::uint64_t digest_frame_counter = 0;
    TextureReloadSummary texture_reload_totals{};
    double texture_reload_poll_seconds = 0.0;
    RenderDocCapture renderdoc_capture;
    if (renderdoc_capture.available()) {
        log(LogLevel::info, "RenderDoc detected: automatic captures every " +
                                std::to_string(config.renderdoc_capture_interval_seconds) +
                                " seconds.");
    }
#endif

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    const auto editor_ready = [&]() {
        if (!editor) {
            editor.emplace(config.editor_layout_path);
        }
        if (editor->available() && !scene_editor) {
            try {
                scene_editor.emplace(SceneDocument::open(authored_scene_path));
            } catch (const std::exception& error) {
                log(LogLevel::error,
                    std::string{"Editor could not open the authored scene: "} + error.what());
            }
        }
        return editor->available() && scene_editor.has_value();
    };
    if (config.start_with_editor && editor_ready()) {
        editor->set_visible(true);
        log(LogLevel::info, "Development editor shown.");
        if (config.enable_editor_texture_hot_reload) {
            log(LogLevel::info, "Editor texture hot swap enabled.");
        }
    }
#endif

    while (!WindowShouldClose() && !close_requested &&
           (config.max_frames == 0 || rendered_frames < config.max_frames) &&
           (config.max_fixed_ticks == 0 || simulated_ticks < config.max_fixed_ticks)) {
        const double frame_seconds = static_cast<double>(GetFrameTime());
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        renderdoc_capture.tick(config.renderdoc_capture_interval_seconds, frame_seconds);
        static_cast<void>(frame_times.record_seconds(frame_seconds));
        const FrameTimingSummary frame_timing = frame_times.summary();
        if (config.enable_editor_texture_hot_reload && editor && editor->visible()) {
            texture_reload_poll_seconds += frame_seconds;
            constexpr double texture_reload_poll_interval_seconds = 0.2;
            if (texture_reload_poll_seconds >= texture_reload_poll_interval_seconds) {
                texture_reload_poll_seconds = 0.0;
                const TextureReloadSummary reload = textures.reload_changed_files();
                texture_reload_totals.watched = reload.watched;
                texture_reload_totals.changed += reload.changed;
                texture_reload_totals.reloaded += reload.reloaded;
                texture_reload_totals.failed += reload.failed;
                editor_texture_hot_reload_observed |= reload.reloaded > 0;
            }
        } else {
            texture_reload_poll_seconds = 0.0;
        }
#endif
        InputSample input_sample = input_adapter.sample();
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        if (editor && editor->blocks_gameplay_input()) {
            // A panel text field owns the keyboard. Mouse ownership is handled
            // later against the actual viewport so clicking to fire cannot
            // erase held movement axes.
            input_sample = InputSample{
                .toggle_post_process = input_sample.toggle_post_process,
                .toggle_debug_visuals = input_sample.toggle_debug_visuals,
                .toggle_editor = input_sample.toggle_editor,
            };
        }
#endif
        if (config.automated_aim) {
            input_sample.gameplay.aim = {
                .horizontal = config.automated_aim_direction.x,
                .depth = config.automated_aim_direction.y,
                .pointer_active = false,
            };
        }
        if (config.automated_fire_hold_ticks > 0) {
            // Held fire is a range and Combat latches it, so evaluating it once
            // per frame is exact: the command is submitted before the frame's
            // ticks run, and the release lands on the first tick at or past the
            // boundary whatever the frame rate.
            input_sample.gameplay.set(GameplayAction::fire,
                                      simulated_ticks < config.automated_fire_hold_ticks);
        }
        // A dodge names one exact tick, and a frame runs zero, one, or several
        // ticks: at 30 Hz simulated_ticks skips values, so the frame whose
        // count matched exactly could never occur and the dodge was silently
        // dropped. Triggering on the first frame at or past the tick, once,
        // makes the same run reproduce at any frame rate.
        if (config.automated_dodge_tick > 0 && !automated_dodge_requested &&
            simulated_ticks + 1 >= config.automated_dodge_tick) {
            input_sample.gameplay.set(GameplayAction::dodge, true);
            automated_dodge_requested = true;
        }
        const InputFrame input = input_tracker.update(input_sample);

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        if (input.pause.pressed) {
            run_state = simulating(run_state) ? RunState::paused : RunState::running;
            clock.reset();
        }
        const auto forget_observations = [&]() {
            scene_observations.forget();
            observations.reset();
            combat.reset();
            combat_observations.reset();
            combat_command_adapter.reset();
            automated_dodge_requested = false;
            enemy_intent.reset();
            navigation_agents.reset();
            enemy_observations.reset();
            projectiles.reset();
            projectile_observations.reset();
            health.reset();
            health_observations.reset();
            simulated_ticks = 0;
            clock.reset();
        };
        if (input.reset.pressed) {
            scene->reset();
            interaction.reset();
            interact_requested = false;
            interact_offered = false;
            world_camera = scene->initial_camera();
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
            editor_camera.attach();
#endif
            forget_observations();
        }
        if (input.toggle_debug_visuals.pressed) {
            debug_visuals.toggle();
            log(LogLevel::info,
                debug_visuals.enabled() ? "Debug visuals enabled." : "Debug visuals disabled.");
        }
        // Escape cancels editing inside the shell; it must not quit the game.
        SetExitKey(editor && editor->visible() ? KEY_NULL : KEY_ESCAPE);
        if (input.toggle_editor.pressed && editor_ready()) {
            editor->toggle_visible();
            log(LogLevel::info,
                editor->visible() ? "Development editor shown." : "Development editor hidden.");
        }
        if (input.cycle_render_pacing.pressed) {
            config.render_pacing = next_render_pacing(config.render_pacing);
            apply_render_pacing(config.render_pacing);
            log(LogLevel::info, "Render pacing changed to " +
                                    render_pacing_description(config.render_pacing) + ".");
        }
        if (input.toggle_gpu_background.pressed) {
            gpu_background_enabled = !gpu_background_enabled;
        }
        if (input.toggle_post_process.pressed) {
            config.post_process.enabled = !config.post_process.enabled;
            log(LogLevel::info, config.post_process.enabled ? "Post-processing enabled."
                                                            : "Post-processing bypassed.");
        }
#endif

        bool combat_editor_visible = false;
        std::optional<Vec2> editor_canvas_pointer;
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        combat_editor_visible = editor && editor->visible();
        if (combat_editor_visible) {
            editor_canvas_pointer = editor->viewport_pointer_canvas();
        }
#endif
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        const Camera25DState view_camera = editor_camera.resolve(world_camera);
#else
        const Camera25DState& view_camera = world_camera;
#endif
        const std::optional<Vec2> combat_canvas_pointer =
            pointer_canvas_point(input.gameplay.aim, combat_editor_visible, editor_canvas_pointer,
                                 config.canvas_width, config.canvas_height);
        // Everything shootable and alive, other than the player. Aim assist
        // reads copied facts; it never reaches into the scene.
        aim_targets.clear();
        if (!config.automated_aim) {
            for (const HealthTargetSnapshot& target : health.snapshot().targets) {
                if (!target.alive || target.target == scene->player_uuid()) {
                    continue;
                }
                if (const std::optional<Vec3> position = scene->actor_position(target.target)) {
                    aim_targets.push_back({.actor = target.target, .position = *position});
                }
            }
        }
        const Vec3 aim_actor_position = scene->player_position();
        const AimingSnapshot& aim_snapshot = aiming.resolve(
            AimingInputs{
                .actor_position = aim_actor_position,
                .pointer_active = input.gameplay.aim.pointer_active,
                .pointer_world_point =
                    combat_canvas_pointer
                        ? std::optional<Vec3>{canvas_ground_point(
                              *combat_canvas_pointer, aim_actor_position.y, config.canvas_width,
                              config.canvas_height, view_camera)}
                        : std::nullopt,
                .stick_world = stick_aim_world(input.gameplay.aim, view_camera),
                .delta_seconds = static_cast<float>(frame_seconds),
                .direct = config.automated_aim,
            },
            aim_targets);
        const std::optional<Vec2> combat_aim =
            aim_snapshot.aiming ? std::optional<Vec2>{aim_snapshot.direction} : std::nullopt;
        const Vec2 camera_movement{
            config.automated_movement ? config.automated_movement_direction.x
                                      : input.move_horizontal,
            config.automated_movement ? config.automated_movement_direction.y : input.move_depth,
        };
        const GroundMovement25D frame_movement =
            camera_ground_movement(camera_movement, world_camera);
        const GroundMovement25D idle_facing = camera_ground_movement({0.0F, 1.0F}, world_camera);
        const CombatCommand combat_command = combat_command_adapter.translate(
            scene->player_uuid(), input, combat_aim, frame_movement.world_direction,
            idle_facing.world_direction,
            !input.gameplay.aim.pointer_active || combat_canvas_pointer.has_value());
        if (!combat_command.empty()) {
            static_cast<void>(combat.submit(combat_command));
        }
        if (input.gameplay.aim.pointer_active && combat_canvas_pointer) {
            HideCursor();
        } else {
            ShowCursor();
        }

        TickPlan tick_plan{};
        if (!simulating(run_state)) {
            clock.reset();
            // Single-stepping advances a run that is under way. Edit mode has
            // no run to advance, and stepping one out of it would leave the
            // scene in a state the document does not describe.
            if (run_state == RunState::paused && input.step_simulation.pressed) {
                tick_plan.fixed_steps = 1;
            }
        } else {
            tick_plan = clock.advance(frame_seconds);
        }

        // A press is an edge, but the tick that resolves it can land before the
        // actor is in range: the key is pressed on the approach, or simply
        // held. Throwing the press away on the next tick regardless left the
        // prompt showing over an item that could not be used until the key was
        // released and pressed again. The press is instead held until a tick
        // actually spends it, and dropped on release once some tick has had
        // the chance to. `offered` is what makes the release safe: at a high
        // refresh rate a quick tap can begin and end between two fixed ticks,
        // and that press must still reach one.
        const ButtonState interact = input.gameplay.action(GameplayAction::interact);
        if (interact.pressed) {
            interact_requested = true;
            interact_offered = false;
        }
        if (!interact.down && interact_offered) {
            interact_requested = false;
            interact_offered = false;
        }
        Vec2 player_facing_direction = camera_movement;
        if (const std::optional<Vec2> aim = combat_command_adapter.aim_direction()) {
            player_facing_direction = world_ground_direction_to_camera(*aim, world_camera);
        }

        const std::uint64_t ticks_before_frame = simulated_ticks;
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        const auto fixed_update_started = phase_now();
#endif
        for (std::uint32_t step = 0;
             step < tick_plan.fixed_steps &&
             (config.max_fixed_ticks == 0 || simulated_ticks < config.max_fixed_ticks);
             ++step) {
            const bool interact_spent = advance_fixed_step(
                *scene, *navigation_grid, navigation_agents, combat, enemy_intent, projectiles,
                health, runtime_layers, scene_observations, combat_observations, enemy_observations,
                projectile_observations, health_observations, world_camera, camera_movement,
                player_facing_direction, static_cast<float>(fixed_step_seconds), simulated_ticks,
                enemy_attack_damage_enabled, aiming.config().muzzle, interaction,
                interact_requested, crowd_flow_field, crowd_plan, actor_overrides, &jobs);
            // Exactly one tick may spend a press, so holding the key must not
            // empty a room. An unspent press stays pending; the tick having
            // seen it is what lets the release drop it.
            if (interact_spent) {
                interact_requested = false;
                interact_offered = false;
            } else if (interact_requested) {
                interact_offered = true;
            }
            ++simulated_ticks;
        }
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        phase_totals.fixed_update += phase_seconds(fixed_update_started);
#endif

        bool refresh_digest = config.report_gameplay_state_digest;
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        // Building the digest snapshots and hashes every entity, actor,
        // projectile and health target in the scene. An automated run validates
        // it on every tick and must keep doing so, but the editor only displays
        // it, and at crowd scale a per-frame refresh costs more than drawing
        // the frame does. A few times a second is far more than a reader can
        // follow, so the displayed copy is refreshed on an interval.
        constexpr std::uint64_t editor_digest_interval_frames = 20;
        refresh_digest =
            refresh_digest || (editor && editor->visible() &&
                               digest_frame_counter % editor_digest_interval_frames == 0);
        ++digest_frame_counter;
#endif
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        const auto digest_started = phase_now();
#endif
        if (refresh_digest && simulated_ticks != ticks_before_frame && !gameplay_digest_failed) {
            try {
                latest_gameplay_digest = gameplay_state_digest({
                    .world = scene->world_snapshot(),
                    .combat = combat.snapshot(),
                    .enemy_intent = enemy_intent.snapshot(),
                    .navigation = navigation_agents.snapshot(),
                    .projectiles = projectiles.snapshot(),
                    .health = health.snapshot(),
                });
            } catch (const std::exception& error) {
                gameplay_digest_failed = true;
                log(LogLevel::error, std::string{"Gameplay state digest failed: "} + error.what());
            }
        }
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        phase_totals.digest += phase_seconds(digest_started);
#endif

        const float interpolation_alpha = static_cast<float>(tick_plan.interpolation_alpha);
        const Vec3 current_player_position = scene->player_position();
        const std::optional<Vec2> crosshair_position = crosshair_canvas_position(
            input.gameplay.aim, combat_canvas_pointer,
            aim_snapshot.aiming ? std::optional<Vec3>{aim_snapshot.aim_point} : std::nullopt,
            config.canvas_width, config.canvas_height, view_camera);
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        const std::string pacing_description = render_pacing_description(config.render_pacing);
#endif
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        const auto render_items_started = phase_now();
#endif
        // Generous enough that the tallest authored sprite, an elevated one and
        // a tick of movement all stay inside it.
        constexpr float visible_region_margin = 256.0F;
        const std::vector<RenderItem2D> render_items = scene->collect_render_items(
            interpolation_alpha, visible_world_region(view_camera, config.canvas_width,
                                                      config.canvas_height, visible_region_margin));
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        phase_totals.render_items += phase_seconds(render_items_started);
        const auto frame_snapshots_started = phase_now();
#endif
        const ProjectileSimulationSnapshot projectile_snapshot = projectiles.snapshot();
        const HealthSnapshot health_snapshot = health.snapshot();
        const EnemyIntentSnapshot enemy_snapshot = enemy_intent.snapshot();
        const NavAgentSnapshot navigation_agent_snapshot = navigation_agents.snapshot();
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        phase_totals.frame_snapshots += phase_seconds(frame_snapshots_started);
#endif
        const NavPathResult visible_navigation_path =
            displayed_navigation_path(navigation_path, navigation_agent_snapshot);

        // Re-read the ground definition every frame because the editor can
        // replace the running scene between frames.
        const GroundMapDefinition& ground_definition = scene->ground_definition();
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        const auto submission_started = phase_now();
#endif
        render_queue.begin(render_camera, config.canvas_width, config.canvas_height);
        submit_world_ground(render_queue, ground_definition, view_camera);
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        submit_navigation_grid(render_queue, navigation_grid_snapshot, view_camera, debug_visuals);
        submit_navigation_path(render_queue, navigation_grid_snapshot, visible_navigation_path,
                               view_camera, debug_visuals);
        submit_debug_ground(render_queue, ground_definition, view_camera, debug_visuals,
                            scene->debug_footprints());
#endif
        submit_scene_sprites(render_queue, render_items, view_camera);
        submit_projectile_sprites(render_queue, projectile_snapshot, view_camera,
                                  interpolation_alpha);
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        phase_totals.submission += phase_seconds(submission_started);
        const auto queue_finish_started = phase_now();
#endif
        const RenderFrame2D render_frame = render_queue.finish();
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        phase_totals.queue_finish += phase_seconds(queue_finish_started);
        const auto draw_started = phase_now();
#endif

        frame_pipeline.begin_scene();
        const bool gpu_backdrop_active =
            draw_background(config, gpu_background_enabled, gpu_backdrop);
        static_cast<void>(gpu_backdrop_active);
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        if (debug_visuals.draws(DebugChannel::world_grid)) {
            draw_projected_ground_grid(config, view_camera, ground_definition.walkable_bounds);
        }
#endif
        const RenderDiagnostics2D render_diagnostics =
            renderer.render(render_frame, config.canvas_width, config.canvas_height);
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        phase_totals.draw += phase_seconds(draw_started);
        // What actually survived the canvas-space cull. Pre-filtering in world
        // space must not change this number: if it drops, the filter is
        // discarding sprites that would have been drawn.
        phase_totals.visible_sprites += render_diagnostics.visible_sprites;
        phase_totals.peak_visible_sprites =
            std::max(phase_totals.peak_visible_sprites,
                     static_cast<std::uint64_t>(render_diagnostics.visible_sprites));
        ++phase_totals.frames;
#endif
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        if (debug_visuals.draws(DebugChannel::stats_overlay)) {
            draw_compact_hud(config, run_state, tick_plan.dropped_time, pacing_description);
        }
        // An outline around a dead actor is an outline around nothing: the
        // sprite it was drawn to frame stopped being rendered when the actor
        // was retired. The selection itself is kept, so the Inspector can still
        // report that the actor is dead and offer the way to bring it back.
        if (editor && editor->visible() && editor->selection() &&
            scene->is_entity_presented(editor->selection())) {
            draw_selection_outline(scene->world_snapshot(), view_camera, config,
                                   editor->selection());
            if (gizmo_preview_offset) {
                draw_selection_drag_preview(scene->world_snapshot(), view_camera, config,
                                            editor->selection(), *gizmo_preview_offset);
            }
        }
#else
        static_cast<void>(gpu_backdrop_active);
        static_cast<void>(render_diagnostics);
#endif
        if (aim_snapshot.assisted_target) {
            if (const std::optional<Vec3> assisted =
                    scene->actor_position(*aim_snapshot.assisted_target)) {
                // Framed on the body rather than the feet, which is where an
                // actor's ground position sits.
                constexpr float assist_marker_height = 17.0F;
                const Vec3 framed{assisted->x, assisted->y + assist_marker_height, assisted->z};
                if (const std::optional<Vec2> marker = canvas_position_of(
                        framed, config.canvas_width, config.canvas_height, view_camera)) {
                    draw_assist_marker(*marker, input.gameplay.action(GameplayAction::fire).down);
                }
            }
        }
        if (const std::optional<InteractionCandidate>& usable = interaction.snapshot().candidate) {
            const auto place =
                std::ranges::find(interactable_places, usable->entity, &Interactable::entity);
            if (place != interactable_places.end()) {
                constexpr float prompt_height = 26.0F;
                const Vec3 prompt{place->position.x, place->position.y + prompt_height,
                                  place->position.z};
                if (const std::optional<Vec2> marker = canvas_position_of(
                        prompt, config.canvas_width, config.canvas_height, view_camera)) {
                    draw_interaction_prompt(*marker);
                }
            }
        }
        if (crosshair_position) {
            draw_pixel_crosshair(*crosshair_position, config.canvas_width, config.canvas_height,
                                 input.gameplay.action(GameplayAction::fire).down);
        }
        frame_pipeline.finish_scene({
            .enabled = config.post_process.enabled,
            .exposure = config.post_process.exposure,
            .saturation = config.post_process.saturation,
            .vignette_strength = config.post_process.vignette_strength,
        });

        const CanvasViewport viewport = compute_canvas_viewport(
            GetScreenWidth(), GetScreenHeight(), config.canvas_width, config.canvas_height);
        const RectF destination{viewport.x, viewport.y, viewport.width, viewport.height};

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        const bool editor_visible = editor && editor->visible() && scene_editor;
        EditorActions editor_actions{};
#else
        constexpr bool editor_visible = false;
#endif

        BeginDrawing();
        ClearBackground(BLACK);
        if (!editor_visible) {
            frame_pipeline.present(destination);
        }
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        const auto editor_ui_started = phase_now();
        // Projecting the selection here keeps every camera detail on this side
        // of the seam; the panel receives canvas pixels and nothing else.
        struct GizmoAnchor {
            std::optional<Vec2> canvas_point;
            Vec2 axis_x_canvas{1.0F, 0.0F};
            Vec2 axis_z_canvas{0.0F, 1.0F};
            bool movable{false};
        } gizmo_anchor;
        if (editor_visible && editor->selection() &&
            scene->is_entity_presented(editor->selection())) {
            const WorldSnapshot selection_snapshot = scene->world_snapshot();
            const auto found = std::ranges::find(selection_snapshot.entities, editor->selection(),
                                                 &EntityBlueprint::uuid);
            if (found != selection_snapshot.entities.end()) {
                const Vec3 anchored = found->transform.position;
                const Vector2 centre =
                    canvas_point(project_world_point(anchored, view_camera), config);
                const Vector2 along_x = canvas_point(
                    project_world_point({anchored.x + 1.0F, anchored.y, anchored.z}, view_camera),
                    config);
                const Vector2 along_z = canvas_point(
                    project_world_point({anchored.x, anchored.y, anchored.z + 1.0F}, view_camera),
                    config);
                gizmo_anchor.canvas_point = Vec2{centre.x, centre.y};
                gizmo_anchor.axis_x_canvas = {along_x.x - centre.x, along_x.y - centre.y};
                gizmo_anchor.axis_z_canvas = {along_z.x - centre.x, along_z.y - centre.y};
                const std::vector<SceneDocumentEntity> authored = scene_editor->entities();
                const auto record =
                    std::ranges::find(authored, editor->selection(), &SceneDocumentEntity::uuid);
                gizmo_anchor.movable = record != authored.end() && !record->physics_bound;
            }
        }

        if (editor_visible) {
            editor_actions = editor->draw(
                *scene_editor, debug_visuals,
                EditorStats{
                    .frames_per_second = GetFPS(),
                    .fixed_update_hz = config.fixed_update_hz,
                    .simulated_ticks = simulated_ticks,
                    .entity_count = scene->entity_count(),
                    .physics_body_count = scene->physics_body_count(),
                    .cpu_worker_count = jobs.worker_count(),
                    .loaded_texture_count = textures.loaded_texture_count(),
                    .visible_sprites = render_diagnostics.visible_sprites,
                    .culled_sprites = render_diagnostics.culled_sprites,
                    .estimated_batches = render_diagnostics.estimated_batches,
                    .estimated_draw_calls = render_diagnostics.estimated_draw_calls,
                    .visible_vertices = render_diagnostics.visible_vertices,
                    .frame_time_p50_ms = frame_timing.p50_milliseconds,
                    .frame_time_p95_ms = frame_timing.p95_milliseconds,
                    .frame_time_p99_ms = frame_timing.p99_milliseconds,
                    .estimated_gpu_passes = frame_pipeline.diagnostics().estimated_gpu_passes,
                    .render_target_switches = frame_pipeline.diagnostics().render_target_switches,
                    .shader_passes = frame_pipeline.diagnostics().shader_passes,
                    .watched_texture_count = texture_reload_totals.watched,
                    .successful_texture_reloads = texture_reload_totals.reloaded,
                    .failed_texture_reloads = texture_reload_totals.failed,
                    .gameplay_digest = latest_gameplay_digest,
                    .navigation_grid = navigation_grid_snapshot,
                    .navigation_path = visible_navigation_path,
                    .navigation_agents = navigation_agent_snapshot,
                    .input = input,
                    .aim = aim_snapshot,
                    .interaction = interaction.snapshot(),
                    .combat = combat.snapshot(),
                    .observed_combat_intents = combat_observations.intent_count,
                    .last_combat_intent = combat_observations.last_intent,
                    .last_dodge = combat_observations.last_dodge,
                    .dodge_distance_travelled = combat_observations.dodge_distance_travelled,
                    .dodge_movement_blocked = combat_observations.dodge_movement_blocked,
                    .observed_projectiles = combat_observations.projectile_count,
                    .last_projectile = combat_observations.last_projectile,
                    .projectiles = projectile_snapshot,
                    .last_expired_projectile = projectile_observations.last_expired,
                    .last_projectile_impact = projectile_observations.last_impact,
                    .enemy_intent = enemy_snapshot,
                    .last_enemy_acquisition = enemy_observations.last_acquisition,
                    .last_enemy_attack = enemy_observations.last_attack,
                    .enemy_distance_travelled = enemy_observations.distance_travelled,
                    .enemy_damage_applied_to_player = enemy_observations.damage_applied_to_player,
                    .enemy_attack_damage_enabled = enemy_attack_damage_enabled,
                    .enemy_movement_blocked = enemy_observations.movement_blocked,
                    .invulnerable_enemy_attacks_rejected =
                        enemy_observations.invulnerable_attacks_rejected,
                    .health = health_snapshot,
                    .last_damage = health_observations.last_damage,
                    .last_death = health_observations.last_death,
                    .post_process_active = frame_pipeline.diagnostics().post_process_active,
                    .post_process_available = frame_pipeline.diagnostics().post_process_available,
                    .texture_hot_reload_enabled = config.enable_editor_texture_hot_reload,
                    .run_state = run_state,
                    .custom_window_chrome = custom_window_chrome,
                    .window_maximized = IsWindowMaximized(),
                    .player_uuid = scene->player_uuid(),
                    .actor_debug = actor_overrides.snapshot(),
                    .camera_detached = editor_camera.detached(),
                    .selection_canvas_point = gizmo_anchor.canvas_point,
                    .selection_axis_x_canvas = gizmo_anchor.axis_x_canvas,
                    .selection_axis_z_canvas = gizmo_anchor.axis_z_canvas,
                    .selection_movable = gizmo_anchor.movable,
                    .grid_snap_step = gizmo_snap_step,
                    .selectable_scenes = selectable_scenes,
                    .loaded_scene = authored_scene_path,
                },
                EditorCanvas{
                    .texture_id = frame_pipeline.output_texture_id(),
                    .width = frame_pipeline.width(),
                    .height = frame_pipeline.height(),
                });
        }
#endif
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        phase_totals.editor_ui += phase_seconds(editor_ui_started);
        const auto present_started = phase_now();
#endif
        EndDrawing();
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        phase_totals.present += phase_seconds(present_started);
#endif
        ++rendered_frames;

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        const auto commit_editor_runtime = [&](EditorRuntimeSceneCandidate candidate) {
            // The replacement allocates its own runtime identities, so every
            // override held over the outgoing scene names an actor that no
            // longer exists. Keeping them would leave the panel reporting
            // overrides nothing is subject to.
            actor_overrides.clear_all();
            enemy_attack_damage_enabled = candidate.enemy_attack_damage_enabled;
            scene = std::move(candidate.scene);
            navigation_grid = std::move(candidate.navigation_grid);
            navigation_grid_snapshot = std::move(candidate.navigation_grid_snapshot);
            navigation_path = std::move(candidate.navigation_path);
            health = std::move(candidate.health);
            enemy_intent = std::move(candidate.enemy_intent);
            navigation_agents = std::move(candidate.navigation_agents);
            crowd_plan = std::move(candidate.crowd_plan);
            // The replacement carries its own grid, so a field built against
            // the old topology is meaningless and must not be consulted.
            crowd_flow_field = FlowField{};
            // The replacement scene carries its own items, so the used set from
            // the previous one is meaningless.
            interactable_places = std::move(candidate.interactables);
            static_cast<void>(interaction.load(interactable_places));
            world_camera = scene->initial_camera();
            editor_camera.attach();
            forget_observations();
        };
        apply_window_chrome(editor_actions.window, window_chrome_drag);
        if (editor_actions.window.close) {
            close_requested = true;
        }
        if (editor_actions.set_run_state && *editor_actions.set_run_state != run_state) {
            run_state = *editor_actions.set_run_state;
            clock.reset();
        }
        for (const EditorActions::ActorDebugRequest& request :
             editor_actions.actor_debug_requests) {
            if (!actor_overrides.set(request.actor, request.flag, request.enabled)) {
                log(LogLevel::warning, "Editor requested an override for an unusable actor.");
            }
        }
        if (editor_actions.kill_actor) {
            // Damage rather than retirement, so the actor dies through exactly
            // the path a bullet uses: the same death event, the same retirement,
            // the same observations. Full maximum health guarantees it lands
            // however hurt the actor already was.
            const HealthSnapshot health_now = health.snapshot();
            const auto target = std::ranges::find(health_now.targets, *editor_actions.kill_actor,
                                                  &HealthTargetSnapshot::target);
            if (target == health_now.targets.end() || !target->alive) {
                log(LogLevel::warning, "Editor asked to kill an actor with no live health.");
            } else if (!health.submit({
                           .tick = health_now.tick + 1,
                           .hit = {.source = *editor_actions.kill_actor,
                                   .value = ++editor_kill_sequence},
                           .target = *editor_actions.kill_actor,
                           .damage = target->maximum_health,
                       })) {
                log(LogLevel::warning, "Editor kill request was refused by health.");
            }
        }
        // Restart once, as soon as something has died, and keep running so the
        // shutdown check can require a second death from the revived actor.
        if (config.validate_restart_recovery && !restart_recovery_performed &&
            health.snapshot().death_count > 0) {
            restart_recovery_performed = true;
            scene->reset();
            interaction.reset();
            interact_requested = false;
            interact_offered = false;
            world_camera = scene->initial_camera();
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
            editor_camera.attach();
#endif
            forget_observations();
            log(LogLevel::info, "Restart-recovery validation restarted the running scene.");
        }
        if (editor_actions.reset_running_scene) {
            scene->reset();
            interaction.reset();
            interact_requested = false;
            interact_offered = false;
            world_camera = scene->initial_camera();
            editor_camera.attach();
            forget_observations();
            // Restart puts the authored scene back, which is edit mode by
            // definition. Without this an editor could reach edit mode only by
            // being relaunched, and the state it opens in would be one an
            // author could never get back to.
            if (config.interactive_editor_session) {
                run_state = RunState::editing;
                clock.reset();
            }
        }
        if (editor_actions.viewport_picked && editor) {
            // Clicking empty ground clears the selection rather than keeping a
            // stale one the pointer is no longer over.
            const std::optional<EntityUuid> picked =
                pick_entity(*scene, scene->world_snapshot(), view_camera, config,
                            editor_actions.viewport_pick_canvas_point);
            // Clicking what is already selected lets go of it, so the pointer
            // that made a selection is also the one that undoes it, without
            // having to hunt for empty ground.
            const EntityUuid resolved = picked.value_or(EntityUuid{});
            editor->select_entity(resolved == editor->selection() ? EntityUuid{} : resolved);
        }
        if (editor_actions.apply_document_to_running_scene && scene_editor) {
            try {
                // Play mode consumes a validated copy of the edited document;
                // the authored file is untouched until the editor saves it.
                EditorRuntimeSceneCandidate candidate =
                    make_editor_runtime_scene_candidate(scene_editor->runtime_copy(), textures, 0);
                commit_editor_runtime(std::move(candidate));
                log(LogLevel::info, "Applied the edited scene document to the running scene: " +
                                        std::to_string(scene->entity_count()) + " entities.");
            } catch (const std::exception& error) {
                log(LogLevel::error,
                    std::string{"Applying the edited scene document failed: "} + error.what());
            }
        }
        // A drag reports its total canvas offset every frame. The offset is
        // previewed as it moves and committed once, on release, so one drag is
        // one undo step.
        gizmo_preview_offset.reset();
        if (editor_actions.gizmo_drag && scene_editor && editor && editor->selection()) {
            const EditorActions::GizmoDrag drag = *editor_actions.gizmo_drag;
            if (!gizmo_drag_origin) {
                const std::vector<SceneDocumentEntity> authored = scene_editor->entities();
                const auto record =
                    std::ranges::find(authored, editor->selection(), &SceneDocumentEntity::uuid);
                if (record != authored.end() && !record->physics_bound) {
                    gizmo_drag_origin = record->position;
                }
            }
            if (gizmo_drag_origin) {
                const Vec3 offset = canvas_ground_offset_to_world(drag.canvas_offset, view_camera);
                Vec3 moved{
                    gizmo_drag_origin->x + offset.x,
                    gizmo_drag_origin->y,
                    gizmo_drag_origin->z + offset.z,
                };
                if (drag.snap && gizmo_snap_step > 0.0F) {
                    moved.x = std::round(moved.x / gizmo_snap_step) * gizmo_snap_step;
                    moved.z = std::round(moved.z / gizmo_snap_step) * gizmo_snap_step;
                }
                if (drag.finished) {
                    try {
                        static_cast<void>(
                            scene_editor->move_unbound_entity(editor->selection(), moved));
                    } catch (const std::exception& error) {
                        log(LogLevel::error, std::string{"Gizmo move rejected: "} + error.what());
                    }
                    gizmo_drag_origin.reset();
                } else {
                    // Preview in canvas space, which is where the outline is
                    // drawn, so a snapped position visibly snaps.
                    const Vector2 from =
                        canvas_point(project_world_point(*gizmo_drag_origin, view_camera), config);
                    const Vector2 to =
                        canvas_point(project_world_point(moved, view_camera), config);
                    gizmo_preview_offset = Vec2{to.x - from.x, to.y - from.y};
                }
            }
        } else {
            gizmo_drag_origin.reset();
        }

        editor_camera.pan(editor_actions.camera_pan_canvas, world_camera);
        editor_camera.zoom(editor_actions.camera_zoom_notches, world_camera);
        if (editor_actions.camera_follow_player) {
            editor_camera.attach();
        }
        if (editor_actions.camera_frame_selection && editor && editor->selection()) {
            const WorldSnapshot snapshot = scene->world_snapshot();
            const auto found =
                std::ranges::find(snapshot.entities, editor->selection(), &EntityBlueprint::uuid);
            if (found != snapshot.entities.end()) {
                editor_camera.frame(found->transform.position, world_camera);
            }
        }
        if (editor_actions.load_scene_path) {
            const std::filesystem::path requested = *editor_actions.load_scene_path;
            try {
                SceneEditor replacement{SceneDocument::open(requested)};
                EditorRuntimeSceneCandidate candidate =
                    make_editor_runtime_scene_candidate(replacement.runtime_copy(), textures, 0);
                commit_editor_runtime(std::move(candidate));
                scene_editor = std::move(replacement);
                authored_scene_path = requested;
                editor->select_entity(EntityUuid{});
                log(LogLevel::info, "Loaded authored scene " + requested.filename().string() +
                                        ": " + std::to_string(scene->entity_count()) +
                                        " entities.");
            } catch (const std::exception& error) {
                log(LogLevel::error,
                    std::string{"Loading the requested scene failed: "} + error.what());
            }
        }
        if (editor_actions.enemy_stress_target_count && scene_editor) {
            try {
                const std::size_t requested = *editor_actions.enemy_stress_target_count;
                EditorRuntimeSceneCandidate candidate = make_editor_runtime_scene_candidate(
                    scene_editor->runtime_copy(), textures, requested);
                const EnemyStressSpawnSummary stress = candidate.stress;
                commit_editor_runtime(std::move(candidate));
                log(LogLevel::info,
                    "Enemy stress scene rebuilt with " + std::to_string(stress.total()) +
                        " total Runner(s), including " + std::to_string(stress.spawned) +
                        " runtime copy/copies; requested " + std::to_string(requested) +
                        (enemy_attack_damage_enabled ? "; normal player damage restored."
                                                     : "; player damage is disabled."));
            } catch (const std::exception& error) {
                log(LogLevel::error,
                    std::string{"Enemy stress scene rebuild failed: "} + error.what());
            }
        }
#endif

        const bool reached_capture_frame =
            config.capture_frame > 0 && rendered_frames >= config.capture_frame;
        const bool reached_capture_tick =
            config.capture_tick > 0 && simulated_ticks >= config.capture_tick;
        const bool reached_texture_hot_reload =
            config.close_after_editor_texture_hot_reload && editor_texture_hot_reload_observed;
        if (!captured_smoke_frame && !config.capture_path.empty() &&
            (reached_capture_frame || reached_capture_tick || reached_texture_hot_reload)) {
            TakeScreenshot(config.capture_path.c_str());
            if (std::filesystem::is_regular_file(config.capture_path)) {
                log(LogLevel::info, "Captured runtime smoke-test frame.");
            } else {
                log(LogLevel::error, "Runtime smoke-test frame could not be written.");
                smoke_capture_failed = true;
            }
            captured_smoke_frame = true;
            if (reached_texture_hot_reload) {
                break;
            }
        }
    }

    if (config.report_gameplay_state_digest && !gameplay_digest_failed) {
        try {
            latest_gameplay_digest = gameplay_state_digest({
                .world = scene->world_snapshot(),
                .combat = combat.snapshot(),
                .enemy_intent = enemy_intent.snapshot(),
                .navigation = navigation_agents.snapshot(),
                .projectiles = projectiles.snapshot(),
                .health = health.snapshot(),
            });
        } catch (const std::exception& error) {
            gameplay_digest_failed = true;
            log(LogLevel::error,
                std::string{"Final gameplay state digest failed: "} + error.what());
        }
    }
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    // Release the editor context and font atlas while the window still exists.
    editor.reset();
    // Where the crowd actually ended up, by compass sector around the player.
    // A crowd that should surround its target but reports most of its actors in
    // one or two sectors is being funnelled, and the counts say by how much.
    {
        const std::vector<EntityUuid> crowd = scene->actor_uuids(ScenePhysicsRole::attacker);
        if (crowd.size() >= 64) {
            const Vec3 centre = scene->player_position();
            std::array<std::size_t, 8> sectors{};
            for (const EntityUuid actor : crowd) {
                const std::optional<Vec3> position = scene->actor_position(actor);
                if (!position) {
                    continue;
                }
                const float angle = std::atan2(position->z - centre.z, position->x - centre.x);
                constexpr float two_pi = 6.28318530718F;
                const float turns = (angle < 0.0F ? angle + two_pi : angle) / two_pi;
                const auto sector = static_cast<std::size_t>(turns * 8.0F) % 8U;
                ++sectors[sector];
            }
            std::string report;
            constexpr std::array<const char*, 8> names{
                "E", "SE", "S", "SW", "W", "NW", "N", "NE",
            };
            for (std::size_t index = 0; index < sectors.size(); ++index) {
                report += std::string{names[index]} + " " + std::to_string(sectors[index]) + "  ";
            }
            log(LogLevel::info, "Crowd sector distribution: " + report);

            // How tightly the crowd is actually packed. Actors are 20 by 12
            // world units, so a nearest neighbour closer than that means bodies
            // are sharing space rather than merely standing close, which is the
            // difference between a dense crowd and separation failing.
            std::vector<Vec2> positions;
            positions.reserve(crowd.size());
            for (const EntityUuid actor : crowd) {
                if (const std::optional<Vec3> position = scene->actor_position(actor)) {
                    positions.push_back({position->x, position->z});
                }
            }
            constexpr float probe_cell = 32.0F;
            std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> buckets;
            const auto key_of = [](const Vec2 point) {
                const auto column = static_cast<std::int32_t>(std::floor(point.x / probe_cell));
                const auto row = static_cast<std::int32_t>(std::floor(point.y / probe_cell));
                return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(column)) << 32U) |
                       static_cast<std::uint64_t>(static_cast<std::uint32_t>(row));
            };
            for (std::uint32_t index = 0; index < positions.size(); ++index) {
                buckets[key_of(positions[index])].push_back(index);
            }
            double total_nearest = 0.0;
            std::size_t measured = 0;
            std::size_t overlapping = 0;
            float closest_seen = std::numeric_limits<float>::max();
            for (std::uint32_t index = 0; index < positions.size(); ++index) {
                const Vec2 self = positions[index];
                float nearest = std::numeric_limits<float>::max();
                const auto column = static_cast<std::int32_t>(std::floor(self.x / probe_cell));
                const auto row = static_cast<std::int32_t>(std::floor(self.y / probe_cell));
                for (std::int32_t dr = -1; dr <= 1; ++dr) {
                    for (std::int32_t dc = -1; dc <= 1; ++dc) {
                        const std::uint64_t key =
                            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(column + dc))
                             << 32U) |
                            static_cast<std::uint64_t>(static_cast<std::uint32_t>(row + dr));
                        const auto found = buckets.find(key);
                        if (found == buckets.end()) {
                            continue;
                        }
                        for (const std::uint32_t other : found->second) {
                            if (other == index) {
                                continue;
                            }
                            const float dx = self.x - positions[other].x;
                            const float dz = self.y - positions[other].y;
                            nearest = std::min(nearest, std::sqrt(dx * dx + dz * dz));
                        }
                    }
                }
                if (nearest < std::numeric_limits<float>::max()) {
                    total_nearest += static_cast<double>(nearest);
                    ++measured;
                    closest_seen = std::min(closest_seen, nearest);
                    if (nearest < 12.0F) {
                        ++overlapping;
                    }
                }
            }
            if (measured > 0) {
                log(LogLevel::info,
                    "Crowd packing: mean nearest neighbour " +
                        std::to_string(total_nearest / static_cast<double>(measured)) +
                        ", closest " + std::to_string(closest_seen) + ", bodies overlapping " +
                        std::to_string(overlapping) + " of " + std::to_string(measured) + ".");
            }
        }
    }
    if (fixed_step_phases.ticks > 0) {
        const auto tick_ms = [&](const double total) {
            return total * 1000.0 / static_cast<double>(fixed_step_phases.ticks);
        };
        log(LogLevel::info,
            "Fixed step averages over " + std::to_string(fixed_step_phases.ticks) +
                " tick(s): perception " + std::to_string(tick_ms(fixed_step_phases.perception)) +
                " ms, intent " + std::to_string(tick_ms(fixed_step_phases.intent)) +
                " ms, navigation " + std::to_string(tick_ms(fixed_step_phases.navigation)) +
                " ms, separation " + std::to_string(tick_ms(fixed_step_phases.separation)) +
                " ms, scene tick " + std::to_string(tick_ms(fixed_step_phases.scene_tick)) +
                " ms.");
    }
    if (phase_totals.frames > 0) {
        const auto average_ms = [&](const double total) {
            return total * 1000.0 / static_cast<double>(phase_totals.frames);
        };
        log(LogLevel::info,
            "Frame phase averages over " + std::to_string(phase_totals.frames) +
                " frame(s): fixed update " + std::to_string(average_ms(phase_totals.fixed_update)) +
                " ms, sprite submission " + std::to_string(average_ms(phase_totals.submission)) +
                " ms, queue sort " + std::to_string(average_ms(phase_totals.queue_finish)) +
                " ms, draw " + std::to_string(average_ms(phase_totals.draw)) +
                " ms, gameplay digest " + std::to_string(average_ms(phase_totals.digest)) +
                " ms, editor UI " + std::to_string(average_ms(phase_totals.editor_ui)) +
                " ms, present " + std::to_string(average_ms(phase_totals.present)) +
                " ms, render items " + std::to_string(average_ms(phase_totals.render_items)) +
                " ms, frame snapshots " + std::to_string(average_ms(phase_totals.frame_snapshots)) +
                " ms.");
        log(LogLevel::info, "Visible sprites: mean " +
                                std::to_string(phase_totals.visible_sprites / phase_totals.frames) +
                                ", peak " + std::to_string(phase_totals.peak_visible_sprites) +
                                ".");
    }
    scene_editor.reset();
#endif
    const std::uint64_t completed_terminal_animations =
        scene->completed_actor_terminal_animation_count();
    gpu_backdrop.release();
    scene.reset();
    const bool texture_lifetime_valid = textures.loaded_texture_count() == 0;
    textures.shutdown();
    frame_pipeline.release();
    ShowCursor();
    CloseWindow();
    log(LogLevel::info,
        "Completed " + std::to_string(simulated_ticks) + " fixed simulation ticks.");
    log(LogLevel::info, "Final camera X/Z: " + std::to_string(world_camera.focus.x) + "/" +
                            std::to_string(world_camera.focus.z) + ".");
    const bool validate_automated_route =
        config.automated_movement && config.validate_automated_route;
    if (config.report_gameplay_state_digest) {
        if (gameplay_digest_failed || !latest_gameplay_digest) {
            log(LogLevel::error,
                "Gameplay replay completed without a valid fixed-boundary digest.");
            return 21;
        }
        log(LogLevel::info, "Gameplay state digest v" +
                                std::to_string(latest_gameplay_digest->schema_version) + ": " +
                                std::to_string(latest_gameplay_digest->value) + ".");
    }
    const int automated_run_exit_code = exit_code(evaluate_automated_run({
        .automated_movement = validate_automated_route,
        .collision_observed = scene_observations.collision_observed,
        .elevation_observed = scene_observations.elevation_observed,
        .physics_contact_observed = observations.physics_contact_observed,
        .trigger_observed = observations.trigger_observed,
        .dynamic_prop_moved = scene_observations.dynamic_prop_moved,
        .animation_event_observed = observations.animation_event_observed,
        .animation_identity_observed = observations.animation_identity_observed,
        .diagonal_animation_observed = observations.diagonal_animation_observed,
        .texture_lifetime_valid = texture_lifetime_valid,
        .smoke_capture_failed = smoke_capture_failed,
    }));
    if (automated_run_exit_code != 0) {
        return automated_run_exit_code;
    }
    const std::uint64_t projectile_spawn_count = combat.snapshot().spawned_projectile_count;
    if (projectile_spawn_count < config.minimum_automated_projectile_spawns) {
        log(LogLevel::error, "Automated held-fire validation spawned only " +
                                 std::to_string(projectile_spawn_count) +
                                 " projectile(s); expected at least " +
                                 std::to_string(config.minimum_automated_projectile_spawns) + ".");
        return 12;
    }
    if (config.minimum_automated_projectile_spawns > 0) {
        log(LogLevel::info, "Automated held-fire validation passed with " +
                                std::to_string(projectile_spawn_count) + " projectile spawns.");
    }
    const std::uint64_t projectile_impact_count = projectiles.snapshot().total_impacted;
    if (projectile_impact_count < config.minimum_automated_projectile_impacts) {
        log(LogLevel::error, "Automated projectile-impact validation resolved only " +
                                 std::to_string(projectile_impact_count) +
                                 " impact(s); expected at least " +
                                 std::to_string(config.minimum_automated_projectile_impacts) + ".");
        return 13;
    }
    if (config.minimum_automated_projectile_impacts > 0) {
        log(LogLevel::info, "Automated projectile-impact validation passed with " +
                                std::to_string(projectile_impact_count) + " resolved impact(s).");
    }
    const std::uint64_t target_death_count = health.snapshot().death_count;
    if (target_death_count < config.minimum_automated_target_deaths) {
        log(LogLevel::error, "Automated target-health validation observed only " +
                                 std::to_string(target_death_count) +
                                 " death(s); expected at least " +
                                 std::to_string(config.minimum_automated_target_deaths) + ".");
        return 14;
    }
    if (config.validate_restart_recovery) {
        if (!restart_recovery_performed) {
            log(LogLevel::error,
                "Restart-recovery validation never observed a death to restart from.");
            return 26;
        }
        if (target_death_count == 0) {
            log(LogLevel::error,
                "Restart-recovery validation observed no death after the restart: a revived "
                "actor was not shootable again.");
            return 26;
        }
        log(LogLevel::info, "Restart-recovery validation passed with " +
                                std::to_string(target_death_count) +
                                " death(s) after restarting the running scene.");
    }
    if (config.minimum_automated_target_deaths > 0) {
        if (health_observations.retired_actor_count < target_death_count) {
            log(LogLevel::error,
                "Automated target-health validation emitted death without retiring its actor.");
            return 15;
        }
        log(LogLevel::info, "Automated target-health validation passed with " +
                                std::to_string(target_death_count) +
                                " deterministic death(s) and matching scene retirement(s).");
    }
    if (completed_terminal_animations < config.minimum_automated_terminal_animation_completions) {
        throw std::runtime_error{
            "Automated target-health validation completed only " +
            std::to_string(completed_terminal_animations) +
            " terminal animation sequence(s); expected at least " +
            std::to_string(config.minimum_automated_terminal_animation_completions) + "."};
    }
    if (config.minimum_automated_terminal_animation_completions > 0) {
        log(LogLevel::info, "Automated terminal-presentation validation passed with " +
                                std::to_string(completed_terminal_animations) +
                                " completed death-to-explosion sequence(s).");
    }
    if (health_observations.retired_crowd_actor_count <
        config.minimum_automated_crowd_actor_retirements) {
        log(LogLevel::error, "Automated crowd-kill validation retired only " +
                                 std::to_string(health_observations.retired_crowd_actor_count) +
                                 " body-less actor(s); expected at least " +
                                 std::to_string(config.minimum_automated_crowd_actor_retirements) +
                                 ".");
        return 30;
    }
    if (config.minimum_automated_crowd_actor_retirements > 0) {
        log(LogLevel::info, "Automated crowd-kill validation passed with " +
                                std::to_string(health_observations.retired_crowd_actor_count) +
                                " body-less actor(s) hit, killed and retired.");
    }
    const DodgeSnapshot dodge = combat.snapshot().dodge;
    if (dodge.started_count < config.minimum_automated_dodge_starts) {
        log(LogLevel::error, "Automated dodge validation observed only " +
                                 std::to_string(dodge.started_count) +
                                 " start(s); expected at least " +
                                 std::to_string(config.minimum_automated_dodge_starts) + ".");
        return 16;
    }
    if (config.minimum_automated_dodge_starts > 0 &&
        !combat_observations.dodge_invulnerability_observed) {
        log(LogLevel::error,
            "Automated dodge validation never observed the player as invulnerable.");
        return 17;
    }
    if (config.minimum_automated_dodge_starts > 0 && dodge.active) {
        log(LogLevel::error,
            "Automated dodge validation ended before the authored dodge duration expired.");
        return 18;
    }
    if (config.expected_automated_dodge_distance > 0.0F) {
        constexpr float distance_tolerance = 0.05F;
        const float distance_error = std::abs(combat_observations.dodge_distance_travelled -
                                              config.expected_automated_dodge_distance);
        if (distance_error > distance_tolerance) {
            log(LogLevel::error, "Automated dodge movement travelled " +
                                     std::to_string(combat_observations.dodge_distance_travelled) +
                                     " world units; expected " +
                                     std::to_string(config.expected_automated_dodge_distance) +
                                     ".");
            return 19;
        }
        if (combat_observations.dodge_movement_blocked) {
            log(LogLevel::error,
                "Automated open-ground dodge unexpectedly reported blocked movement.");
            return 20;
        }
    }
    if (config.minimum_automated_dodge_starts > 0) {
        log(LogLevel::info, "Automated dodge validation passed with " +
                                std::to_string(dodge.started_count) +
                                " fixed-tick start(s), an observed invulnerability window, "
                                "completed duration, and " +
                                std::to_string(combat_observations.dodge_distance_travelled) +
                                " world units of collision-resolved travel.");
    }
    const EnemyIntentSnapshot enemy_result = enemy_intent.snapshot();
    if (enemy_result.acquisition_count < config.minimum_automated_enemy_acquisitions) {
        log(LogLevel::error, "Automated enemy-intent validation observed only " +
                                 std::to_string(enemy_result.acquisition_count) +
                                 " acquisition(s); expected at least " +
                                 std::to_string(config.minimum_automated_enemy_acquisitions) + ".");
        return 22;
    }
    if (enemy_result.attack_count < config.minimum_automated_enemy_attacks) {
        log(LogLevel::error, "Automated enemy-intent validation observed only " +
                                 std::to_string(enemy_result.attack_count) +
                                 " attack request(s); expected at least " +
                                 std::to_string(config.minimum_automated_enemy_attacks) + ".");
        return 23;
    }
    if (enemy_observations.distance_travelled < config.minimum_automated_enemy_distance) {
        log(LogLevel::error, "Automated moving-attacker validation travelled only " +
                                 std::to_string(enemy_observations.distance_travelled) +
                                 " world units; expected at least " +
                                 std::to_string(config.minimum_automated_enemy_distance) + ".");
        return 24;
    }
    if (enemy_observations.damage_applied_to_player < config.minimum_automated_player_damage) {
        log(LogLevel::error, "Automated moving-attacker validation applied only " +
                                 std::to_string(enemy_observations.damage_applied_to_player) +
                                 " player damage; expected at least " +
                                 std::to_string(config.minimum_automated_player_damage) + ".");
        return 25;
    }
    if (config.require_automated_zero_player_damage &&
        enemy_observations.damage_applied_to_player != 0.0F) {
        log(LogLevel::error, "Automated harmless-enemy validation applied " +
                                 std::to_string(enemy_observations.damage_applied_to_player) +
                                 " player damage; expected exactly zero.");
        return 29;
    }
    if (config.require_automated_zero_player_damage) {
        log(LogLevel::info, "Automated harmless-enemy validation passed with zero player damage.");
    }
    if (config.minimum_automated_enemy_acquisitions > 0 ||
        config.minimum_automated_enemy_attacks > 0 ||
        config.minimum_automated_enemy_distance > 0.0F ||
        config.minimum_automated_player_damage > 0.0F) {
        log(LogLevel::info,
            "Automated moving-attacker validation passed with " +
                std::to_string(enemy_result.acquisition_count) + " acquisition(s), " +
                std::to_string(enemy_result.attack_count) + " attack request(s), " +
                std::to_string(enemy_observations.distance_travelled) +
                " world units of collision-resolved travel, and " +
                std::to_string(enemy_observations.damage_applied_to_player) + " player damage.");
    }
    const NavAgentSnapshot navigation_result = navigation_agents.snapshot();
    if (navigation_result.actors.size() < config.minimum_automated_navigation_agents) {
        log(LogLevel::error, "Automated navigation validation retained only " +
                                 std::to_string(navigation_result.actors.size()) +
                                 " agent(s); expected at least " +
                                 std::to_string(config.minimum_automated_navigation_agents) + ".");
        return 28;
    }
    if (navigation_result.total_search_count < config.minimum_automated_navigation_searches) {
        log(LogLevel::error, "Automated navigation validation performed only " +
                                 std::to_string(navigation_result.total_search_count) +
                                 " search(es); expected at least " +
                                 std::to_string(config.minimum_automated_navigation_searches) +
                                 ".");
        return 26;
    }
    std::uint64_t waypoint_advances = 0;
    for (const NavAgentStateSnapshot& actor : navigation_result.actors) {
        waypoint_advances += actor.waypoint_advance_count;
    }
    if (waypoint_advances < config.minimum_automated_navigation_waypoint_advances) {
        log(LogLevel::error,
            "Automated navigation validation advanced only " + std::to_string(waypoint_advances) +
                " waypoint(s); expected at least " +
                std::to_string(config.minimum_automated_navigation_waypoint_advances) + ".");
        return 27;
    }
    if (config.minimum_automated_navigation_searches > 0 ||
        config.minimum_automated_navigation_waypoint_advances > 0 ||
        config.minimum_automated_navigation_agents > 0) {
        log(LogLevel::info, "Automated navigation validation passed with " +
                                std::to_string(navigation_result.actors.size()) + " agent(s), " +
                                std::to_string(navigation_result.total_search_count) +
                                " bounded search(es) and " + std::to_string(waypoint_advances) +
                                " waypoint advance(s).");
    }
    return 0;
}

} // namespace ic2d
