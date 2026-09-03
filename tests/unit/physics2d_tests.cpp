#include <doctest/doctest.h>

#include "ic2d/physics2d.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace {

[[nodiscard]] bool near(const float left, const float right, const float tolerance = 0.05F) {
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] const ic2d::PhysicsBodySnapshot* find_body(const ic2d::PhysicsStepResult& result,
                                                         const ic2d::PhysicsBodyId body) {
    const auto found = std::ranges::find(result.bodies, body, &ic2d::PhysicsBodySnapshot::body);
    return found == result.bodies.end() ? nullptr : &*found;
}

TEST_CASE("units and dynamic motion") {
    ic2d::PhysicsWorld physics{{.pixels_per_metre = 32.0F, .enable_sleep = false}};
    const auto body = physics.create_box({
        .motion = ic2d::PhysicsMotionType::dynamic_body,
        .center = {16.0F, 24.0F},
        .half_extents = {8.0F, 8.0F},
        .linear_velocity = {32.0F, 0.0F},
        .gravity_scale = 0.0F,
        .fixed_rotation = true,
    });

    const auto result = physics.step(0.5F);
    const auto* snapshot = find_body(result, body);
    CHECK_MESSAGE((snapshot != nullptr), "A live body must appear in the step snapshot.");
    CHECK_MESSAGE(
        (snapshot != nullptr && near(snapshot->center.x, 32.0F) && near(snapshot->center.y, 24.0F)),
        "Pixel-to-metre conversion must preserve the public movement units.");
    CHECK_MESSAGE((snapshot != nullptr && near(snapshot->linear_velocity.x, 32.0F)),
                  "Velocity snapshots must be converted back to pixels per second.");
}

TEST_CASE("contact and sensor events") {
    constexpr std::uint64_t world_layer = 1;
    constexpr std::uint64_t actor_layer = 2;
    constexpr std::uint64_t sensor_layer = 4;
    ic2d::PhysicsWorld physics{{.pixels_per_metre = 32.0F, .enable_sleep = false}};
    const auto wall = physics.create_box({
        .motion = ic2d::PhysicsMotionType::static_body,
        .center = {80.0F, 0.0F},
        .half_extents = {8.0F, 24.0F},
        .category_bits = world_layer,
        .mask_bits = actor_layer,
        .tag = 10,
    });
    const auto sensor = physics.create_box({
        .motion = ic2d::PhysicsMotionType::static_body,
        .center = {32.0F, 0.0F},
        .half_extents = {8.0F, 24.0F},
        .category_bits = sensor_layer,
        .mask_bits = actor_layer,
        .tag = 77,
        .sensor = true,
    });
    const auto actor = physics.create_box({
        .motion = ic2d::PhysicsMotionType::dynamic_body,
        .center = {0.0F, 0.0F},
        .half_extents = {4.0F, 4.0F},
        .linear_velocity = {64.0F, 0.0F},
        .gravity_scale = 0.0F,
        .category_bits = actor_layer,
        .mask_bits = world_layer | sensor_layer,
        .tag = 20,
        .fixed_rotation = true,
    });

    bool trigger_begin = false;
    bool contact_begin = false;
    for (int tick = 0; tick < 120; ++tick) {
        const auto result = physics.step(1.0F / 60.0F);
        for (const ic2d::PhysicsEvent& event : result.events) {
            trigger_begin = trigger_begin ||
                            (event.kind == ic2d::PhysicsEventKind::trigger_begin &&
                             event.body_a == sensor && event.body_b == actor && event.tag_a == 77);
            contact_begin = contact_begin || (event.kind == ic2d::PhysicsEventKind::contact_begin &&
                                              ((event.body_a == wall && event.body_b == actor) ||
                                               (event.body_a == actor && event.body_b == wall)));
        }
    }
    CHECK_MESSAGE((trigger_begin), "Sensor overlap must leave as an engine-owned trigger event.");
    CHECK_MESSAGE((contact_begin), "Solid contact must leave as an engine-owned contact event.");
}

TEST_CASE("kinematic target and handle generation") {
    ic2d::PhysicsWorld physics{{.pixels_per_metre = 16.0F, .enable_sleep = false}};
    const auto first = physics.create_box({
        .motion = ic2d::PhysicsMotionType::kinematic_body,
        .half_extents = {4.0F, 4.0F},
        .gravity_scale = 0.0F,
    });
    CHECK_MESSAGE((physics.set_kinematic_target(first, {30.0F, 12.0F}, 1.0F / 60.0F)),
                  "A kinematic body must accept a target.");
    const auto result = physics.step(1.0F / 60.0F);
    const auto* snapshot = find_body(result, first);
    CHECK_MESSAGE(
        (snapshot != nullptr && near(snapshot->center.x, 30.0F) && near(snapshot->center.y, 12.0F)),
        "Kinematic target movement must be visible after one fixed step.");

    CHECK_MESSAGE((physics.destroy_body(first)), "Destroying a live body must succeed.");
    CHECK_MESSAGE((!physics.destroy_body(first)), "A stale handle must not destroy another body.");
    const auto second = physics.create_box({
        .motion = ic2d::PhysicsMotionType::dynamic_body,
        .half_extents = {4.0F, 4.0F},
        .gravity_scale = 0.0F,
    });
    CHECK_MESSAGE((second.slot == first.slot && second.generation != first.generation),
                  "Reused slots must advance their public generation.");
    CHECK_MESSAGE((!physics.set_linear_velocity(first, {1.0F, 0.0F})),
                  "A stale handle must not mutate a replacement body.");
}

TEST_CASE("kinematic body stops when target stops") {
    ic2d::PhysicsWorld physics{{.pixels_per_metre = 32.0F, .enable_sleep = false}};
    const auto player = physics.create_box({
        .motion = ic2d::PhysicsMotionType::kinematic_body,
        .half_extents = {9.0F, 5.0F},
        .gravity_scale = 0.0F,
        .fixed_rotation = true,
    });

    constexpr float fixed_step = 1.0F / 60.0F;
    CHECK_MESSAGE((physics.set_kinematic_target(player, {2.0F, 0.0F}, fixed_step)),
                  "A movement tick must accept its kinematic target.");
    const auto moving_step = physics.step(fixed_step);
    const auto* moving = find_body(moving_step, player);
    CHECK_MESSAGE((moving != nullptr && moving->linear_velocity.x > 0.0F),
                  "The movement tick must produce positive player velocity.");

    CHECK_MESSAGE((physics.set_kinematic_target(player, {2.0F, 0.0F}, fixed_step)),
                  "A released movement tick must accept the stationary target.");
    const auto released_step = physics.step(fixed_step);
    const auto* released = find_body(released_step, player);
    CHECK_MESSAGE((released != nullptr && near(released->center.x, 2.0F) &&
                   near(released->linear_velocity.x, 0.0F, 0.001F)),
                  "Releasing movement must stop the kinematic body on the next fixed tick.");

    const auto idle_step = physics.step(fixed_step);
    const auto* idle = find_body(idle_step, player);
    CHECK_MESSAGE((idle != nullptr && near(idle->center.x, 2.0F) &&
                   near(idle->linear_velocity.x, 0.0F, 0.001F)),
                  "The stopped kinematic body must remain still on later fixed ticks.");
}

TEST_CASE("segment query returns the nearest solid hit") {
    ic2d::PhysicsWorld physics{{.pixels_per_metre = 20.0F}};
    const ic2d::PhysicsBodyId nearest = physics.create_box({
        .center = {30.0F, 0.0F},
        .half_extents = {5.0F, 8.0F},
        .tag = 41,
    });
    static_cast<void>(physics.create_box({
        .center = {60.0F, 0.0F},
        .half_extents = {5.0F, 8.0F},
        .tag = 82,
    }));

    const std::optional<ic2d::PhysicsSegmentHit> hit = physics.cast_segment({
        .start = {0.0F, 0.0F},
        .end = {100.0F, 0.0F},
    });
    CHECK_MESSAGE((hit.has_value()), "A segment crossing solid boxes must report a hit.");
    if (hit) {
        CHECK_MESSAGE((hit->body == nearest && hit->tag == 41),
                      "The segment query must return the nearest body and copied tag.");
        CHECK_MESSAGE((near(hit->point.x, 25.0F) && near(hit->point.y, 0.0F) &&
                       near(hit->fraction, 0.25F, 0.001F)),
                      "The hit point and fraction must use engine pixel coordinates.");
        CHECK_MESSAGE((near(hit->normal.x, -1.0F, 0.001F) && near(hit->normal.y, 0.0F, 0.001F)),
                      "The segment hit must copy its outward surface normal.");
    }
}

TEST_CASE("segment query ignores owner and sensors") {
    ic2d::PhysicsWorld physics{{.pixels_per_metre = 20.0F}};
    const ic2d::PhysicsBodyId owner = physics.create_box({
        .center = {10.0F, 0.0F},
        .half_extents = {4.0F, 6.0F},
        .tag = 10,
    });
    static_cast<void>(physics.create_box({
        .center = {20.0F, 0.0F},
        .half_extents = {3.0F, 6.0F},
        .tag = 20,
        .sensor = true,
    }));
    const ic2d::PhysicsBodyId target = physics.create_box({
        .center = {30.0F, 0.0F},
        .half_extents = {5.0F, 6.0F},
        .tag = 30,
    });

    const std::optional<ic2d::PhysicsSegmentHit> hit = physics.cast_segment({
        .start = {0.0F, 0.0F},
        .end = {50.0F, 0.0F},
        .ignored_body = owner,
    });
    CHECK_MESSAGE(
        (hit.has_value() && hit->body == target && hit->tag == 30 && near(hit->point.x, 25.0F)),
        "Segment filtering must skip the owning body and non-solid sensors.");
}

TEST_CASE("invalid definitions fail early") {
    bool rejected = false;
    try {
        ic2d::PhysicsWorld physics;
        static_cast<void>(physics.create_box({.half_extents = {0.0F, 1.0F}}));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK_MESSAGE((rejected), "Invalid body geometry must fail before entering Box2D.");
}

} // namespace
