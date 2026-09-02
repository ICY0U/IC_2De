#pragma once

#include "ic2d/identity.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace ic2d {

struct HitIdentity {
    EntityUuid source{};
    std::uint64_t value{0};

    auto operator<=>(const HitIdentity&) const = default;
};

struct HealthTargetDefinition {
    EntityUuid target{};
    float maximum_health{0.0F};
};

struct DamageCommand {
    std::uint64_t tick{0};
    HitIdentity hit{};
    EntityUuid target{};
    float damage{0.0F};
};

struct DamageAppliedEvent {
    std::uint64_t tick{0};
    std::uint64_t sequence{0};
    HitIdentity hit{};
    EntityUuid target{};
    float requested_damage{0.0F};
    float applied_damage{0.0F};
    float health_before{0.0F};
    float health_after{0.0F};
};

struct ActorDiedEvent {
    std::uint64_t tick{0};
    std::uint64_t sequence{0};
    HitIdentity killing_hit{};
    EntityUuid target{};
};

using HealthEvent = std::variant<DamageAppliedEvent, ActorDiedEvent>;

struct HealthTargetSnapshot {
    EntityUuid target{};
    float maximum_health{0.0F};
    float current_health{0.0F};
    bool alive{false};
};

struct HealthSnapshot {
    std::uint64_t tick{0};
    std::uint64_t applied_hit_count{0};
    std::uint64_t death_count{0};
    std::uint64_t rejected_duplicate_hit_count{0};
    std::size_t pending_damage_count{0};
    std::uint64_t next_event_sequence{1};
    // Canonical source/value order. This is authoritative future state: the
    // same hit identity must remain rejected for the rest of the run.
    std::vector<HitIdentity> accepted_hits;
    std::vector<HealthTargetSnapshot> targets;
};

// Owns fixed-tick health reduction, stable hit-instance deduplication, and
// exactly-once death events. Target definitions survive reset; transient
// damage, hit history, events, and tick state do not.
class Health final {
public:
    Health();
    ~Health();

    Health(const Health&) = delete;
    Health& operator=(const Health&) = delete;
    Health(Health&&) noexcept;
    Health& operator=(Health&&) noexcept;

    [[nodiscard]] bool register_target(const HealthTargetDefinition& definition) noexcept;
    [[nodiscard]] bool submit(const DamageCommand& command) noexcept;
    void fixed_update(std::uint64_t tick);

    [[nodiscard]] HealthSnapshot snapshot() const;
    [[nodiscard]] std::vector<HealthEvent> drain_events();
    void reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
