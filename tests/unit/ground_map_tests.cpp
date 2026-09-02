#include "ic2d/ground_map.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
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
    return std::abs(left - right) < 0.001F;
}

[[nodiscard]] ic2d::GroundMap make_map() {
    return ic2d::GroundMap{{
        .walkable_bounds = {0.0F, 0.0F, 100.0F, 100.0F},
        .max_step_height = 16.0F,
        .areas = {
            {.bounds = {40.0F, 40.0F, 20.0F, 20.0F}, .kind = ic2d::GroundAreaKind::solid},
            {.bounds = {60.0F, 0.0F, 40.0F, 40.0F},
             .kind = ic2d::GroundAreaKind::elevation,
             .elevation = 16.0F},
            {.bounds = {60.0F, 60.0F, 30.0F, 30.0F},
             .kind = ic2d::GroundAreaKind::elevation,
             .elevation = 40.0F},
            {.bounds = {10.0F, 70.0F, 20.0F, 20.0F},
             .kind = ic2d::GroundAreaKind::trigger,
             .tag = 7},
        },
    }};
}

void test_free_motion_and_world_bounds() {
    const auto map = make_map();
    const auto free = map.move({10.0F, 0.0F, 10.0F}, {20.0F, 30.0F}, {2.0F, 2.0F});
    expect(near(free.position.x, 20.0F) && near(free.position.z, 30.0F),
           "Free motion must reach the desired X/Z position.");

    const auto clamped = map.move({10.0F, 0.0F, 10.0F}, {-20.0F, 120.0F}, {2.0F, 2.0F});
    expect(near(clamped.position.x, 2.0F) && near(clamped.position.z, 98.0F),
           "Actor footprint must stay inside walkable bounds.");
    expect(clamped.blocked_x && clamped.blocked_z,
           "World-bound clamping must report both blocked axes.");
}

void test_solid_collision_slides_by_axis() {
    const auto map = make_map();
    const auto result = map.move({20.0F, 0.0F, 50.0F}, {50.0F, 70.0F}, {2.0F, 2.0F});
    expect(near(result.position.x, 38.0F) && near(result.position.z, 70.0F),
           "A blocked X move must advance to contact and still permit free Z movement.");
    expect(result.blocked_x && !result.blocked_z,
           "Axis-separated collision must report only the blocked axis.");
}

void test_fast_motion_cannot_tunnel_through_a_solid() {
    const auto map = make_map();
    const auto forward = map.move(
        {10.0F, 0.0F, 50.0F}, {90.0F, 50.0F}, {2.0F, 2.0F});
    expect(near(forward.position.x, 38.0F) && forward.blocked_x,
           "Fast positive movement must stop at the near wall face instead of tunnelling.");

    const auto reverse = map.move(
        {90.0F, 0.0F, 50.0F}, {10.0F, 50.0F}, {2.0F, 2.0F});
    expect(near(reverse.position.x, 62.0F) && reverse.blocked_x,
           "Fast negative movement must stop at the far wall face instead of tunnelling.");
}

void test_discrete_elevation_and_step_limit() {
    const auto map = make_map();
    const auto step = map.move({50.0F, 0.0F, 20.0F}, {70.0F, 20.0F}, {2.0F, 2.0F});
    expect(near(step.position.x, 70.0F) && near(step.position.y, 16.0F),
           "An elevation within max step height must update World Y.");

    const auto too_high = map.move({50.0F, 0.0F, 70.0F}, {70.0F, 70.0F}, {2.0F, 2.0F});
    expect(near(too_high.position.x, 50.0F) && too_high.blocked_x,
           "An elevation above max step height must behave as a blocker.");
}

void test_trigger_is_reported_without_blocking() {
    const auto map = make_map();
    const auto result = map.move({5.0F, 0.0F, 75.0F}, {15.0F, 75.0F}, {2.0F, 2.0F});
    expect(result.trigger_tag && *result.trigger_tag == 7,
           "Overlapping a trigger footprint must return its engine-owned tag.");
    expect(!result.blocked_x && !result.blocked_z,
           "Triggers must not block movement.");
}

void test_invalid_definition_is_rejected() {
    bool threw = false;
    try {
        static_cast<void>(ic2d::GroundMap{{.walkable_bounds = {0.0F, 0.0F, 0.0F, 10.0F}}});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "A ground map with empty bounds must fail immediately.");
}

} // namespace

int main() {
    test_free_motion_and_world_bounds();
    test_solid_collision_slides_by_axis();
    test_fast_motion_cannot_tunnel_through_a_solid();
    test_discrete_elevation_and_step_limit();
    test_trigger_is_reported_without_blocking();
    test_invalid_definition_is_rejected();

    if (failures == 0) {
        std::cout << "All GroundMap tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
