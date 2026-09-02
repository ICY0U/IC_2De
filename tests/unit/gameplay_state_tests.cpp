#include "ic2d/gameplay_state.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] ic2d::GameplayStateSnapshot representative_state() {
    ic2d::GameplayStateSnapshot state;
    state.world.entities = {
        {
            .uuid = {20},
            .name = "Second",
            .transform = {.position = {12.0F, 0.0F, -4.0F}},
        },
        {
            .uuid = {10},
            .name = "First",
            .transform = {.position = {-8.0F, 2.0F, 5.0F}},
        },
    };
    state.combat = {
        .tick = 12,
        .consumed_command_count = 4,
        .emitted_intent_count = 3,
        .latest_actor = {10},
        .aim_direction = {0.6F, 0.8F},
        .weapon = {
            .magazine_ammo = 9,
            .reserve_ammo = 42,
            .fire_cooldown_ticks_remaining = 5,
        },
        .dodge = {
            .direction = {-1.0F, 0.0F},
            .active_ticks_remaining = 7,
            .invulnerable_ticks_remaining = 4,
            .cooldown_ticks_remaining = 31,
            .active = true,
            .invulnerable = true,
            .started_count = 1,
        },
        .spawned_projectile_count = 3,
        .next_event_sequence = 8,
        .next_projectile_id = 4,
        .actors = {
            {
                .actor = {20},
                .aim_direction = {-1.0F, 0.0F},
                .weapon = {.magazine_ammo = 11, .reserve_ammo = 36},
                .fire_held = true,
            },
            {
                .actor = {10},
                .aim_direction = {0.6F, 0.8F},
                .weapon = {
                    .magazine_ammo = 9,
                    .reserve_ammo = 42,
                    .fire_cooldown_ticks_remaining = 5,
                },
                .dodge = {
                    .direction = {-1.0F, 0.0F},
                    .active_ticks_remaining = 7,
                    .invulnerable_ticks_remaining = 4,
                    .cooldown_ticks_remaining = 31,
                    .active = true,
                    .invulnerable = true,
                    .started_count = 1,
                },
            },
        },
    };
    state.projectiles = {
        .tick = 12,
        .total_spawned = 3,
        .total_expired = 1,
        .active = {
            {
                .projectile_id = 9,
                .actor = {10},
                .position = {20.0F, 14.0F, 5.0F},
                .direction = {1.0F, 0.0F},
                .speed = 520.0F,
                .lifetime_ticks_remaining = 60,
                .damage = 18.0F,
            },
            {
                .projectile_id = 3,
                .actor = {10},
                .position = {8.0F, 14.0F, 7.0F},
                .direction = {0.0F, 1.0F},
                .speed = 520.0F,
                .lifetime_ticks_remaining = 48,
                .damage = 18.0F,
            },
        },
    };
    state.enemy_intent = {
        .tick = 12,
        .next_event_sequence = 5,
        .acquisition_count = 2,
        .attack_count = 2,
        .actors = {
            {
                .actor = {60},
                .target = {10},
                .state = ic2d::EnemyIntentState::attacking,
                .acquired = true,
                .facing_direction = {-1.0F, 0.0F},
                .distance_to_target = 18.0F,
                .movement_speed = 54.0F,
                .acquisition_range = 180.0F,
                .attack_range = 20.0F,
                .attack_cooldown_ticks = 45,
                .attack_cooldown_ticks_remaining = 31,
                .attack_damage = 12.0F,
                .attack_count = 2,
            },
            {
                .actor = {50},
                .target = {10},
                .state = ic2d::EnemyIntentState::pursuing,
                .acquired = true,
                .movement_direction = {0.0F, -1.0F},
                .facing_direction = {0.0F, -1.0F},
                .distance_to_target = 42.0F,
                .movement_speed = 54.0F,
                .acquisition_range = 180.0F,
                .attack_range = 20.0F,
                .attack_cooldown_ticks = 45,
                .attack_damage = 12.0F,
            },
        },
    };
    state.navigation = {
        .tick = 12,
        .total_search_count = 4,
        .repath_interval_ticks = 30,
        .waypoint_tolerance = 4.0F,
        .actors = {
            {
                .actor = {60},
                .target = {10},
                .active = false,
                .path_status = ic2d::NavPathStatus::unreachable,
                .current_cell = ic2d::NavCell{4, 3},
                .goal_cell = ic2d::NavCell{3, 3},
                .next_repath_tick = 12,
                .search_count = 1,
            },
            {
                .actor = {50},
                .target = {10},
                .active = true,
                .path_status = ic2d::NavPathStatus::found,
                .current_cell = ic2d::NavCell{1, 2},
                .goal_cell = ic2d::NavCell{3, 2},
                .path = {{1, 2}, {2, 1}, {3, 2}},
                .waypoint_index = 1,
                .next_repath_tick = 31,
                .search_count = 3,
                .waypoint_advance_count = 2,
                .movement_direction = {0.70710678F, -0.70710678F},
                .path_distance = 56.568542F,
                .expanded_cell_count = 3,
            },
        },
    };
    state.health = {
        .tick = 12,
        .applied_hit_count = 2,
        .next_event_sequence = 3,
        .accepted_hits = {
            {.source = {20}, .value = 9},
            {.source = {10}, .value = 3},
        },
        .targets = {
            {.target = {40}, .maximum_health = 80.0F, .current_health = 44.0F, .alive = true},
            {.target = {30}, .maximum_health = 54.0F, .current_health = 18.0F, .alive = true},
        },
    };
    return state;
}

void test_digest_is_canonical_across_snapshot_storage_order() {
    const ic2d::GameplayStateSnapshot original = representative_state();
    ic2d::GameplayStateSnapshot reordered = original;
    std::ranges::reverse(reordered.world.entities);
    std::ranges::reverse(reordered.combat.actors);
    std::ranges::reverse(reordered.enemy_intent.actors);
    std::ranges::reverse(reordered.navigation.actors);
    std::ranges::reverse(reordered.projectiles.active);
    std::ranges::reverse(reordered.health.accepted_hits);
    std::ranges::reverse(reordered.health.targets);

    const ic2d::GameplayStateDigest first = ic2d::gameplay_state_digest(original);
    const ic2d::GameplayStateDigest second = ic2d::gameplay_state_digest(reordered);
    expect(first.schema_version == 3,
           "The digest must expose an explicit schema version.");
    expect(first.value != 0 && first.value == second.value,
           "Equivalent authoritative state must produce one digest regardless of vector order.");
}

void test_digest_includes_health_hit_namespace_and_future_identity() {
    const ic2d::GameplayStateSnapshot baseline = representative_state();
    const ic2d::GameplayStateDigest baseline_digest =
        ic2d::gameplay_state_digest(baseline);

    ic2d::GameplayStateSnapshot changed_hit = baseline;
    ++changed_hit.health.accepted_hits.front().value;
    expect(ic2d::gameplay_state_digest(changed_hit) != baseline_digest,
           "Changing accepted hit identity must change future-state digest even when health totals match.");

    ic2d::GameplayStateSnapshot changed_identity = baseline;
    ++changed_identity.health.next_event_sequence;
    expect(ic2d::gameplay_state_digest(changed_identity) != baseline_digest,
           "Changing the next Health event identity must change the digest.");
}

void test_digest_rejects_an_incomplete_fixed_tick_boundary() {
    ic2d::GameplayStateSnapshot pending = representative_state();
    pending.combat.pending_command_count = 1;

    bool rejected = false;
    try {
        static_cast<void>(ic2d::gameplay_state_digest(pending));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected,
           "The digest must reject a snapshot with private pending command state.");

    ic2d::GameplayStateSnapshot mismatched_tick = representative_state();
    ++mismatched_tick.health.tick;
    rejected = false;
    try {
        static_cast<void>(ic2d::gameplay_state_digest(mismatched_tick));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected,
           "The digest must reject subsystem snapshots from different fixed ticks.");

    mismatched_tick = representative_state();
    ++mismatched_tick.enemy_intent.tick;
    rejected = false;
    try {
        static_cast<void>(ic2d::gameplay_state_digest(mismatched_tick));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected,
           "The digest must reject EnemyIntent state from a different fixed tick.");

    mismatched_tick = representative_state();
    ++mismatched_tick.navigation.tick;
    rejected = false;
    try {
        static_cast<void>(ic2d::gameplay_state_digest(mismatched_tick));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected,
           "The digest must reject path-following state from a different fixed tick.");
}

void test_digest_includes_enemy_intent_and_future_attack_identity() {
    const ic2d::GameplayStateSnapshot baseline = representative_state();
    const ic2d::GameplayStateDigest baseline_digest =
        ic2d::gameplay_state_digest(baseline);

    ic2d::GameplayStateSnapshot changed_cooldown = baseline;
    ++changed_cooldown.enemy_intent.actors.front().attack_cooldown_ticks_remaining;
    expect(ic2d::gameplay_state_digest(changed_cooldown) != baseline_digest,
           "Changing an attacker's future cooldown must change the digest.");

    ic2d::GameplayStateSnapshot changed_identity = baseline;
    ++changed_identity.enemy_intent.next_event_sequence;
    expect(ic2d::gameplay_state_digest(changed_identity) != baseline_digest,
           "Changing the next enemy-intent event identity must change the digest.");
}

void test_digest_includes_navigation_route_and_repath_state() {
    const ic2d::GameplayStateSnapshot baseline = representative_state();
    const ic2d::GameplayStateDigest baseline_digest =
        ic2d::gameplay_state_digest(baseline);

    ic2d::GameplayStateSnapshot changed_waypoint = baseline;
    ++changed_waypoint.navigation.actors.back().waypoint_index;
    expect(ic2d::gameplay_state_digest(changed_waypoint) != baseline_digest,
           "Changing the next navigation waypoint must change the future-state digest.");

    ic2d::GameplayStateSnapshot changed_repath = baseline;
    ++changed_repath.navigation.actors.back().next_repath_tick;
    expect(ic2d::gameplay_state_digest(changed_repath) != baseline_digest,
           "Changing the bounded repath deadline must change the digest.");
}

void test_digest_separates_authoritative_state_from_presentation() {
    const ic2d::GameplayStateSnapshot baseline = representative_state();
    const ic2d::GameplayStateDigest baseline_digest =
        ic2d::gameplay_state_digest(baseline);

    ic2d::GameplayStateSnapshot presentation_only = baseline;
    presentation_only.world.entities.front().name = "Renamed in editor";
    presentation_only.world.entities.front().sprite = ic2d::Sprite2D{
        .texture = {.index = 77, .generation = 5},
        .size = {128.0F, 96.0F},
    };
    expect(ic2d::gameplay_state_digest(presentation_only) == baseline_digest,
           "Names, sprites, and transient texture handles must not affect authoritative digest state.");

    ic2d::GameplayStateSnapshot moved = baseline;
    moved.world.entities.front().transform.position.x += 1.0F;
    expect(ic2d::gameplay_state_digest(moved) != baseline_digest,
           "Changing an authoritative world transform must change the digest.");

    ic2d::GameplayStateSnapshot projectile_moved = baseline;
    projectile_moved.projectiles.active.front().position.z += 1.0F;
    expect(ic2d::gameplay_state_digest(projectile_moved) != baseline_digest,
           "Changing active projectile state must change the digest.");

    ic2d::GameplayStateSnapshot damaged = baseline;
    damaged.health.targets.front().current_health -= 1.0F;
    expect(ic2d::gameplay_state_digest(damaged) != baseline_digest,
           "Changing target health must change the digest.");

    ic2d::GameplayStateSnapshot redirected_dodge = baseline;
    redirected_dodge.combat.actors.back().dodge.direction = {0.0F, 1.0F};
    expect(ic2d::gameplay_state_digest(redirected_dodge) != baseline_digest,
           "Changing an actor's frozen dodge direction must change the digest.");
}

void test_digest_includes_every_combat_actor_and_future_identity() {
    const ic2d::GameplayStateSnapshot baseline = representative_state();
    const ic2d::GameplayStateDigest baseline_digest =
        ic2d::gameplay_state_digest(baseline);

    ic2d::GameplayStateSnapshot changed_actor = baseline;
    changed_actor.combat.actors.front().fire_held = false;
    expect(ic2d::gameplay_state_digest(changed_actor) != baseline_digest,
           "Changing a non-latest actor's held-fire state must change the digest.");

    ic2d::GameplayStateSnapshot changed_identity = baseline;
    ++changed_identity.combat.next_event_sequence;
    expect(ic2d::gameplay_state_digest(changed_identity) != baseline_digest,
           "Changing the next deterministic event identity must change the digest.");
}

} // namespace

int main() {
    test_digest_is_canonical_across_snapshot_storage_order();
    test_digest_includes_every_combat_actor_and_future_identity();
    test_digest_includes_enemy_intent_and_future_attack_identity();
    test_digest_includes_navigation_route_and_repath_state();
    test_digest_includes_health_hit_namespace_and_future_identity();
    test_digest_rejects_an_incomplete_fixed_tick_boundary();
    test_digest_separates_authoritative_state_from_presentation();
    if (failures == 0) {
        std::cout << "Gameplay-state digest tests passed.\n";
        return 0;
    }
    std::cerr << failures << " gameplay-state digest assertion(s) failed.\n";
    return 1;
}
