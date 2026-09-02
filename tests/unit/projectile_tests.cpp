#include "ic2d/projectiles.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] bool near(const float left, const float right) {
    return std::abs(left - right) < 0.0001F;
}

void test_spawn_is_buffered_and_moves_one_fixed_tick() {
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

    expect(projectiles.spawn(spawn, {10.0F, 3.0F, -4.0F}),
           "A valid copied Combat spawn must enter the projectile buffer.");
    expect(projectiles.snapshot().active.empty() &&
               projectiles.snapshot().pending_spawn_count == 1,
           "Render-rate submission must not mutate authoritative projectile positions.");

    projectiles.fixed_update(1, 1.0F / 60.0F);
    const ic2d::ProjectileSimulationSnapshot snapshot = projectiles.snapshot();
    expect(snapshot.tick == 1 && snapshot.total_spawned == 1 &&
               snapshot.pending_spawn_count == 0 && snapshot.active.size() == 1,
           "The matching fixed tick must activate exactly one buffered projectile.");
    if (snapshot.active.size() == 1) {
        const ic2d::ProjectileStateSnapshot& projectile = snapshot.active.front();
        expect(projectile.projectile_id == 7 && projectile.actor == ic2d::EntityUuid{42},
               "Projectile movement must retain stable projectile and actor identity.");
        expect(near(projectile.previous_position.x, 10.0F) &&
                   near(projectile.previous_position.y, 3.0F) &&
                   near(projectile.previous_position.z, -4.0F),
               "The first movement segment must begin at the resolved actor origin.");
        expect(near(projectile.position.x, 11.2F) &&
                   near(projectile.position.y, 3.0F) &&
                   near(projectile.position.z, -2.4F),
               "One 60 Hz tick must integrate direction times speed in world X/Z space.");
        expect(projectile.lifetime_ticks_remaining == 9,
               "The first simulated step must consume exactly one lifetime tick.");
    }
}

void test_projectile_expires_after_its_exact_authored_lifetime() {
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

    expect(projectiles.spawn(spawn, {10.0F, 2.0F, 6.0F}),
           "The lifetime fixture must accept its copied spawn.");
    projectiles.fixed_update(1, 1.0F / 60.0F);
    expect(projectiles.snapshot().active.size() == 1 &&
               projectiles.snapshot().active.front().lifetime_ticks_remaining == 2,
           "A three-tick projectile must remain active after its first step.");
    projectiles.fixed_update(2, 1.0F / 60.0F);
    expect(projectiles.snapshot().active.size() == 1 &&
               projectiles.snapshot().active.front().lifetime_ticks_remaining == 1,
           "A projectile must remain active through the tick before expiry.");
    expect(projectiles.drain_expired_events().empty(),
           "No expiration event may be emitted before the authored lifetime elapses.");

    projectiles.fixed_update(3, 1.0F / 60.0F);
    const ic2d::ProjectileSimulationSnapshot expired = projectiles.snapshot();
    expect(expired.active.empty() && expired.total_expired == 1,
           "The projectile must leave active state on its third simulated step.");
    const std::vector<ic2d::ProjectileExpiredEvent> events =
        projectiles.drain_expired_events();
    expect(events.size() == 1,
           "Exact lifetime expiry must emit one copied lifecycle event.");
    if (events.size() == 1) {
        expect(events.front().tick == 3 && events.front().projectile_id == 19 &&
                   events.front().actor == ic2d::EntityUuid{88},
               "The expiration event must retain tick and stable ownership identity.");
        expect(near(events.front().position.x, 13.0F) &&
                   near(events.front().position.y, 2.0F) &&
                   near(events.front().position.z, 6.0F),
               "Expiration must report the endpoint after exactly three movement steps.");
    }
    expect(projectiles.drain_expired_events().empty(),
           "Draining copied expiration events must be destructive.");
}

void test_impact_ignores_owner_then_removes_projectile_on_valid_target() {
    ic2d::ProjectileSimulation projectiles;
    const ic2d::EntityUuid owner{301};
    expect(projectiles.spawn({
                                 .tick = 1,
                                 .projectile_id = 44,
                                 .actor = owner,
                                 .aim_direction = {1.0F, 0.0F},
                                 .speed = 60.0F,
                                 .lifetime_ticks = 10,
                                 .damage = 18.0F,
                             },
                             {0.0F, 4.0F, 0.0F}),
           "The impact fixture must accept its projectile spawn.");
    projectiles.fixed_update(1, 1.0F / 60.0F);

    expect(!projectiles.resolve_impact({
               .tick = 1,
               .projectile_id = 44,
               .target = owner,
               .position = {0.25F, 4.0F, 0.0F},
               .normal = {-1.0F, 0.0F},
               .tag = 10,
           }),
           "A projectile must reject an impact against its owning actor.");
    expect(projectiles.snapshot().active.size() == 1 &&
               projectiles.drain_impact_events().empty(),
           "An ignored owner overlap must not remove the projectile or emit an impact.");

    const ic2d::EntityUuid target{302};
    expect(projectiles.resolve_impact({
               .tick = 1,
               .projectile_id = 44,
               .target = target,
               .position = {0.75F, 4.0F, 0.0F},
               .normal = {-1.0F, 0.0F},
               .tag = 30,
           }),
           "A valid non-owner collision must resolve the active projectile.");
    expect(projectiles.snapshot().active.empty() &&
               projectiles.snapshot().total_impacted == 1,
           "A resolved impact must remove the projectile and increment impact state once.");
    const std::vector<ic2d::ProjectileImpactEvent> events =
        projectiles.drain_impact_events();
    expect(events.size() == 1,
           "A resolved collision must emit exactly one copied impact event.");
    if (events.size() == 1) {
        expect(events.front().tick == 1 && events.front().projectile_id == 44 &&
                   events.front().actor == owner && events.front().target == target,
               "The impact event must preserve projectile, owner, and target identity.");
        expect(events.front().tag == 30 && near(events.front().damage, 18.0F) &&
                   near(events.front().position.x, 0.75F) &&
                   near(events.front().normal.x, -1.0F),
               "The impact event must copy collision geometry, tag, and authored damage.");
    }
    expect(!projectiles.resolve_impact({
               .tick = 1,
               .projectile_id = 44,
               .target = target,
           }),
           "The same projectile identity must not resolve damage twice.");
}

} // namespace

int main() {
    test_spawn_is_buffered_and_moves_one_fixed_tick();
    test_projectile_expires_after_its_exact_authored_lifetime();
    test_impact_ignores_owner_then_removes_projectile_on_valid_target();

    if (failures == 0) {
        std::cout << "Projectile simulation tests passed.\n";
        return 0;
    }
    std::cerr << failures << " projectile assertion(s) failed.\n";
    return 1;
}
