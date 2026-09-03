#pragma once

#include "ic2d/identity.hpp"
#include "ic2d/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace ic2d {

enum class CombatIntent : std::uint8_t {
    fire,
    reload,
    dodge,
    swap_weapon,
    count,
};

inline constexpr std::array combat_intents{
    CombatIntent::fire,
    CombatIntent::reload,
    CombatIntent::dodge,
    CombatIntent::swap_weapon,
};

[[nodiscard]] std::string_view combat_intent_name(CombatIntent intent) noexcept;

enum class WeaponKind : std::uint8_t {
    needle_pistol,
};

struct WeaponDefinition {
    WeaponKind kind{WeaponKind::needle_pistol};
    std::uint32_t magazine_capacity{12};
    std::uint32_t initial_reserve_ammo{48};
    // A reserve ceiling makes a pickup refusable and keeps the counter from
    // growing without bound over a long run.
    std::uint32_t maximum_reserve_ammo{240};
    std::uint32_t fire_cooldown_ticks{8};
    std::uint32_t reload_duration_ticks{54};
    float projectile_speed{520.0F};
    std::uint32_t projectile_lifetime_ticks{72};
    float projectile_damage{18.0F};
};

inline constexpr WeaponDefinition needle_pistol{};

struct DodgeDefinition {
    std::uint32_t duration_ticks{12};
    std::uint32_t invulnerability_ticks{9};
    std::uint32_t cooldown_ticks{36};
    float movement_speed_multiplier{3.0F};
};

inline constexpr DodgeDefinition player_dodge{};
static_assert(player_dodge.invulnerability_ticks <= player_dodge.duration_ticks);
static_assert(player_dodge.duration_ticks <= player_dodge.cooldown_ticks);

// One render-frame request. The Combat module latches requested intents until
// the next fixed tick and keeps only engine-owned identity and math types.
struct CombatCommand {
    EntityUuid actor{};
    std::optional<Vec2> aim_direction;
    // World X/Z direction sampled on the dodge edge. Combat normalizes and
    // freezes it only when the dodge actually starts.
    std::optional<Vec2> dodge_direction;

    void request(CombatIntent intent, bool requested = true) noexcept;
    // Continuous fire is submitted only when the input state changes, then
    // remains authoritative until a matching release update arrives.
    void set_fire_held(bool held) noexcept;
    [[nodiscard]] bool requested(CombatIntent intent) const noexcept;
    [[nodiscard]] std::optional<bool> fire_held_update() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    std::array<bool, combat_intents.size()> intents_{};
    std::optional<bool> fire_held_update_;
};

// A copied acknowledgement that one buffered intent reached the authoritative
// fixed tick. Later gameplay slices add result events without changing command
// buffering or exposing internal queues.
struct CombatIntentEvent {
    std::uint64_t tick{0};
    std::uint64_t sequence{0};
    EntityUuid actor{};
    CombatIntent intent{CombatIntent::fire};
    Vec2 aim_direction{};
};

// A self-contained fixed-tick request for the later projectile module. The
// consumer resolves the actor's world origin at this event's tick; no pointer
// or backend physics type crosses the Combat seam.
struct ProjectileSpawnedEvent {
    std::uint64_t tick{0};
    std::uint64_t sequence{0};
    std::uint64_t projectile_id{0};
    EntityUuid actor{};
    WeaponKind weapon{WeaponKind::needle_pistol};
    Vec2 aim_direction{};
    float speed{0.0F};
    std::uint32_t lifetime_ticks{0};
    float damage{0.0F};
};

struct DodgeStartedEvent {
    std::uint64_t tick{0};
    std::uint64_t sequence{0};
    EntityUuid actor{};
    Vec2 direction{0.0F, 1.0F};
    std::uint32_t duration_ticks{0};
    std::uint32_t invulnerability_ticks{0};
    std::uint32_t cooldown_ticks{0};
};

using CombatEvent = std::variant<CombatIntentEvent, ProjectileSpawnedEvent, DodgeStartedEvent>;

struct WeaponSnapshot {
    WeaponKind weapon{WeaponKind::needle_pistol};
    std::uint32_t magazine_ammo{0};
    std::uint32_t reserve_ammo{0};
    std::uint32_t fire_cooldown_ticks_remaining{0};
    std::uint32_t reload_ticks_remaining{0};
    bool reloading{false};
};

struct DodgeSnapshot {
    Vec2 direction{0.0F, 1.0F};
    std::uint32_t active_ticks_remaining{0};
    std::uint32_t invulnerable_ticks_remaining{0};
    std::uint32_t cooldown_ticks_remaining{0};
    bool active{false};
    bool invulnerable{false};
    std::uint64_t started_count{0};
};

struct CombatActorSnapshot {
    EntityUuid actor{};
    Vec2 aim_direction{0.0F, 1.0F};
    WeaponSnapshot weapon{};
    DodgeSnapshot dodge{};
    bool fire_held{false};
};

struct CombatSnapshot {
    std::uint64_t tick{0};
    std::uint64_t consumed_command_count{0};
    std::uint64_t emitted_intent_count{0};
    std::size_t pending_command_count{0};
    EntityUuid latest_actor{};
    Vec2 aim_direction{0.0F, 1.0F};
    WeaponSnapshot weapon{};
    DodgeSnapshot dodge{};
    std::uint64_t spawned_projectile_count{0};
    std::uint64_t next_event_sequence{1};
    std::uint64_t next_projectile_id{1};
    // Canonical UUID order. This is the complete per-actor state used by
    // deterministic replay and save-oriented consumers; the scalar actor
    // fields above remain a convenient latest-actor view for editor telemetry.
    std::vector<CombatActorSnapshot> actors;
};

class Combat final {
public:
    Combat();
    ~Combat();

    Combat(const Combat&) = delete;
    Combat& operator=(const Combat&) = delete;
    Combat(Combat&&) noexcept;
    Combat& operator=(Combat&&) noexcept;

    // Returns false without mutating state when identity, aim, or content is
    // invalid. Valid commands remain buffered until fixed_update().
    [[nodiscard]] bool submit(const CombatCommand& command) noexcept;

    // Adds rounds to an actor's reserve and returns how many were actually
    // taken. A pickup reports that number, so a full reserve can decline the
    // item instead of silently consuming it. Applies immediately: resupply
    // carries no aim or timing and needs no fixed-tick ordering.
    [[nodiscard]] std::uint32_t resupply(EntityUuid actor, std::uint32_t rounds) noexcept;

    // Fills the actor's magazine to capacity and its reserve to the ceiling,
    // and abandons any reload in progress because there is nothing left to
    // reload into. Returns false for an invalid identity. Like resupply this
    // carries no aim or timing and applies immediately.
    //
    // Distinct from resupply, which tops up the reserve an actor still has to
    // reload from. This is the whole-weapon refill a full pickup grants, and
    // repeating it every tick is what an unlimited-ammo run amounts to.
    [[nodiscard]] bool replenish(EntityUuid actor) noexcept;

    // Ticks are non-zero and strictly sequential. Violations throw
    // std::invalid_argument before any buffered command is consumed.
    void fixed_update(std::uint64_t tick);

    [[nodiscard]] CombatSnapshot snapshot() const;
    [[nodiscard]] bool invulnerable(EntityUuid actor) const noexcept;
    [[nodiscard]] std::vector<CombatEvent> drain_events();
    void reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
