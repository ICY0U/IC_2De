#pragma once

#include "ic2d/combat.hpp"
#include "ic2d/types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ic2d {

struct ProjectileStateSnapshot {
    std::uint64_t projectile_id{0};
    EntityUuid actor{};
    WeaponKind weapon{WeaponKind::needle_pistol};
    Vec3 previous_position{};
    Vec3 position{};
    Vec2 direction{};
    float speed{0.0F};
    std::uint32_t lifetime_ticks_remaining{0};
    float damage{0.0F};
};

struct ProjectileExpiredEvent {
    std::uint64_t tick{0};
    std::uint64_t projectile_id{0};
    EntityUuid actor{};
    Vec3 position{};
};

struct ProjectileImpact {
    std::uint64_t tick{0};
    std::uint64_t projectile_id{0};
    EntityUuid target{};
    Vec3 position{};
    Vec2 normal{};
    std::uint32_t tag{0};
};

struct ProjectileImpactEvent {
    std::uint64_t tick{0};
    std::uint64_t projectile_id{0};
    EntityUuid actor{};
    EntityUuid target{};
    WeaponKind weapon{WeaponKind::needle_pistol};
    Vec3 position{};
    Vec2 normal{};
    std::uint32_t tag{0};
    float damage{0.0F};
};

struct ProjectileSimulationSnapshot {
    std::uint64_t tick{0};
    std::uint64_t total_spawned{0};
    std::uint64_t total_expired{0};
    std::uint64_t total_impacted{0};
    std::size_t pending_spawn_count{0};
    std::vector<ProjectileStateSnapshot> active;
};

// Owns deterministic projectile movement and lifetime. Spawn requests are
// copied from Combat and remain buffered until their matching fixed tick.
// Rendering and collision consume copied snapshots; no backend type crosses
// this interface.
class ProjectileSimulation final {
public:
    ProjectileSimulation();
    ~ProjectileSimulation();

    ProjectileSimulation(const ProjectileSimulation&) = delete;
    ProjectileSimulation& operator=(const ProjectileSimulation&) = delete;
    ProjectileSimulation(ProjectileSimulation&&) noexcept;
    ProjectileSimulation& operator=(ProjectileSimulation&&) noexcept;

    [[nodiscard]] bool spawn(
        const ProjectileSpawnedEvent& event,
        const Vec3& world_origin
    ) noexcept;
    void fixed_update(std::uint64_t tick, float fixed_step_seconds);
    [[nodiscard]] bool resolve_impact(const ProjectileImpact& impact) noexcept;

    [[nodiscard]] ProjectileSimulationSnapshot snapshot() const;
    [[nodiscard]] std::vector<ProjectileExpiredEvent> drain_expired_events();
    [[nodiscard]] std::vector<ProjectileImpactEvent> drain_impact_events();
    void reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
