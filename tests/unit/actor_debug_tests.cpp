#include "ic2d/actor_debug.hpp"

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

constexpr ic2d::EntityUuid runner{4001};
constexpr ic2d::EntityUuid player{1002};

void test_overrides_are_held_per_actor_and_flag() {
    ic2d::ActorDebugOverrides overrides;
    expect(!overrides.enabled(runner, ic2d::ActorDebugFlag::frozen),
           "An actor nobody has touched must hold no override.");
    expect(overrides.set(runner, ic2d::ActorDebugFlag::frozen, true),
           "Freezing a valid actor must be accepted.");
    expect(overrides.enabled(runner, ic2d::ActorDebugFlag::frozen),
           "A held override must read back.");
    expect(!overrides.enabled(runner, ic2d::ActorDebugFlag::invulnerable),
           "One flag must not imply another.");
    expect(!overrides.enabled(player, ic2d::ActorDebugFlag::frozen),
           "An override must not leak to another actor.");

    // Two flags on one actor, then one released: the actor keeps the other.
    expect(overrides.set(runner, ic2d::ActorDebugFlag::invulnerable, true),
           "A second flag on the same actor must be accepted.");
    expect(overrides.set(runner, ic2d::ActorDebugFlag::frozen, false),
           "Releasing a flag must be accepted.");
    expect(!overrides.enabled(runner, ic2d::ActorDebugFlag::frozen) &&
               overrides.enabled(runner, ic2d::ActorDebugFlag::invulnerable),
           "Releasing one flag must leave the others held.");
    expect(overrides.any(runner), "An actor still holding a flag must count as overridden.");
}

void test_an_actor_back_to_authored_behaviour_is_not_an_override() {
    ic2d::ActorDebugOverrides overrides;
    expect(overrides.set(runner, ic2d::ActorDebugFlag::frozen, true), "Setup must hold.");
    expect(overrides.set(runner, ic2d::ActorDebugFlag::frozen, false), "Setup must hold.");
    expect(!overrides.any(runner),
           "An actor with every flag released must stop being an overridden actor.");
    expect(overrides.snapshot().overridden_actor_count == 0,
           "A released actor must leave no entry behind.");
    // Releasing something nobody holds is a success that stores nothing, or a
    // panel drawing an unchecked box would create an entry per actor it draws.
    expect(overrides.set(player, ic2d::ActorDebugFlag::infinite_ammo, false),
           "Releasing an unheld flag must be accepted.");
    expect(overrides.snapshot().overridden_actor_count == 0,
           "Releasing an unheld flag must store nothing.");
}

void test_snapshot_is_canonical_and_answers_for_absent_actors() {
    ic2d::ActorDebugOverrides overrides;
    // Set in descending identity order; the snapshot must not reflect that.
    expect(overrides.set(runner, ic2d::ActorDebugFlag::frozen, true), "Setup must hold.");
    expect(overrides.set(player, ic2d::ActorDebugFlag::infinite_ammo, true), "Setup must hold.");
    const ic2d::ActorDebugSnapshot snapshot = overrides.snapshot();
    expect(snapshot.actors.size() == 2 && snapshot.overridden_actor_count == 2,
           "Every overridden actor must appear exactly once.");
    expect(snapshot.actors[0].actor == player && snapshot.actors[1].actor == runner,
           "Snapshot order must be canonical UUID order, not insertion order.");
    expect(snapshot.enabled(runner, ic2d::ActorDebugFlag::frozen),
           "A snapshot must answer for a held flag.");
    expect(!snapshot.enabled(ic2d::EntityUuid{9999}, ic2d::ActorDebugFlag::frozen),
           "A snapshot must answer no for an actor it never saw.");
}

void test_invalid_requests_change_nothing() {
    ic2d::ActorDebugOverrides overrides;
    expect(!overrides.set({}, ic2d::ActorDebugFlag::frozen, true),
           "A zero identity must be refused.");
    expect(!overrides.set(runner, ic2d::ActorDebugFlag::count, true),
           "The count sentinel must be refused as a flag.");
    expect(!overrides.enabled({}, ic2d::ActorDebugFlag::frozen),
           "A zero identity must hold nothing.");
    expect(overrides.snapshot().overridden_actor_count == 0,
           "A refused request must store nothing.");
}

void test_clearing_releases_one_actor_or_every_actor() {
    ic2d::ActorDebugOverrides overrides;
    expect(overrides.set(runner, ic2d::ActorDebugFlag::frozen, true), "Setup must hold.");
    expect(overrides.set(player, ic2d::ActorDebugFlag::invulnerable, true), "Setup must hold.");
    expect(overrides.clear(runner), "Clearing a held actor must report that it did.");
    expect(!overrides.clear(runner), "Clearing an actor twice must report nothing to do.");
    expect(overrides.enabled(player, ic2d::ActorDebugFlag::invulnerable),
           "Clearing one actor must leave the others held.");
    overrides.clear_all();
    expect(overrides.snapshot().overridden_actor_count == 0,
           "Clearing everything must leave no overridden actor.");
}

} // namespace

int main() {
    test_overrides_are_held_per_actor_and_flag();
    test_an_actor_back_to_authored_behaviour_is_not_an_override();
    test_snapshot_is_canonical_and_answers_for_absent_actors();
    test_invalid_requests_change_nothing();
    test_clearing_releases_one_actor_or_every_actor();

    if (failures == 0) {
        std::cout << "actor debug override tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
