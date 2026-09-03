#include "ic2d/health.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] bool near(const float left, const float right) {
    return std::abs(left - right) < 0.0001F;
}

void test_three_needle_hits_kill_one_registered_target_once() {
    ic2d::Health health;
    const ic2d::EntityUuid player{1002};
    const ic2d::EntityUuid target{1014};
    expect(health.register_target({.target = target, .maximum_health = 54.0F}),
           "The target dummy must accept its stable identity and maximum health.");

    for (std::uint64_t tick = 1; tick <= 3; ++tick) {
        expect(health.submit({
                   .tick = tick,
                   .hit = {.source = player, .value = tick},
                   .target = target,
                   .damage = 18.0F,
               }),
               "Each unique projectile hit must enter the matching fixed-tick buffer.");
        health.fixed_update(tick);

        const ic2d::HealthSnapshot snapshot = health.snapshot();
        expect(snapshot.targets.size() == 1,
               "The registered target must remain visible in copied health state.");
        if (snapshot.targets.size() == 1) {
            const float expected_health = 54.0F - static_cast<float>(tick) * 18.0F;
            expect(near(snapshot.targets.front().current_health, expected_health),
                   "Every needle hit must remove exactly 18 health.");
            expect(snapshot.targets.front().alive == (tick < 3),
                   "The target must die only when health reaches zero.");
        }

        const std::vector<ic2d::HealthEvent> events = health.drain_events();
        const std::size_t expected_event_count = tick == 3 ? 2 : 1;
        expect(events.size() == expected_event_count,
               "A hit emits damage, and only the lethal hit also emits one death.");
    }

    const ic2d::HealthSnapshot final = health.snapshot();
    expect(final.applied_hit_count == 3 && final.death_count == 1,
           "Three unique hits must produce one terminal death result.");
}

void test_duplicate_hit_identity_cannot_apply_damage_twice() {
    ic2d::Health health;
    const ic2d::EntityUuid player{1002};
    const ic2d::EntityUuid target{1014};
    expect(health.register_target({.target = target, .maximum_health = 54.0F}),
           "The duplicate-hit fixture must register its target.");
    const ic2d::DamageCommand impact{
        .tick = 1,
        .hit = {.source = player, .value = 77},
        .target = target,
        .damage = 18.0F,
    };

    expect(health.submit(impact), "The first observation of a hit identity must be accepted.");
    expect(!health.submit(impact), "The same hit identity must be rejected before fixed update.");
    health.fixed_update(1);

    const ic2d::HealthSnapshot snapshot = health.snapshot();
    expect(snapshot.targets.size() == 1 && near(snapshot.targets.front().current_health, 36.0F),
           "A duplicated projectile impact must remove health exactly once.");
    expect(snapshot.applied_hit_count == 1 && snapshot.rejected_duplicate_hit_count == 1 &&
               health.drain_events().size() == 1,
           "Duplicate rejection must be observable without emitting another damage event.");
    expect(snapshot.accepted_hits.size() == 1 && snapshot.accepted_hits.front() == impact.hit,
           "A copied Health snapshot must retain the accepted hit namespace that affects future "
           "rejection.");
    expect(snapshot.next_event_sequence == 2,
           "A copied Health snapshot must retain the next deterministic event identity.");
}

void test_reset_restores_registered_target_and_hit_identity_namespace() {
    ic2d::Health health;
    const ic2d::EntityUuid player{1002};
    const ic2d::EntityUuid target{1014};
    const ic2d::DamageCommand lethal{
        .tick = 1,
        .hit = {.source = player, .value = 9},
        .target = target,
        .damage = 99.0F,
    };
    expect(health.register_target({.target = target, .maximum_health = 10.0F}),
           "The reset fixture must register its target.");
    expect(health.submit(lethal), "The first lethal hit must be accepted.");
    health.fixed_update(1);
    const std::vector<ic2d::HealthEvent> lethal_events = health.drain_events();
    expect(lethal_events.size() == 2,
           "Overkill must clamp to zero and still emit one damage plus one death.");
    if (!lethal_events.empty()) {
        const auto* damage = std::get_if<ic2d::DamageAppliedEvent>(&lethal_events.front());
        expect(damage != nullptr && near(damage->applied_damage, 10.0F) &&
                   near(damage->health_after, 0.0F),
               "Applied overkill damage must clamp to the target's remaining health.");
    }

    health.reset();
    const ic2d::HealthSnapshot reset = health.snapshot();
    expect(reset.tick == 0 && reset.applied_hit_count == 0 && reset.death_count == 0 &&
               reset.targets.size() == 1 && reset.targets.front().alive &&
               reset.accepted_hits.empty() && reset.next_event_sequence == 1 &&
               near(reset.targets.front().current_health, 10.0F),
           "Reset must preserve definitions while restoring all transient health state.");
    expect(health.submit(lethal),
           "A new run must be allowed to reuse deterministic projectile identities.");
}

} // namespace

int main() {
    test_three_needle_hits_kill_one_registered_target_once();
    test_duplicate_hit_identity_cannot_apply_damage_twice();
    test_reset_restores_registered_target_and_hit_identity_namespace();
    if (failures == 0) {
        std::cout << "Health and damage tests passed.\n";
        return 0;
    }
    std::cerr << failures << " health assertion(s) failed.\n";
    return 1;
}
