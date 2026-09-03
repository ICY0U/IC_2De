#include <doctest/doctest.h>

#include "ic2d/combat.hpp"
#include "ic2d/interaction.hpp"

#include <array>
#include <cmath>
#include <string_view>

namespace {

constexpr ic2d::EntityUuid player{1};

[[nodiscard]] std::array<ic2d::Interactable, 3> fixture() {
    return {
        ic2d::Interactable{.entity = {10},
                           .position = {0.0F, 0.0F, 0.0F},
                           .kind = ic2d::InteractionKind::pickup_ammo,
                           .amount = 12.0F,
                           .radius = 30.0F},
        ic2d::Interactable{.entity = {11},
                           .position = {20.0F, 0.0F, 0.0F},
                           .kind = ic2d::InteractionKind::pickup_ammo,
                           .amount = 24.0F,
                           .radius = 30.0F},
        ic2d::Interactable{.entity = {12},
                           .position = {900.0F, 0.0F, 900.0F},
                           .kind = ic2d::InteractionKind::pickup_ammo,
                           .amount = 24.0F,
                           .radius = 30.0F},
    };
}

} // namespace

TEST_CASE("loading rejects records that cannot describe a usable item") {
    // Loading rejects records that cannot describe a usable item, rather than
    // carrying them and failing later.
    ic2d::Interaction interaction;
    const std::array<ic2d::Interactable, 4> mixed{
        ic2d::Interactable{.entity = {1}, .amount = 5.0F, .radius = 10.0F},
        ic2d::Interactable{.entity = {}, .amount = 5.0F, .radius = 10.0F},
        ic2d::Interactable{.entity = {3}, .amount = 0.0F, .radius = 10.0F},
        ic2d::Interactable{.entity = {4}, .amount = 5.0F, .radius = 0.0F},
    };
    CHECK_MESSAGE((interaction.load(mixed) == 1),
                  "Only interactables that describe a usable item may be accepted.");
    CHECK_MESSAGE((interaction.snapshot().remaining_count == 1),
                  "Loading must report how many items remain to be used.");
}

TEST_CASE("out of range there is no candidate") {
    // Out of range there is no candidate, so no prompt can be shown and a
    // press cannot consume anything.
    ic2d::Interaction interaction;
    const auto items = fixture();
    static_cast<void>(interaction.load(items));
    static_cast<void>(interaction.fixed_update(1, player, {500.0F, 0.0F, 500.0F}, true));
    CHECK_MESSAGE((!interaction.snapshot().candidate.has_value()),
                  "An actor outside every radius must have no candidate.");
    CHECK_MESSAGE((interaction.snapshot().performed_count == 0),
                  "A press with no candidate must consume nothing.");
    CHECK_MESSAGE((interaction.drain_events().empty()),
                  "A press with no candidate must emit no event.");
}

TEST_CASE("in range of two, the nearer one is offered") {
    // In range of two, the nearer one is offered.
    ic2d::Interaction interaction;
    const auto items = fixture();
    static_cast<void>(interaction.load(items));
    static_cast<void>(interaction.fixed_update(1, player, {14.0F, 0.0F, 0.0F}, false));
    const ic2d::InteractionSnapshot& snapshot = interaction.snapshot();
    CHECK_MESSAGE((snapshot.available_count == 2), "Both items in range must be counted.");
    CHECK_MESSAGE((snapshot.candidate.has_value() && snapshot.candidate->entity.value == 11),
                  "The nearest item in range must be the candidate.");
    CHECK_MESSAGE((snapshot.candidate && std::abs(snapshot.candidate->distance - 6.0F) < 0.001F),
                  "The candidate must report its ground distance.");
    CHECK_MESSAGE((snapshot.performed_count == 0),
                  "Standing near an item must not use it without a request.");
}

TEST_CASE("using an item emits exactly one event and removes it from play") {
    // Using an item emits exactly one event and removes it from play.
    ic2d::Interaction interaction;
    const auto items = fixture();
    static_cast<void>(interaction.load(items));
    static_cast<void>(interaction.fixed_update(1, player, {14.0F, 0.0F, 0.0F}, true));
    std::vector<ic2d::InteractionPerformedEvent> events = interaction.drain_events();
    CHECK_MESSAGE((events.size() == 1), "Using an item must emit exactly one event.");
    CHECK_MESSAGE((events.front().entity.value == 11 && events.front().actor == player &&
                   events.front().amount == 24.0F && events.front().tick == 1),
                  "The event must carry the actor, the item, its payload, and its tick.");
    CHECK_MESSAGE((interaction.consumed({11})), "A used item must be consumed.");
    CHECK_MESSAGE((interaction.snapshot().remaining_count == 2),
                  "Using an item must reduce what remains.");
    CHECK_MESSAGE((!interaction.snapshot().candidate.has_value()),
                  "The used item must stop being offered on the tick it is used.");
    CHECK_MESSAGE((interaction.drain_events().empty()), "Draining twice must not repeat an event.");

    // With the nearer item gone the next one is offered instead.
    static_cast<void>(interaction.fixed_update(2, player, {14.0F, 0.0F, 0.0F}, false));
    CHECK_MESSAGE((interaction.snapshot().candidate.has_value() &&
                   interaction.snapshot().candidate->entity.value == 10),
                  "With the nearer item gone, the next one must be offered.");

    // A consumed item can never be used a second time.
    static_cast<void>(interaction.fixed_update(3, player, {14.0F, 0.0F, 0.0F}, true));
    std::vector<ic2d::InteractionPerformedEvent> second = interaction.drain_events();
    CHECK_MESSAGE((second.size() == 1 && second.front().entity.value == 10),
                  "The next press must use the next item, not the consumed one.");
    CHECK_MESSAGE((interaction.snapshot().remaining_count == 1), "Only the far item may remain.");

    static_cast<void>(interaction.fixed_update(4, player, {14.0F, 0.0F, 0.0F}, true));
    CHECK_MESSAGE((interaction.drain_events().empty()),
                  "With everything in range consumed, a press must do nothing.");
}

TEST_CASE("a payload owner that cannot accept an item returns it to play") {
    // A payload owner that cannot accept an item returns it to play, so a full
    // reserve does not silently destroy a pickup.
    ic2d::Interaction interaction;
    const auto items = fixture();
    static_cast<void>(interaction.load(items));
    static_cast<void>(interaction.fixed_update(1, player, {14.0F, 0.0F, 0.0F}, true));
    CHECK_MESSAGE((interaction.consumed({11})), "The item must first be consumed.");
    CHECK_MESSAGE((interaction.decline({11})), "Declining a consumed item must succeed.");
    CHECK_MESSAGE((!interaction.consumed({11})), "A declined item must return to play.");
    CHECK_MESSAGE((interaction.snapshot().remaining_count == 3),
                  "A declined item must count as remaining again.");
    CHECK_MESSAGE((!interaction.decline({11})),
                  "Declining an item already in play must report false.");

    static_cast<void>(interaction.fixed_update(2, player, {14.0F, 0.0F, 0.0F}, false));
    CHECK_MESSAGE((interaction.snapshot().candidate.has_value() &&
                   interaction.snapshot().candidate->entity.value == 11),
                  "A declined item must be offered again.");
}

TEST_CASE("reset returns items to play without needing to reload the scene") {
    // Reset returns items to play without needing to reload the scene.
    ic2d::Interaction interaction;
    const auto items = fixture();
    static_cast<void>(interaction.load(items));
    static_cast<void>(interaction.fixed_update(1, player, {14.0F, 0.0F, 0.0F}, true));
    interaction.reset();
    CHECK_MESSAGE((!interaction.consumed({11})), "Reset must return consumed items to play.");
    CHECK_MESSAGE((interaction.snapshot().remaining_count == 3),
                  "Reset must restore the full authored set.");
    CHECK_MESSAGE((interaction.snapshot().performed_count == 0),
                  "Reset must clear the performed count.");
}

TEST_CASE("a press made with nothing in range is not spent") {
    // A press made with nothing in range is not spent, so a caller can keep
    // offering it. This is what makes walking up to an item with the key
    // already held pick it up, instead of leaving the prompt showing over an
    // item that only a fresh press could use.
    ic2d::Interaction interaction;
    const auto items = fixture();
    static_cast<void>(interaction.load(items));
    CHECK_MESSAGE((!interaction.fixed_update(1, player, {500.0F, 0.0F, 500.0F}, true)),
                  "A press with nothing in range must not be spent.");
    CHECK_MESSAGE((interaction.snapshot().performed_count == 0),
                  "A press with nothing in range must consume nothing.");
    // The same press, still held, once the actor has walked into range.
    CHECK_MESSAGE((interaction.fixed_update(2, player, {14.0F, 0.0F, 0.0F}, true)),
                  "A held press must be spent by the first tick that has an item.");
    CHECK_MESSAGE((interaction.consumed({11})),
                  "A held press must use the item the actor walked up to.");
}

TEST_CASE("a spent press is spent exactly once") {
    // A spent press is spent exactly once: the tick that used an item reports
    // it, so holding the key cannot empty a room.
    ic2d::Interaction interaction;
    const auto items = fixture();
    static_cast<void>(interaction.load(items));
    CHECK_MESSAGE((interaction.fixed_update(1, player, {10.0F, 0.0F, 0.0F}, true)),
                  "A press with an item in range must be spent.");
    CHECK_MESSAGE((interaction.snapshot().performed_count == 1),
                  "One press must use exactly one item.");
}

TEST_CASE("a tick that is offered nothing has no press to spend") {
    // A tick that is offered nothing has no press to spend.
    ic2d::Interaction interaction;
    const auto items = fixture();
    static_cast<void>(interaction.load(items));
    CHECK_MESSAGE((!interaction.fixed_update(1, player, {14.0F, 0.0F, 0.0F}, false)),
                  "A tick with no request must report no press spent.");
    CHECK_MESSAGE((!interaction.fixed_update(2, {}, {14.0F, 0.0F, 0.0F}, true)),
                  "A request from no actor must not be spent.");
}

TEST_CASE("a pickup adds to the reserve and a full reserve declines it") {
    // The payload half: a pickup adds to the reserve, and a full reserve
    // declines rather than silently swallowing the item.
    ic2d::Combat combat;
    const std::uint32_t taken = combat.resupply(player, 30);
    CHECK_MESSAGE((taken == 30), "A resupply below the ceiling must take every round.");
    const ic2d::WeaponSnapshot before = combat.snapshot().weapon;
    CHECK_MESSAGE((before.reserve_ammo == 78),
                  "Resupply must add to the authored starting reserve.");

    const std::uint32_t topped = combat.resupply(player, 100000);
    CHECK_MESSAGE((topped == 240 - 78), "Resupply must stop at the reserve ceiling.");
    CHECK_MESSAGE((combat.snapshot().weapon.reserve_ammo == 240),
                  "The reserve must saturate at its ceiling.");
    CHECK_MESSAGE((combat.resupply(player, 10) == 0),
                  "A full reserve must decline a pickup instead of consuming it.");
    CHECK_MESSAGE((combat.resupply({}, 10) == 0), "Resupply requires a real actor.");
    CHECK_MESSAGE((combat.resupply(player, 0) == 0), "An empty resupply must take nothing.");
}
