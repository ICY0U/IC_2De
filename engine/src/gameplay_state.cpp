#include "ic2d/gameplay_state.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace ic2d {
namespace {

class StableHasher final {
public:
    void append(const std::uint64_t value) noexcept {
        constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
        for (std::uint32_t byte_index = 0; byte_index < 8; ++byte_index) {
            value_ ^= (value >> (byte_index * 8U)) & 0xFFULL;
            value_ *= fnv_prime;
        }
    }

    void append_float(const float value) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument{"Gameplay state contains a non-finite number."};
        }
        const float canonical = value == 0.0F ? 0.0F : value;
        append(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(canonical)));
    }

    [[nodiscard]] std::uint64_t value() const noexcept { return value_; }

private:
    std::uint64_t value_{14'695'981'039'346'656'037ULL};
};

void append_vec2(StableHasher& hash, const Vec2& value) {
    hash.append_float(value.x);
    hash.append_float(value.y);
}

void append_vec3(StableHasher& hash, const Vec3& value) {
    hash.append_float(value.x);
    hash.append_float(value.y);
    hash.append_float(value.z);
}

void append_bool(StableHasher& hash, const bool value) noexcept {
    hash.append(value ? 1ULL : 0ULL);
}

template <typename Value, typename Identity>
[[nodiscard]] std::vector<const Value*> canonical_order(const std::vector<Value>& values,
                                                        Identity identity, const char* diagnostic) {
    std::vector<const Value*> ordered;
    ordered.reserve(values.size());
    for (const Value& value : values) {
        if (identity(value) == 0) {
            throw std::invalid_argument{diagnostic};
        }
        ordered.push_back(&value);
    }
    std::ranges::sort(ordered, {}, [&identity](const Value* value) { return identity(*value); });
    for (std::size_t index = 1; index < ordered.size(); ++index) {
        if (identity(*ordered[index - 1]) == identity(*ordered[index])) {
            throw std::invalid_argument{diagnostic};
        }
    }
    return ordered;
}

void append_world(StableHasher& hash, const WorldSnapshot& world) {
    constexpr std::uint64_t section = 0x574F524C44ULL; // WORLD
    hash.append(section);
    const auto entities = canonical_order(
        world.entities, [](const EntityBlueprint& entity) { return entity.uuid.value; },
        "Gameplay world snapshots require unique non-zero entity identities.");
    hash.append(entities.size());
    for (const EntityBlueprint* entity : entities) {
        hash.append(entity->uuid.value);
        append_vec3(hash, entity->transform.position);
        hash.append_float(entity->transform.heading_degrees);
        append_vec3(hash, entity->transform.scale);
    }
}

void append_weapon(StableHasher& hash, const WeaponSnapshot& weapon) {
    hash.append(static_cast<std::uint64_t>(weapon.weapon));
    hash.append(weapon.magazine_ammo);
    hash.append(weapon.reserve_ammo);
    hash.append(weapon.fire_cooldown_ticks_remaining);
    hash.append(weapon.reload_ticks_remaining);
    append_bool(hash, weapon.reloading);
}

void append_dodge(StableHasher& hash, const DodgeSnapshot& dodge) {
    append_vec2(hash, dodge.direction);
    hash.append(dodge.active_ticks_remaining);
    hash.append(dodge.invulnerable_ticks_remaining);
    hash.append(dodge.cooldown_ticks_remaining);
    append_bool(hash, dodge.active);
    append_bool(hash, dodge.invulnerable);
    hash.append(dodge.started_count);
}

void append_combat(StableHasher& hash, const CombatSnapshot& combat) {
    constexpr std::uint64_t section = 0x434F4D424154ULL; // COMBAT
    hash.append(section);
    hash.append(combat.tick);
    hash.append(combat.consumed_command_count);
    hash.append(combat.emitted_intent_count);
    hash.append(combat.pending_command_count);
    hash.append(combat.latest_actor.value);
    append_vec2(hash, combat.aim_direction);
    append_weapon(hash, combat.weapon);
    append_dodge(hash, combat.dodge);
    hash.append(combat.spawned_projectile_count);
    hash.append(combat.next_event_sequence);
    hash.append(combat.next_projectile_id);

    const auto actors = canonical_order(
        combat.actors, [](const CombatActorSnapshot& actor) { return actor.actor.value; },
        "Gameplay Combat snapshots require unique non-zero actor identities.");
    hash.append(actors.size());
    bool latest_actor_found = !combat.latest_actor;
    for (const CombatActorSnapshot* actor : actors) {
        hash.append(actor->actor.value);
        append_vec2(hash, actor->aim_direction);
        append_weapon(hash, actor->weapon);
        append_dodge(hash, actor->dodge);
        append_bool(hash, actor->fire_held);
        latest_actor_found = latest_actor_found || actor->actor == combat.latest_actor;
    }
    if (!latest_actor_found) {
        throw std::invalid_argument{
            "The latest Combat actor must appear in the complete actor snapshot."};
    }
}

void append_projectiles(StableHasher& hash, const ProjectileSimulationSnapshot& projectiles) {
    constexpr std::uint64_t section = 0x50524F4A454354ULL; // PROJECT
    hash.append(section);
    hash.append(projectiles.tick);
    hash.append(projectiles.total_spawned);
    hash.append(projectiles.total_expired);
    hash.append(projectiles.total_impacted);
    hash.append(projectiles.pending_spawn_count);
    const auto active = canonical_order(
        projectiles.active,
        [](const ProjectileStateSnapshot& projectile) { return projectile.projectile_id; },
        "Gameplay projectile snapshots require unique non-zero projectile identities.");
    hash.append(active.size());
    for (const ProjectileStateSnapshot* projectile : active) {
        if (!projectile->actor) {
            throw std::invalid_argument{
                "Gameplay projectile snapshots require non-zero actor identities."};
        }
        hash.append(projectile->projectile_id);
        hash.append(projectile->actor.value);
        hash.append(static_cast<std::uint64_t>(projectile->weapon));
        append_vec3(hash, projectile->previous_position);
        append_vec3(hash, projectile->position);
        append_vec2(hash, projectile->direction);
        hash.append_float(projectile->speed);
        hash.append(projectile->lifetime_ticks_remaining);
        hash.append_float(projectile->damage);
    }
}

void append_enemy_intent(StableHasher& hash, const EnemyIntentSnapshot& intent) {
    constexpr std::uint64_t section = 0x454E454D59ULL; // ENEMY
    hash.append(section);
    hash.append(intent.tick);
    hash.append(intent.next_event_sequence);
    hash.append(intent.acquisition_count);
    hash.append(intent.attack_count);
    const auto actors = canonical_order(
        intent.actors, [](const EnemyActorIntentSnapshot& actor) { return actor.actor.value; },
        "Gameplay EnemyIntent snapshots require unique non-zero actor identities.");
    hash.append(actors.size());
    for (const EnemyActorIntentSnapshot* actor : actors) {
        if (!actor->target) {
            throw std::invalid_argument{
                "Gameplay EnemyIntent snapshots require non-zero target identities."};
        }
        hash.append(actor->actor.value);
        hash.append(actor->target.value);
        hash.append(static_cast<std::uint64_t>(actor->state));
        append_bool(hash, actor->acquired);
        append_vec2(hash, actor->movement_direction);
        append_vec2(hash, actor->facing_direction);
        hash.append_float(actor->distance_to_target);
        hash.append_float(actor->movement_speed);
        hash.append_float(actor->acquisition_range);
        hash.append_float(actor->attack_range);
        hash.append(actor->attack_cooldown_ticks);
        hash.append(actor->attack_cooldown_ticks_remaining);
        hash.append_float(actor->attack_damage);
        hash.append(actor->attack_count);
    }
}

void append_nav_cell(StableHasher& hash, const NavCell cell) {
    if (cell.column < 0 || cell.row < 0) {
        throw std::invalid_argument{"Gameplay navigation snapshots require non-negative cells."};
    }
    hash.append(static_cast<std::uint64_t>(cell.column));
    hash.append(static_cast<std::uint64_t>(cell.row));
}

void append_optional_nav_cell(StableHasher& hash, const std::optional<NavCell>& cell) {
    append_bool(hash, cell.has_value());
    if (cell) {
        append_nav_cell(hash, *cell);
    }
}

void append_navigation(StableHasher& hash, const NavAgentSnapshot& navigation) {
    constexpr std::uint64_t section = 0x4E41564947415445ULL; // NAVIGATE
    if (navigation.repath_interval_ticks == 0 || !std::isfinite(navigation.waypoint_tolerance) ||
        !(navigation.waypoint_tolerance > 0.0F)) {
        throw std::invalid_argument{
            "Gameplay navigation snapshots require valid path-following settings."};
    }
    hash.append(section);
    hash.append(navigation.tick);
    hash.append(navigation.total_search_count);
    hash.append(navigation.repath_interval_ticks);
    hash.append_float(navigation.waypoint_tolerance);

    const auto actors = canonical_order(
        navigation.actors, [](const NavAgentStateSnapshot& actor) { return actor.actor.value; },
        "Gameplay navigation snapshots require unique non-zero actor identities.");
    hash.append(actors.size());
    for (const NavAgentStateSnapshot* actor : actors) {
        if (!actor->target) {
            throw std::invalid_argument{
                "Gameplay navigation snapshots require non-zero target identities."};
        }
        if ((actor->path_status == NavPathStatus::found && actor->path.empty()) ||
            (actor->path_status != NavPathStatus::found && !actor->path.empty()) ||
            actor->waypoint_index > actor->path.size()) {
            throw std::invalid_argument{
                "Gameplay navigation snapshots contain inconsistent route state."};
        }
        hash.append(actor->actor.value);
        hash.append(actor->target.value);
        append_bool(hash, actor->active);
        hash.append(static_cast<std::uint64_t>(actor->path_status));
        append_optional_nav_cell(hash, actor->current_cell);
        append_optional_nav_cell(hash, actor->goal_cell);
        hash.append(actor->path.size());
        for (const NavCell cell : actor->path) {
            append_nav_cell(hash, cell);
        }
        hash.append(actor->waypoint_index);
        hash.append(actor->next_repath_tick);
        hash.append(actor->search_count);
        hash.append(actor->waypoint_advance_count);
        append_vec2(hash, actor->movement_direction);
        hash.append_float(actor->path_distance);
        hash.append(actor->expanded_cell_count);
    }
}

void append_health(StableHasher& hash, const HealthSnapshot& health) {
    constexpr std::uint64_t section = 0x4845414C5448ULL; // HEALTH
    hash.append(section);
    hash.append(health.tick);
    hash.append(health.applied_hit_count);
    hash.append(health.death_count);
    hash.append(health.rejected_duplicate_hit_count);
    hash.append(health.pending_damage_count);
    hash.append(health.next_event_sequence);

    std::vector<const HitIdentity*> accepted_hits;
    accepted_hits.reserve(health.accepted_hits.size());
    for (const HitIdentity& hit : health.accepted_hits) {
        if (!hit.source || hit.value == 0) {
            throw std::invalid_argument{
                "Gameplay Health snapshots require non-zero accepted hit identities."};
        }
        accepted_hits.push_back(&hit);
    }
    std::ranges::sort(accepted_hits, [](const HitIdentity* left, const HitIdentity* right) {
        return *left < *right;
    });
    for (std::size_t index = 1; index < accepted_hits.size(); ++index) {
        if (*accepted_hits[index - 1] == *accepted_hits[index]) {
            throw std::invalid_argument{
                "Gameplay Health snapshots require unique accepted hit identities."};
        }
    }
    hash.append(accepted_hits.size());
    for (const HitIdentity* hit : accepted_hits) {
        hash.append(hit->source.value);
        hash.append(hit->value);
    }

    const auto targets = canonical_order(
        health.targets, [](const HealthTargetSnapshot& target) { return target.target.value; },
        "Gameplay health snapshots require unique non-zero target identities.");
    hash.append(targets.size());
    for (const HealthTargetSnapshot* target : targets) {
        hash.append(target->target.value);
        hash.append_float(target->maximum_health);
        hash.append_float(target->current_health);
        append_bool(hash, target->alive);
    }
}

} // namespace

GameplayStateDigest gameplay_state_digest(const GameplayStateSnapshot& state) {
    if (state.combat.tick != state.projectiles.tick || state.combat.tick != state.health.tick ||
        state.combat.tick != state.enemy_intent.tick ||
        state.combat.tick != state.navigation.tick) {
        throw std::invalid_argument{
            "Gameplay state snapshots must describe one completed fixed tick."};
    }
    if (state.combat.pending_command_count != 0 || state.projectiles.pending_spawn_count != 0 ||
        state.health.pending_damage_count != 0) {
        throw std::invalid_argument{
            "Gameplay state digests require a completed fixed-tick boundary with no pending work."};
    }

    StableHasher hash;
    hash.append(gameplay_state_digest_schema_version);
    append_world(hash, state.world);
    append_combat(hash, state.combat);
    append_enemy_intent(hash, state.enemy_intent);
    append_navigation(hash, state.navigation);
    append_projectiles(hash, state.projectiles);
    append_health(hash, state.health);
    return {.value = hash.value()};
}

} // namespace ic2d
