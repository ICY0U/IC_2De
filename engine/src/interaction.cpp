#include "ic2d/interaction.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_set>

namespace ic2d {
namespace {

[[nodiscard]] bool finite(const Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

std::string_view interaction_kind_name(const InteractionKind kind) noexcept {
    switch (kind) {
    case InteractionKind::pickup_ammo:
        return "pickup_ammo";
    }
    return "unknown";
}

std::optional<InteractionKind> parse_interaction_kind(const std::string_view name) noexcept {
    if (name == "pickup_ammo") {
        return InteractionKind::pickup_ammo;
    }
    return std::nullopt;
}

struct Interaction::Impl {
    std::vector<Interactable> authored;
    std::unordered_set<std::uint64_t> consumed;
    std::vector<InteractionPerformedEvent> events;
    InteractionSnapshot snapshot{};
    std::uint64_t next_sequence{1};

    void refresh_consumed_snapshot() {
        snapshot.consumed.clear();
        snapshot.consumed.reserve(consumed.size());
        for (const std::uint64_t value : consumed) {
            snapshot.consumed.push_back(EntityUuid{value});
        }
        // Canonical order, so two runs that consumed the same items produce
        // byte-identical snapshots regardless of hash iteration order.
        std::ranges::sort(snapshot.consumed, {}, &EntityUuid::value);
    }
};

Interaction::Interaction() : impl_{std::make_unique<Impl>()} {}
Interaction::~Interaction() = default;
Interaction::Interaction(Interaction&&) noexcept = default;
Interaction& Interaction::operator=(Interaction&&) noexcept = default;

std::size_t Interaction::load(const std::span<const Interactable> interactables) {
    impl_->authored.clear();
    for (const Interactable& item : interactables) {
        if (!item.entity || !finite(item.position) || !std::isfinite(item.radius) ||
            !(item.radius > 0.0F) || !std::isfinite(item.amount) || !(item.amount > 0.0F)) {
            continue;
        }
        impl_->authored.push_back(item);
    }
    // Identity order, so the authored set is canonical however the caller
    // gathered it.
    std::ranges::sort(impl_->authored, {},
                      [](const Interactable& item) { return item.entity.value; });
    reset();
    return impl_->authored.size();
}

bool Interaction::fixed_update(const std::uint64_t tick, const EntityUuid actor,
                               const Vec3 actor_position, const bool requested) {
    impl_->snapshot.tick = tick;
    impl_->snapshot.candidate.reset();
    impl_->snapshot.available_count = 0;

    if (!actor || !finite(actor_position)) {
        return false;
    }

    const Interactable* best = nullptr;
    float best_distance = 0.0F;
    for (const Interactable& item : impl_->authored) {
        if (impl_->consumed.contains(item.entity.value)) {
            continue;
        }
        const float dx = item.position.x - actor_position.x;
        const float dz = item.position.z - actor_position.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance > item.radius) {
            continue;
        }
        ++impl_->snapshot.available_count;
        // Nearest wins; the authored set is already in identity order, so a
        // distance tie resolves to the lower entity id every time.
        if (best == nullptr || distance < best_distance) {
            best = &item;
            best_distance = distance;
        }
    }

    if (best != nullptr) {
        impl_->snapshot.candidate = InteractionCandidate{
            .entity = best->entity,
            .kind = best->kind,
            .amount = best->amount,
            .distance = best_distance,
        };
    }

    // A request with nothing in range is not spent. Reporting it unused is
    // what lets a held key still use the item the actor walks up to.
    if (!requested || best == nullptr) {
        return false;
    }

    impl_->consumed.insert(best->entity.value);
    impl_->events.push_back({
        .tick = tick,
        .sequence = impl_->next_sequence++,
        .actor = actor,
        .entity = best->entity,
        .kind = best->kind,
        .amount = best->amount,
    });
    ++impl_->snapshot.performed_count;
    impl_->snapshot.candidate.reset();
    impl_->snapshot.available_count =
        impl_->snapshot.available_count > 0 ? impl_->snapshot.available_count - 1 : 0;
    impl_->snapshot.remaining_count = impl_->authored.size() - impl_->consumed.size();
    impl_->snapshot.next_event_sequence = impl_->next_sequence;
    impl_->refresh_consumed_snapshot();
    return true;
}

const InteractionSnapshot& Interaction::snapshot() const noexcept { return impl_->snapshot; }

std::vector<InteractionPerformedEvent> Interaction::drain_events() {
    std::vector<InteractionPerformedEvent> drained = std::move(impl_->events);
    impl_->events.clear();
    return drained;
}

bool Interaction::consumed(const EntityUuid entity) const noexcept {
    return impl_->consumed.contains(entity.value);
}

bool Interaction::decline(const EntityUuid entity) noexcept {
    if (impl_->consumed.erase(entity.value) == 0) {
        return false;
    }
    impl_->snapshot.remaining_count = impl_->authored.size() - impl_->consumed.size();
    if (impl_->snapshot.performed_count > 0) {
        --impl_->snapshot.performed_count;
    }
    impl_->refresh_consumed_snapshot();
    return true;
}

void Interaction::reset() noexcept {
    impl_->consumed.clear();
    impl_->events.clear();
    impl_->next_sequence = 1;
    impl_->snapshot = InteractionSnapshot{};
    impl_->snapshot.remaining_count = impl_->authored.size();
}

} // namespace ic2d
