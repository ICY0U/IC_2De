#include <doctest/doctest.h>

#include "ic2d/actor_debug.hpp"

#include <string_view>

namespace {

constexpr ic2d::EntityUuid runner{4001};
constexpr ic2d::EntityUuid player{1002};

TEST_CASE("overrides are held per actor and flag") {
    ic2d::ActorDebugOverrides overrides;
    CHECK_MESSAGE((!overrides.enabled(runner, ic2d::ActorDebugFlag::frozen)),
                  "An actor nobody has touched must hold no override.");
    CHECK_MESSAGE((overrides.set(runner, ic2d::ActorDebugFlag::frozen, true)),
                  "Freezing a valid actor must be accepted.");
    CHECK_MESSAGE((overrides.enabled(runner, ic2d::ActorDebugFlag::frozen)),
                  "A held override must read back.");
    CHECK_MESSAGE((!overrides.enabled(runner, ic2d::ActorDebugFlag::invulnerable)),
                  "One flag must not imply another.");
    CHECK_MESSAGE((!overrides.enabled(player, ic2d::ActorDebugFlag::frozen)),
                  "An override must not leak to another actor.");

    // Two flags on one actor, then one released: the actor keeps the other.
    CHECK_MESSAGE((overrides.set(runner, ic2d::ActorDebugFlag::invulnerable, true)),
                  "A second flag on the same actor must be accepted.");
    CHECK_MESSAGE((overrides.set(runner, ic2d::ActorDebugFlag::frozen, false)),
                  "Releasing a flag must be accepted.");
    CHECK_MESSAGE((!overrides.enabled(runner, ic2d::ActorDebugFlag::frozen) &&
                   overrides.enabled(runner, ic2d::ActorDebugFlag::invulnerable)),
                  "Releasing one flag must leave the others held.");
    CHECK_MESSAGE((overrides.any(runner)),
                  "An actor still holding a flag must count as overridden.");
}

TEST_CASE("an actor back to authored behaviour is not an override") {
    ic2d::ActorDebugOverrides overrides;
    CHECK_MESSAGE((overrides.set(runner, ic2d::ActorDebugFlag::frozen, true)), "Setup must hold.");
    CHECK_MESSAGE((overrides.set(runner, ic2d::ActorDebugFlag::frozen, false)), "Setup must hold.");
    CHECK_MESSAGE((!overrides.any(runner)),
                  "An actor with every flag released must stop being an overridden actor.");
    CHECK_MESSAGE((overrides.snapshot().overridden_actor_count == 0),
                  "A released actor must leave no entry behind.");
    // Releasing something nobody holds is a success that stores nothing, or a
    // panel drawing an unchecked box would create an entry per actor it draws.
    CHECK_MESSAGE((overrides.set(player, ic2d::ActorDebugFlag::infinite_ammo, false)),
                  "Releasing an unheld flag must be accepted.");
    CHECK_MESSAGE((overrides.snapshot().overridden_actor_count == 0),
                  "Releasing an unheld flag must store nothing.");
}

TEST_CASE("snapshot is canonical and answers for absent actors") {
    ic2d::ActorDebugOverrides overrides;
    // Set in descending identity order; the snapshot must not reflect that.
    CHECK_MESSAGE((overrides.set(runner, ic2d::ActorDebugFlag::frozen, true)), "Setup must hold.");
    CHECK_MESSAGE((overrides.set(player, ic2d::ActorDebugFlag::infinite_ammo, true)),
                  "Setup must hold.");
    const ic2d::ActorDebugSnapshot snapshot = overrides.snapshot();
    CHECK_MESSAGE((snapshot.actors.size() == 2 && snapshot.overridden_actor_count == 2),
                  "Every overridden actor must appear exactly once.");
    CHECK_MESSAGE((snapshot.actors[0].actor == player && snapshot.actors[1].actor == runner),
                  "Snapshot order must be canonical UUID order, not insertion order.");
    CHECK_MESSAGE((snapshot.enabled(runner, ic2d::ActorDebugFlag::frozen)),
                  "A snapshot must answer for a held flag.");
    CHECK_MESSAGE((!snapshot.enabled(ic2d::EntityUuid{9999}, ic2d::ActorDebugFlag::frozen)),
                  "A snapshot must answer no for an actor it never saw.");
}

TEST_CASE("invalid requests change nothing") {
    ic2d::ActorDebugOverrides overrides;
    CHECK_MESSAGE((!overrides.set({}, ic2d::ActorDebugFlag::frozen, true)),
                  "A zero identity must be refused.");
    CHECK_MESSAGE((!overrides.set(runner, ic2d::ActorDebugFlag::count, true)),
                  "The count sentinel must be refused as a flag.");
    CHECK_MESSAGE((!overrides.enabled({}, ic2d::ActorDebugFlag::frozen)),
                  "A zero identity must hold nothing.");
    CHECK_MESSAGE((overrides.snapshot().overridden_actor_count == 0),
                  "A refused request must store nothing.");
}

TEST_CASE("clearing releases one actor or every actor") {
    ic2d::ActorDebugOverrides overrides;
    CHECK_MESSAGE((overrides.set(runner, ic2d::ActorDebugFlag::frozen, true)), "Setup must hold.");
    CHECK_MESSAGE((overrides.set(player, ic2d::ActorDebugFlag::invulnerable, true)),
                  "Setup must hold.");
    CHECK_MESSAGE((overrides.clear(runner)), "Clearing a held actor must report that it did.");
    CHECK_MESSAGE((!overrides.clear(runner)), "Clearing an actor twice must report nothing to do.");
    CHECK_MESSAGE((overrides.enabled(player, ic2d::ActorDebugFlag::invulnerable)),
                  "Clearing one actor must leave the others held.");
    overrides.clear_all();
    CHECK_MESSAGE((overrides.snapshot().overridden_actor_count == 0),
                  "Clearing everything must leave no overridden actor.");
}

} // namespace
