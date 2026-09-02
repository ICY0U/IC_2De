#include "ic2d/enemy_intent.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] bool near(const float lhs, const float rhs, const float epsilon = 0.0001F) {
    return std::abs(lhs - rhs) <= epsilon;
}

[[nodiscard]] ic2d::EnemyIntentDefinition definition(
    const std::uint64_t actor,
    const std::uint64_t target
) {
    return {
        .actor = {actor},
        .target = {target},
        .movement_speed = 54.0F,
        .acquisition_range = 180.0F,
        .attack_range = 20.0F,
        .attack_cooldown_ticks = 4,
        .attack_damage = 12.0F,
    };
}

void test_acquires_pursues_attacks_and_respects_cooldown() {
    ic2d::EnemyIntent intent;
    expect(intent.register_actor(definition(200, 100)),
           "A valid authored attacker must register before simulation starts.");

    intent.fixed_update(1, {{
        .actor = {200},
        .target = {100},
        .actor_position = {0.0F, 0.0F},
        .target_position = {100.0F, 0.0F},
    }});
    ic2d::EnemyIntentSnapshot snapshot = intent.snapshot();
    expect(snapshot.tick == 1 && snapshot.acquisition_count == 1 &&
               snapshot.actors.size() == 1 && snapshot.actors.front().acquired &&
               snapshot.actors.front().state == ic2d::EnemyIntentState::pursuing,
           "An in-range target must be acquired and enter pursuit on the same fixed tick.");
    expect(near(snapshot.actors.front().movement_direction.x, 1.0F) &&
               near(snapshot.actors.front().movement_direction.y, 0.0F),
           "Pursuit must expose one normalized world X/Z direction.");
    std::vector<ic2d::EnemyIntentEvent> events = intent.drain_events();
    expect(events.size() == 1 &&
               std::holds_alternative<ic2d::EnemyAcquiredTargetEvent>(events.front()),
           "The acquisition transition must emit one copied event.");

    intent.fixed_update(2, {{
        .actor = {200},
        .target = {100},
        .actor_position = {82.0F, 0.0F},
        .target_position = {100.0F, 0.0F},
    }});
    snapshot = intent.snapshot();
    events = intent.drain_events();
    expect(snapshot.actors.front().state == ic2d::EnemyIntentState::attacking &&
               near(snapshot.actors.front().movement_direction.x, 0.0F),
           "An attacker inside range must stop pursuit and enter the attacking state.");
    expect(events.size() == 1 &&
               std::holds_alternative<ic2d::EnemyAttackRequestedEvent>(events.front()) &&
               std::get<ic2d::EnemyAttackRequestedEvent>(events.front()).sequence == 2 &&
               near(std::get<ic2d::EnemyAttackRequestedEvent>(events.front()).damage, 12.0F),
           "Attack range entry must emit one sequenced copied damage request.");

    for (std::uint64_t tick = 3; tick <= 5; ++tick) {
        intent.fixed_update(tick, {{
            .actor = {200},
            .target = {100},
            .actor_position = {82.0F, 0.0F},
            .target_position = {100.0F, 0.0F},
        }});
        expect(intent.drain_events().empty(),
               "Attack requests must remain silent until the exact cooldown-ready tick.");
    }
    intent.fixed_update(6, {{
        .actor = {200},
        .target = {100},
        .actor_position = {82.0F, 0.0F},
        .target_position = {100.0F, 0.0F},
    }});
    events = intent.drain_events();
    expect(events.size() == 1 &&
               std::holds_alternative<ic2d::EnemyAttackRequestedEvent>(events.front()) &&
               intent.snapshot().attack_count == 2,
           "The next request must occur exactly four fixed ticks after the first attack.");
}

void test_order_is_canonical_and_target_death_stops_intent() {
    ic2d::EnemyIntent first;
    ic2d::EnemyIntent second;
    expect(first.register_actor(definition(300, 100)) &&
               first.register_actor(definition(200, 100)),
           "Registration must accept multiple stable attackers.");
    expect(second.register_actor(definition(200, 100)) &&
               second.register_actor(definition(300, 100)),
           "The comparison fixture must accept the opposite registration order.");
    const std::vector<ic2d::EnemyPerception> perceptions{
        {.actor = {300}, .target = {100}, .actor_position = {20.0F, 0.0F},
         .target_position = {0.0F, 0.0F}},
        {.actor = {200}, .target = {100}, .actor_position = {-20.0F, 0.0F},
         .target_position = {0.0F, 0.0F}},
    };
    std::vector<ic2d::EnemyPerception> reversed = perceptions;
    std::ranges::reverse(reversed);
    first.fixed_update(1, perceptions);
    second.fixed_update(1, reversed);
    expect(first.snapshot().actors == second.snapshot().actors &&
               first.snapshot().actors.front().actor == ic2d::EntityUuid{200},
           "Snapshots must use canonical actor identity order regardless of input order.");

    first.fixed_update(2, {
        {.actor = {300}, .target = {100}, .actor_position = {20.0F, 0.0F},
         .target_position = {0.0F, 0.0F}, .target_alive = false},
        {.actor = {200}, .target = {100}, .actor_position = {-20.0F, 0.0F},
         .target_position = {0.0F, 0.0F}, .target_alive = false},
    });
    const ic2d::EnemyIntentSnapshot inactive = first.snapshot();
    expect(std::ranges::all_of(inactive.actors, [](const auto& actor) {
               return actor.state == ic2d::EnemyIntentState::inactive &&
                      near(actor.movement_direction.x, 0.0F) &&
                      near(actor.movement_direction.y, 0.0F);
           }),
           "A dead target must stop every registered attacker without emitting new intent.");
}

void test_rejects_invalid_perception_and_reset_restores_definitions() {
    ic2d::EnemyIntent intent;
    ic2d::EnemyIntentDefinition invalid = definition(200, 100);
    invalid.attack_range = invalid.acquisition_range + 1.0F;
    expect(!intent.register_actor(invalid),
           "Attack range beyond acquisition range must be rejected at registration.");
    expect(intent.register_actor(definition(200, 100)),
           "The valid reset fixture must register.");

    bool rejected_missing = false;
    try {
        intent.fixed_update(1, {});
    } catch (const std::invalid_argument&) {
        rejected_missing = true;
    }
    expect(rejected_missing,
           "A tick missing perception for a registered attacker must fail atomically.");
    expect(intent.snapshot().tick == 0,
           "Rejected perception must not advance authoritative tick state.");

    intent.fixed_update(1, {{
        .actor = {200}, .target = {100}, .actor_position = {0.0F, 0.0F},
        .target_position = {10.0F, 0.0F},
    }});
    static_cast<void>(intent.drain_events());
    intent.reset();
    const ic2d::EnemyIntentSnapshot reset = intent.snapshot();
    expect(reset.tick == 0 && reset.next_event_sequence == 1 &&
               reset.acquisition_count == 0 && reset.attack_count == 0 &&
               reset.actors.size() == 1 && !reset.actors.front().acquired &&
               reset.actors.front().state == ic2d::EnemyIntentState::unaware &&
               near(reset.actors.front().movement_speed, 54.0F),
           "Reset must retain definitions while clearing all transient intent state.");
}

} // namespace

int main() {
    test_acquires_pursues_attacks_and_respects_cooldown();
    test_order_is_canonical_and_target_death_stops_intent();
    test_rejects_invalid_perception_and_reset_restores_definitions();
    if (failures == 0) {
        std::cout << "Enemy-intent tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
