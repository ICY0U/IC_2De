#include "ic2d/combat.hpp"
#include "ic2d/interaction.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <string_view>

namespace {

int interaction_failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++interaction_failures;
    }
}

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

int main() {
    // Loading rejects records that cannot describe a usable item, rather than
    // carrying them and failing later.
    {
        ic2d::Interaction interaction;
        const std::array<ic2d::Interactable, 4> mixed{
            ic2d::Interactable{.entity = {1}, .amount = 5.0F, .radius = 10.0F},
            ic2d::Interactable{.entity = {}, .amount = 5.0F, .radius = 10.0F},
            ic2d::Interactable{.entity = {3}, .amount = 0.0F, .radius = 10.0F},
            ic2d::Interactable{.entity = {4}, .amount = 5.0F, .radius = 0.0F},
        };
        expect(interaction.load(mixed) == 1,
               "Only interactables that describe a usable item may be accepted.");
        expect(interaction.snapshot().remaining_count == 1,
               "Loading must report how many items remain to be used.");
    }

    // Out of range there is no candidate, so no prompt can be shown and a
    // press cannot consume anything.
    {
        ic2d::Interaction interaction;
        const auto items = fixture();
        static_cast<void>(interaction.load(items));
        static_cast<void>(interaction.fixed_update(1, player, {500.0F, 0.0F, 500.0F}, true));
        expect(!interaction.snapshot().candidate.has_value(),
               "An actor outside every radius must have no candidate.");
        expect(interaction.snapshot().performed_count == 0,
               "A press with no candidate must consume nothing.");
        expect(interaction.drain_events().empty(),
               "A press with no candidate must emit no event.");
    }

    // In range of two, the nearer one is offered.
    {
        ic2d::Interaction interaction;
        const auto items = fixture();
        static_cast<void>(interaction.load(items));
        static_cast<void>(interaction.fixed_update(1, player, {14.0F, 0.0F, 0.0F}, false));
        const ic2d::InteractionSnapshot& snapshot = interaction.snapshot();
        expect(snapshot.available_count == 2, "Both items in range must be counted.");
        expect(snapshot.candidate.has_value() && snapshot.candidate->entity.value == 11,
               "The nearest item in range must be the candidate.");
        expect(snapshot.candidate && std::abs(snapshot.candidate->distance - 6.0F) < 0.001F,
               "The candidate must report its ground distance.");
        expect(snapshot.performed_count == 0,
               "Standing near an item must not use it without a request.");
    }

    // Using an item emits exactly one event and removes it from play.
    {
        ic2d::Interaction interaction;
        const auto items = fixture();
        static_cast<void>(interaction.load(items));
        static_cast<void>(interaction.fixed_update(1, player, {14.0F, 0.0F, 0.0F}, true));
        std::vector<ic2d::InteractionPerformedEvent> events = interaction.drain_events();
        expect(events.size() == 1, "Using an item must emit exactly one event.");
        expect(events.front().entity.value == 11 && events.front().actor == player &&
                   events.front().amount == 24.0F && events.front().tick == 1,
               "The event must carry the actor, the item, its payload, and its tick.");
        expect(interaction.consumed({11}), "A used item must be consumed.");
        expect(interaction.snapshot().remaining_count == 2,
               "Using an item must reduce what remains.");
        expect(!interaction.snapshot().candidate.has_value(),
               "The used item must stop being offered on the tick it is used.");
        expect(interaction.drain_events().empty(), "Draining twice must not repeat an event.");

        // With the nearer item gone the next one is offered instead.
        static_cast<void>(interaction.fixed_update(2, player, {14.0F, 0.0F, 0.0F}, false));
        expect(interaction.snapshot().candidate.has_value() &&
                   interaction.snapshot().candidate->entity.value == 10,
               "With the nearer item gone, the next one must be offered.");

        // A consumed item can never be used a second time.
        static_cast<void>(interaction.fixed_update(3, player, {14.0F, 0.0F, 0.0F}, true));
        std::vector<ic2d::InteractionPerformedEvent> second = interaction.drain_events();
        expect(second.size() == 1 && second.front().entity.value == 10,
               "The next press must use the next item, not the consumed one.");
        expect(interaction.snapshot().remaining_count == 1,
               "Only the far item may remain.");

        static_cast<void>(interaction.fixed_update(4, player, {14.0F, 0.0F, 0.0F}, true));
        expect(interaction.drain_events().empty(),
               "With everything in range consumed, a press must do nothing.");
    }

    // A payload owner that cannot accept an item returns it to play, so a full
    // reserve does not silently destroy a pickup.
    {
        ic2d::Interaction interaction;
        const auto items = fixture();
        static_cast<void>(interaction.load(items));
        static_cast<void>(interaction.fixed_update(1, player, {14.0F, 0.0F, 0.0F}, true));
        expect(interaction.consumed({11}), "The item must first be consumed.");
        expect(interaction.decline({11}), "Declining a consumed item must succeed.");
        expect(!interaction.consumed({11}), "A declined item must return to play.");
        expect(interaction.snapshot().remaining_count == 3,
               "A declined item must count as remaining again.");
        expect(!interaction.decline({11}), "Declining an item already in play must report false.");

        static_cast<void>(interaction.fixed_update(2, player, {14.0F, 0.0F, 0.0F}, false));
        expect(interaction.snapshot().candidate.has_value() &&
                   interaction.snapshot().candidate->entity.value == 11,
               "A declined item must be offered again.");
    }

    // Reset returns items to play without needing to reload the scene.
    {
        ic2d::Interaction interaction;
        const auto items = fixture();
        static_cast<void>(interaction.load(items));
        static_cast<void>(interaction.fixed_update(1, player, {14.0F, 0.0F, 0.0F}, true));
        interaction.reset();
        expect(!interaction.consumed({11}), "Reset must return consumed items to play.");
        expect(interaction.snapshot().remaining_count == 3,
               "Reset must restore the full authored set.");
        expect(interaction.snapshot().performed_count == 0,
               "Reset must clear the performed count.");
    }

    // A press made with nothing in range is not spent, so a caller can keep
    // offering it. This is what makes walking up to an item with the key
    // already held pick it up, instead of leaving the prompt showing over an
    // item that only a fresh press could use.
    {
        ic2d::Interaction interaction;
        const auto items = fixture();
        static_cast<void>(interaction.load(items));
        expect(!interaction.fixed_update(1, player, {500.0F, 0.0F, 500.0F}, true),
               "A press with nothing in range must not be spent.");
        expect(interaction.snapshot().performed_count == 0,
               "A press with nothing in range must consume nothing.");
        // The same press, still held, once the actor has walked into range.
        expect(interaction.fixed_update(2, player, {14.0F, 0.0F, 0.0F}, true),
               "A held press must be spent by the first tick that has an item.");
        expect(interaction.consumed({11}),
               "A held press must use the item the actor walked up to.");
    }

    // A spent press is spent exactly once: the tick that used an item reports
    // it, so holding the key cannot empty a room.
    {
        ic2d::Interaction interaction;
        const auto items = fixture();
        static_cast<void>(interaction.load(items));
        expect(interaction.fixed_update(1, player, {10.0F, 0.0F, 0.0F}, true),
               "A press with an item in range must be spent.");
        expect(interaction.snapshot().performed_count == 1,
               "One press must use exactly one item.");
    }

    // A tick that is offered nothing has no press to spend.
    {
        ic2d::Interaction interaction;
        const auto items = fixture();
        static_cast<void>(interaction.load(items));
        expect(!interaction.fixed_update(1, player, {14.0F, 0.0F, 0.0F}, false),
               "A tick with no request must report no press spent.");
        expect(!interaction.fixed_update(2, {}, {14.0F, 0.0F, 0.0F}, true),
               "A request from no actor must not be spent.");
    }

    // The payload half: a pickup adds to the reserve, and a full reserve
    // declines rather than silently swallowing the item.
    {
        ic2d::Combat combat;
        const std::uint32_t taken = combat.resupply(player, 30);
        expect(taken == 30, "A resupply below the ceiling must take every round.");
        const ic2d::WeaponSnapshot before = combat.snapshot().weapon;
        expect(before.reserve_ammo == 78,
               "Resupply must add to the authored starting reserve.");

        const std::uint32_t topped = combat.resupply(player, 100000);
        expect(topped == 240 - 78, "Resupply must stop at the reserve ceiling.");
        expect(combat.snapshot().weapon.reserve_ammo == 240,
               "The reserve must saturate at its ceiling.");
        expect(combat.resupply(player, 10) == 0,
               "A full reserve must decline a pickup instead of consuming it.");
        expect(combat.resupply({}, 10) == 0, "Resupply requires a real actor.");
        expect(combat.resupply(player, 0) == 0, "An empty resupply must take nothing.");
    }

    if (interaction_failures == 0) {
        std::cout << "Interaction tests passed.\n";
    }
    return interaction_failures == 0 ? 0 : 1;
}
