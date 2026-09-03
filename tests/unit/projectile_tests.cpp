#include <doctest/doctest.h>

#include "ic2d/projectiles.hpp"

#include <cmath>
#include <string_view>

namespace {

[[nodiscard]] bool near(const float left, const float right) {
    return std::abs(left - right) < 0.0001F;
}

TEST_CASE("spawn is buffered and moves one fixed tick") {
    ic2d::ProjectileSimulation projectiles;
    const ic2d::ProjectileSpawnedEvent spawn{
        .tick = 1,
        .sequence = 9,
        .projectile_id = 7,
        .actor = {42},
        .weapon = ic2d::WeaponKind::needle_pistol,
        .aim_direction = {0.6F, 0.8F},
        .speed = 120.0F,
        .lifetime_ticks = 10,
        .damage = 18.0F,
    };

    CHECK_MESSAGE((projectiles.spawn(spawn, {10.0F, 3.0F, -4.0F})),
                  "A valid copied Combat spawn must enter the projectile buffer.");
    CHECK_MESSAGE(
        (projectiles.snapshot().active.empty() && projectiles.snapshot().pending_spawn_count == 1),
        "Render-rate submission must not mutate authoritative projectile positions.");

    projectiles.fixed_update(1, 1.0F / 60.0F);
    const ic2d::ProjectileSimulationSnapshot snapshot = projectiles.snapshot();
    CHECK_MESSAGE((snapshot.tick == 1 && snapshot.total_spawned == 1 &&
                   snapshot.pending_spawn_count == 0 && snapshot.active.size() == 1),
                  "The matching fixed tick must activate exactly one buffered projectile.");
    if (snapshot.active.size() == 1) {
        const ic2d::ProjectileStateSnapshot& projectile = snapshot.active.front();
        CHECK_MESSAGE((projectile.projectile_id == 7 && projectile.actor == ic2d::EntityUuid{42}),
                      "Projectile movement must retain stable projectile and actor identity.");
        CHECK_MESSAGE((near(projectile.previous_position.x, 10.0F) &&
                       near(projectile.previous_position.y, 3.0F) &&
                       near(projectile.previous_position.z, -4.0F)),
                      "The first movement segment must begin at the resolved actor origin.");
        CHECK_MESSAGE((near(projectile.position.x, 11.2F) && near(projectile.position.y, 3.0F) &&
                       near(projectile.position.z, -2.4F)),
                      "One 60 Hz tick must integrate direction times speed in world X/Z space.");
        CHECK_MESSAGE((projectile.lifetime_ticks_remaining == 9),
                      "The first simulated step must consume exactly one lifetime tick.");
    }
}

TEST_CASE("projectile expires after its exact authored lifetime") {
    ic2d::ProjectileSimulation projectiles;
    const ic2d::ProjectileSpawnedEvent spawn{
        .tick = 1,
        .projectile_id = 19,
        .actor = {88},
        .aim_direction = {1.0F, 0.0F},
        .speed = 60.0F,
        .lifetime_ticks = 3,
        .damage = 5.0F,
    };

    CHECK_MESSAGE((projectiles.spawn(spawn, {10.0F, 2.0F, 6.0F})),
                  "The lifetime fixture must accept its copied spawn.");
    projectiles.fixed_update(1, 1.0F / 60.0F);
    CHECK_MESSAGE((projectiles.snapshot().active.size() == 1 &&
                   projectiles.snapshot().active.front().lifetime_ticks_remaining == 2),
                  "A three-tick projectile must remain active after its first step.");
    projectiles.fixed_update(2, 1.0F / 60.0F);
    CHECK_MESSAGE((projectiles.snapshot().active.size() == 1 &&
                   projectiles.snapshot().active.front().lifetime_ticks_remaining == 1),
                  "A projectile must remain active through the tick before expiry.");
    CHECK_MESSAGE((projectiles.drain_expired_events().empty()),
                  "No expiration event may be emitted before the authored lifetime elapses.");

    projectiles.fixed_update(3, 1.0F / 60.0F);
    const ic2d::ProjectileSimulationSnapshot expired = projectiles.snapshot();
    CHECK_MESSAGE((expired.active.empty() && expired.total_expired == 1),
                  "The projectile must leave active state on its third simulated step.");
    const std::vector<ic2d::ProjectileExpiredEvent> events = projectiles.drain_expired_events();
    CHECK_MESSAGE((events.size() == 1),
                  "Exact lifetime expiry must emit one copied lifecycle event.");
    if (events.size() == 1) {
        CHECK_MESSAGE((events.front().tick == 3 && events.front().projectile_id == 19 &&
                       events.front().actor == ic2d::EntityUuid{88}),
                      "The expiration event must retain tick and stable ownership identity.");
        CHECK_MESSAGE((near(events.front().position.x, 13.0F) &&
                       near(events.front().position.y, 2.0F) &&
                       near(events.front().position.z, 6.0F)),
                      "Expiration must report the endpoint after exactly three movement steps.");
    }
    CHECK_MESSAGE((projectiles.drain_expired_events().empty()),
                  "Draining copied expiration events must be destructive.");
}

TEST_CASE("impact ignores owner then removes projectile on valid target") {
    ic2d::ProjectileSimulation projectiles;
    const ic2d::EntityUuid owner{301};
    CHECK_MESSAGE((projectiles.spawn(
                      {
                          .tick = 1,
                          .projectile_id = 44,
                          .actor = owner,
                          .aim_direction = {1.0F, 0.0F},
                          .speed = 60.0F,
                          .lifetime_ticks = 10,
                          .damage = 18.0F,
                      },
                      {0.0F, 4.0F, 0.0F})),
                  "The impact fixture must accept its projectile spawn.");
    projectiles.fixed_update(1, 1.0F / 60.0F);

    CHECK_MESSAGE((!projectiles.resolve_impact({
                      .tick = 1,
                      .projectile_id = 44,
                      .target = owner,
                      .position = {0.25F, 4.0F, 0.0F},
                      .normal = {-1.0F, 0.0F},
                      .tag = 10,
                  })),
                  "A projectile must reject an impact against its owning actor.");
    CHECK_MESSAGE(
        (projectiles.snapshot().active.size() == 1 && projectiles.drain_impact_events().empty()),
        "An ignored owner overlap must not remove the projectile or emit an impact.");

    const ic2d::EntityUuid target{302};
    CHECK_MESSAGE((projectiles.resolve_impact({
                      .tick = 1,
                      .projectile_id = 44,
                      .target = target,
                      .position = {0.75F, 4.0F, 0.0F},
                      .normal = {-1.0F, 0.0F},
                      .tag = 30,
                  })),
                  "A valid non-owner collision must resolve the active projectile.");
    CHECK_MESSAGE(
        (projectiles.snapshot().active.empty() && projectiles.snapshot().total_impacted == 1),
        "A resolved impact must remove the projectile and increment impact state once.");
    const std::vector<ic2d::ProjectileImpactEvent> events = projectiles.drain_impact_events();
    CHECK_MESSAGE((events.size() == 1),
                  "A resolved collision must emit exactly one copied impact event.");
    if (events.size() == 1) {
        CHECK_MESSAGE((events.front().tick == 1 && events.front().projectile_id == 44 &&
                       events.front().actor == owner && events.front().target == target),
                      "The impact event must preserve projectile, owner, and target identity.");
        CHECK_MESSAGE((events.front().tag == 30 && near(events.front().damage, 18.0F) &&
                       near(events.front().position.x, 0.75F) &&
                       near(events.front().normal.x, -1.0F)),
                      "The impact event must copy collision geometry, tag, and authored damage.");
    }
    CHECK_MESSAGE((!projectiles.resolve_impact({
                      .tick = 1,
                      .projectile_id = 44,
                      .target = target,
                  })),
                  "The same projectile identity must not resolve damage twice.");
}

} // namespace
