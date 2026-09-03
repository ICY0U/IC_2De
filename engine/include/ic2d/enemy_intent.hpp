#pragma once

#include "ic2d/identity.hpp"
#include "ic2d/types.hpp"

#include <compare>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace ic2d {

enum class EnemyIntentState : std::uint8_t {
    unaware,
    pursuing,
    attacking,
    inactive,
};

struct EnemyIntentDefinition {
    EntityUuid actor{};
    EntityUuid target{};
    float movement_speed{0.0F};
    float acquisition_range{0.0F};
    float attack_range{0.0F};
    std::uint32_t attack_cooldown_ticks{0};
    float attack_damage{0.0F};
};

struct EnemyPerception {
    EntityUuid actor{};
    EntityUuid target{};
    Vec2 actor_position{};
    Vec2 target_position{};
    bool actor_alive{true};
    bool target_alive{true};
};

struct EnemyAcquiredTargetEvent {
    std::uint64_t tick{0};
    std::uint64_t sequence{0};
    EntityUuid actor{};
    EntityUuid target{};
};

struct EnemyAttackRequestedEvent {
    std::uint64_t tick{0};
    std::uint64_t sequence{0};
    EntityUuid actor{};
    EntityUuid target{};
    float damage{0.0F};
};

using EnemyIntentEvent = std::variant<EnemyAcquiredTargetEvent, EnemyAttackRequestedEvent>;

struct EnemyActorIntentSnapshot {
    EntityUuid actor{};
    EntityUuid target{};
    EnemyIntentState state{EnemyIntentState::unaware};
    bool acquired{false};
    Vec2 movement_direction{};
    Vec2 facing_direction{0.0F, 1.0F};
    float distance_to_target{0.0F};
    float movement_speed{0.0F};
    float acquisition_range{0.0F};
    float attack_range{0.0F};
    std::uint32_t attack_cooldown_ticks{0};
    std::uint32_t attack_cooldown_ticks_remaining{0};
    float attack_damage{0.0F};
    std::uint64_t attack_count{0};

    [[nodiscard]] bool operator==(const EnemyActorIntentSnapshot& other) const noexcept {
        return actor == other.actor && target == other.target && state == other.state &&
               acquired == other.acquired && movement_direction.x == other.movement_direction.x &&
               movement_direction.y == other.movement_direction.y &&
               facing_direction.x == other.facing_direction.x &&
               facing_direction.y == other.facing_direction.y &&
               distance_to_target == other.distance_to_target &&
               movement_speed == other.movement_speed &&
               acquisition_range == other.acquisition_range && attack_range == other.attack_range &&
               attack_cooldown_ticks == other.attack_cooldown_ticks &&
               attack_cooldown_ticks_remaining == other.attack_cooldown_ticks_remaining &&
               attack_damage == other.attack_damage && attack_count == other.attack_count;
    }
};

struct EnemyIntentSnapshot {
    std::uint64_t tick{0};
    std::uint64_t next_event_sequence{1};
    std::uint64_t acquisition_count{0};
    std::uint64_t attack_count{0};
    // Canonical actor UUID order, independent of registration or perception order.
    std::vector<EnemyActorIntentSnapshot> actors;
};

// Owns deterministic target acquisition, direct pursuit, attack-range entry,
// and cooldown-authoritative attack requests. Callers provide copied
// perception facts once per registered actor at each sequential fixed tick.
// Navigation and attack resolution remain separate modules.
class EnemyIntent final {
public:
    EnemyIntent();
    ~EnemyIntent();

    EnemyIntent(const EnemyIntent&) = delete;
    EnemyIntent& operator=(const EnemyIntent&) = delete;
    EnemyIntent(EnemyIntent&&) noexcept;
    EnemyIntent& operator=(EnemyIntent&&) noexcept;

    // Definitions survive reset and may only be registered before tick one.
    [[nodiscard]] bool register_actor(const EnemyIntentDefinition& definition) noexcept;
    void fixed_update(std::uint64_t tick, const std::vector<EnemyPerception>& perceptions);

    [[nodiscard]] EnemyIntentSnapshot snapshot() const;
    [[nodiscard]] std::vector<EnemyIntentEvent> drain_events();
    void reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
