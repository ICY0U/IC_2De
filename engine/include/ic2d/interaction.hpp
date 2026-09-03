#pragma once

#include "ic2d/identity.hpp"
#include "ic2d/types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ic2d {

// What using an interactable does. The enum is the extension point: a new kind
// adds a case here and a payload rule in whoever owns that resource, without
// touching how candidates are found or how the prompt is presented.
enum class InteractionKind : std::uint8_t {
    pickup_ammo,
};

[[nodiscard]] std::string_view interaction_kind_name(InteractionKind kind) noexcept;
[[nodiscard]] std::optional<InteractionKind> parse_interaction_kind(std::string_view name) noexcept;

// One authored interactable, as copied facts. The entity supplies identity,
// position, and appearance; this record supplies only behaviour, so a pickup
// composes with prefabs, the gizmo, and the inspector like any other placement.
struct Interactable {
    EntityUuid entity{};
    Vec3 position{};
    InteractionKind kind{InteractionKind::pickup_ammo};
    float amount{0.0F};
    // How close the actor must be. Authored per item so a large object can be
    // usable from further away than a small one.
    float radius{28.0F};
};

// What the player could use right now, if anything.
struct InteractionCandidate {
    EntityUuid entity{};
    InteractionKind kind{InteractionKind::pickup_ammo};
    float amount{0.0F};
    float distance{0.0F};
};

// Emitted on the tick an interactable is used. The payload is described, not
// applied: whoever owns ammo or health consumes this and decides what happens,
// so Interaction never learns another module's rules.
struct InteractionPerformedEvent {
    std::uint64_t tick{0};
    std::uint64_t sequence{0};
    EntityUuid actor{};
    EntityUuid entity{};
    InteractionKind kind{InteractionKind::pickup_ammo};
    float amount{0.0F};
};

struct InteractionSnapshot {
    std::uint64_t tick{0};
    std::optional<InteractionCandidate> candidate;
    std::size_t available_count{0};
    std::size_t remaining_count{0};
    std::uint64_t performed_count{0};
    std::uint64_t next_event_sequence{1};
    // Consumed entities, in canonical order. This is authoritative future
    // state: a consumed interactable stays consumed for the rest of the run.
    std::vector<EntityUuid> consumed;
};

// Owns which interactables exist, which have been used, and which one the
// player is close enough to use. It is deliberately ignorant of ammo, health,
// input devices, and rendering: it reports a candidate and emits an event, and
// the application routes the payload.
//
// Resolution is deterministic. Candidates are ranked by distance and ties are
// broken by entity identity, so the same tick always chooses the same item.
class Interaction final {
public:
    Interaction();
    ~Interaction();

    Interaction(const Interaction&) = delete;
    Interaction& operator=(const Interaction&) = delete;
    Interaction(Interaction&&) noexcept;
    Interaction& operator=(Interaction&&) noexcept;

    // Replaces the authored set and forgets every consumed item and event.
    // Interactables with a non-positive radius or amount are rejected; the
    // return value is how many were accepted.
    std::size_t load(std::span<const Interactable> interactables);

    // One fixed tick. `requested` is an edge: the frame the interact action was
    // pressed, not whether it is held, so holding the key uses one item.
    //
    // Returns whether the request was spent. A press made with nothing in
    // range is not spent, so the caller can keep offering it while the key is
    // held rather than throwing it away on the tick it arrived: pressing just
    // before the prompt appears, or holding the key while walking up to an
    // item, must still use the item once it is actually reachable.
    [[nodiscard]] bool fixed_update(
        std::uint64_t tick,
        EntityUuid actor,
        Vec3 actor_position,
        bool requested
    );

    [[nodiscard]] const InteractionSnapshot& snapshot() const noexcept;
    [[nodiscard]] std::vector<InteractionPerformedEvent> drain_events();
    [[nodiscard]] bool consumed(EntityUuid entity) const noexcept;
    // Returns a used item to play. The payload owner calls this when it could
    // not accept what the item offered, so a full reserve leaves the pickup
    // standing instead of silently destroying it.
    [[nodiscard]] bool decline(EntityUuid entity) noexcept;
    // Forgets consumed items and events; the authored set survives.
    void reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
