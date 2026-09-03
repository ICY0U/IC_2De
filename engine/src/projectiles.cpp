#include "ic2d/projectiles.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ic2d {
namespace {

[[nodiscard]] bool finite(const Vec2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

struct ProjectileSimulation::Impl {
    std::vector<ProjectileStateSnapshot> pending;
    std::vector<ProjectileExpiredEvent> expired_events;
    std::vector<ProjectileImpactEvent> impact_events;
    ProjectileSimulationSnapshot snapshot{};
};

ProjectileSimulation::ProjectileSimulation() : impl_{std::make_unique<Impl>()} {}
ProjectileSimulation::~ProjectileSimulation() = default;
ProjectileSimulation::ProjectileSimulation(ProjectileSimulation&&) noexcept = default;
ProjectileSimulation& ProjectileSimulation::operator=(ProjectileSimulation&&) noexcept = default;

bool ProjectileSimulation::spawn(const ProjectileSpawnedEvent& event,
                                 const Vec3& world_origin) noexcept {
    const float direction_length = std::sqrt(event.aim_direction.x * event.aim_direction.x +
                                             event.aim_direction.y * event.aim_direction.y);
    if (event.tick != impl_->snapshot.tick + 1 || event.projectile_id == 0 || !event.actor ||
        !finite(event.aim_direction) || !(direction_length > 0.0001F) ||
        !std::isfinite(event.speed) || !(event.speed > 0.0F) || event.lifetime_ticks == 0 ||
        !std::isfinite(event.damage) || event.damage < 0.0F || !finite(world_origin)) {
        return false;
    }
    const auto has_id = [&event](const ProjectileStateSnapshot& projectile) {
        return projectile.projectile_id == event.projectile_id;
    };
    if (std::ranges::any_of(impl_->pending, has_id) ||
        std::ranges::any_of(impl_->snapshot.active, has_id)) {
        return false;
    }

    impl_->pending.push_back({
        .projectile_id = event.projectile_id,
        .actor = event.actor,
        .weapon = event.weapon,
        .previous_position = world_origin,
        .position = world_origin,
        .direction =
            {
                event.aim_direction.x / direction_length,
                event.aim_direction.y / direction_length,
            },
        .speed = event.speed,
        .lifetime_ticks_remaining = event.lifetime_ticks,
        .damage = event.damage,
    });
    impl_->snapshot.pending_spawn_count = impl_->pending.size();
    return true;
}

void ProjectileSimulation::fixed_update(const std::uint64_t tick, const float fixed_step_seconds) {
    if (tick != impl_->snapshot.tick + 1 || !std::isfinite(fixed_step_seconds) ||
        !(fixed_step_seconds > 0.0F)) {
        throw std::invalid_argument{
            "Projectile fixed ticks must be sequential with a positive finite duration."};
    }
    for (ProjectileStateSnapshot& projectile : impl_->pending) {
        impl_->snapshot.active.push_back(std::move(projectile));
        ++impl_->snapshot.total_spawned;
    }
    impl_->pending.clear();
    impl_->snapshot.pending_spawn_count = 0;

    for (ProjectileStateSnapshot& projectile : impl_->snapshot.active) {
        projectile.previous_position = projectile.position;
        projectile.position.x += projectile.direction.x * projectile.speed * fixed_step_seconds;
        projectile.position.z += projectile.direction.y * projectile.speed * fixed_step_seconds;
        if (projectile.lifetime_ticks_remaining > 0) {
            --projectile.lifetime_ticks_remaining;
        }
    }
    std::erase_if(impl_->snapshot.active, [this, tick](const ProjectileStateSnapshot& projectile) {
        if (projectile.lifetime_ticks_remaining != 0) {
            return false;
        }
        impl_->expired_events.push_back({
            .tick = tick,
            .projectile_id = projectile.projectile_id,
            .actor = projectile.actor,
            .position = projectile.position,
        });
        ++impl_->snapshot.total_expired;
        return true;
    });
    impl_->snapshot.tick = tick;
}

bool ProjectileSimulation::resolve_impact(const ProjectileImpact& impact) noexcept {
    if (impact.tick != impl_->snapshot.tick || impact.projectile_id == 0 ||
        !finite(impact.position) || !finite(impact.normal)) {
        return false;
    }
    const auto found = std::ranges::find(impl_->snapshot.active, impact.projectile_id,
                                         &ProjectileStateSnapshot::projectile_id);
    if (found == impl_->snapshot.active.end() || (impact.target && impact.target == found->actor)) {
        return false;
    }

    impl_->impact_events.push_back({
        .tick = impact.tick,
        .projectile_id = found->projectile_id,
        .actor = found->actor,
        .target = impact.target,
        .weapon = found->weapon,
        .position = impact.position,
        .normal = impact.normal,
        .tag = impact.tag,
        .damage = found->damage,
    });
    impl_->snapshot.active.erase(found);
    ++impl_->snapshot.total_impacted;
    return true;
}

ProjectileSimulationSnapshot ProjectileSimulation::snapshot() const { return impl_->snapshot; }

std::vector<ProjectileExpiredEvent> ProjectileSimulation::drain_expired_events() {
    std::vector<ProjectileExpiredEvent> drained;
    drained.swap(impl_->expired_events);
    return drained;
}

std::vector<ProjectileImpactEvent> ProjectileSimulation::drain_impact_events() {
    std::vector<ProjectileImpactEvent> drained;
    drained.swap(impl_->impact_events);
    return drained;
}

void ProjectileSimulation::reset() noexcept {
    impl_->pending.clear();
    impl_->expired_events.clear();
    impl_->impact_events.clear();
    impl_->snapshot = {};
}

} // namespace ic2d
