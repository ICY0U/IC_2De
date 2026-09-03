#include <doctest/doctest.h>

#include "ic2d/health.hpp"

#include <cmath>
#include <string_view>

namespace {

[[nodiscard]] bool near(const float left, const float right) {
    return std::abs(left - right) < 0.0001F;
}

TEST_CASE("three needle hits kill one registered target once") {
    ic2d::Health health;
    const ic2d::EntityUuid player{1002};
    const ic2d::EntityUuid target{1014};
    CHECK_MESSAGE((health.register_target({.target = target, .maximum_health = 54.0F})),
                  "The target dummy must accept its stable identity and maximum health.");

    for (std::uint64_t tick = 1; tick <= 3; ++tick) {
        CHECK_MESSAGE((health.submit({
                          .tick = tick,
                          .hit = {.source = player, .value = tick},
                          .target = target,
                          .damage = 18.0F,
                      })),
                      "Each unique projectile hit must enter the matching fixed-tick buffer.");
        health.fixed_update(tick);

        const ic2d::HealthSnapshot snapshot = health.snapshot();
        CHECK_MESSAGE((snapshot.targets.size() == 1),
                      "The registered target must remain visible in copied health state.");
        if (snapshot.targets.size() == 1) {
            const float expected_health = 54.0F - static_cast<float>(tick) * 18.0F;
            CHECK_MESSAGE((near(snapshot.targets.front().current_health, expected_health)),
                          "Every needle hit must remove exactly 18 health.");
            CHECK_MESSAGE((snapshot.targets.front().alive == (tick < 3)),
                          "The target must die only when health reaches zero.");
        }

        const std::vector<ic2d::HealthEvent> events = health.drain_events();
        const std::size_t expected_event_count = tick == 3 ? 2 : 1;
        CHECK_MESSAGE((events.size() == expected_event_count),
                      "A hit emits damage, and only the lethal hit also emits one death.");
    }

    const ic2d::HealthSnapshot final = health.snapshot();
    CHECK_MESSAGE((final.applied_hit_count == 3 && final.death_count == 1),
                  "Three unique hits must produce one terminal death result.");
}

TEST_CASE("duplicate hit identity cannot apply damage twice") {
    ic2d::Health health;
    const ic2d::EntityUuid player{1002};
    const ic2d::EntityUuid target{1014};
    CHECK_MESSAGE((health.register_target({.target = target, .maximum_health = 54.0F})),
                  "The duplicate-hit fixture must register its target.");
    const ic2d::DamageCommand impact{
        .tick = 1,
        .hit = {.source = player, .value = 77},
        .target = target,
        .damage = 18.0F,
    };

    CHECK_MESSAGE((health.submit(impact)),
                  "The first observation of a hit identity must be accepted.");
    CHECK_MESSAGE((!health.submit(impact)),
                  "The same hit identity must be rejected before fixed update.");
    health.fixed_update(1);

    const ic2d::HealthSnapshot snapshot = health.snapshot();
    CHECK_MESSAGE(
        (snapshot.targets.size() == 1 && near(snapshot.targets.front().current_health, 36.0F)),
        "A duplicated projectile impact must remove health exactly once.");
    CHECK_MESSAGE((snapshot.applied_hit_count == 1 && snapshot.rejected_duplicate_hit_count == 1 &&
                   health.drain_events().size() == 1),
                  "Duplicate rejection must be observable without emitting another damage event.");
    CHECK_MESSAGE(
        (snapshot.accepted_hits.size() == 1 && snapshot.accepted_hits.front() == impact.hit),
        "A copied Health snapshot must retain the accepted hit namespace that affects future "
        "rejection.");
    CHECK_MESSAGE((snapshot.next_event_sequence == 2),
                  "A copied Health snapshot must retain the next deterministic event identity.");
}

TEST_CASE("reset restores registered target and hit identity namespace") {
    ic2d::Health health;
    const ic2d::EntityUuid player{1002};
    const ic2d::EntityUuid target{1014};
    const ic2d::DamageCommand lethal{
        .tick = 1,
        .hit = {.source = player, .value = 9},
        .target = target,
        .damage = 99.0F,
    };
    CHECK_MESSAGE((health.register_target({.target = target, .maximum_health = 10.0F})),
                  "The reset fixture must register its target.");
    CHECK_MESSAGE((health.submit(lethal)), "The first lethal hit must be accepted.");
    health.fixed_update(1);
    const std::vector<ic2d::HealthEvent> lethal_events = health.drain_events();
    CHECK_MESSAGE((lethal_events.size() == 2),
                  "Overkill must clamp to zero and still emit one damage plus one death.");
    if (!lethal_events.empty()) {
        const auto* damage = std::get_if<ic2d::DamageAppliedEvent>(&lethal_events.front());
        CHECK_MESSAGE((damage != nullptr && near(damage->applied_damage, 10.0F) &&
                       near(damage->health_after, 0.0F)),
                      "Applied overkill damage must clamp to the target's remaining health.");
    }

    health.reset();
    const ic2d::HealthSnapshot reset = health.snapshot();
    CHECK_MESSAGE((reset.tick == 0 && reset.applied_hit_count == 0 && reset.death_count == 0 &&
                   reset.targets.size() == 1 && reset.targets.front().alive &&
                   reset.accepted_hits.empty() && reset.next_event_sequence == 1 &&
                   near(reset.targets.front().current_health, 10.0F)),
                  "Reset must preserve definitions while restoring all transient health state.");
    CHECK_MESSAGE((health.submit(lethal)),
                  "A new run must be allowed to reuse deterministic projectile identities.");
}

} // namespace
