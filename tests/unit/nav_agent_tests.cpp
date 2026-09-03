#include <doctest/doctest.h>

#include "ic2d/nav_agent.hpp"

#include <cmath>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] bool near(const float left, const float right) {
    return std::abs(left - right) < 0.001F;
}

[[nodiscard]] ic2d::NavGrid detour_grid() {
    return ic2d::NavGrid{
        {
            .walkable_bounds = {0.0F, 0.0F, 60.0F, 60.0F},
            .max_step_height = 0.0F,
            .areas = {{
                .bounds = {20.0F, 20.0F, 20.0F, 20.0F},
                .kind = ic2d::GroundAreaKind::solid,
            }},
        },
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };
}

[[nodiscard]] ic2d::NavAgentRequest request(const ic2d::Vec2 actor_position,
                                            const ic2d::Vec2 target_position,
                                            const bool active = true) {
    return {
        .actor = {200},
        .target = {100},
        .actor_position = actor_position,
        .target_position = target_position,
        .active = active,
    };
}

TEST_CASE("follows cell centres around a hard block") {
    const ic2d::NavGrid grid = detour_grid();
    ic2d::NavAgentSystem navigation{{.repath_interval_ticks = 30, .waypoint_tolerance = 1.0F}};
    CHECK_MESSAGE((navigation.register_agent({200})),
                  "A valid path-following actor must register.");

    std::vector<ic2d::NavAgentMotion> motion =
        navigation.fixed_update(1, grid, {request({10.0F, 30.0F}, {50.0F, 30.0F})});
    CHECK_MESSAGE((motion.size() == 1 && motion.front().repathed &&
                   motion.front().path_status == ic2d::NavPathStatus::found),
                  "The first active request must search immediately.");
    CHECK_MESSAGE((near(motion.front().movement_direction.x, 0.0F) &&
                   near(motion.front().movement_direction.y, -1.0F)),
                  "The first movement must turn away from the blocked direct line.");
    const std::vector<ic2d::NavCell> expected{
        {0, 1}, {0, 0}, {1, 0}, {2, 0}, {2, 1},
    };
    CHECK_MESSAGE((navigation.snapshot().actors.front().path == expected),
                  "The agent must retain A-star's stable no-corner-cutting detour.");

    motion = navigation.fixed_update(2, grid, {request({10.0F, 10.0F}, {50.0F, 30.0F})});
    // Arriving at the corner cell advances one waypoint, and smoothing then
    // skips straight to the far side of the block, which is the last waypoint
    // still in sight. Chasing the intermediate centre would be a detour the
    // agent can see is unnecessary.
    CHECK_MESSAGE((near(motion.front().movement_direction.x, 1.0F) &&
                   near(motion.front().movement_direction.y, 0.0F)),
                  "The agent must head along the clear row toward the furthest visible waypoint.");
    CHECK_MESSAGE((navigation.snapshot().actors.front().waypoint_index == 3),
                  "Smoothing must skip the intermediate cell centre it can see past.");
    CHECK_MESSAGE((navigation.snapshot().actors.front().waypoint_advance_count == 2),
                  "Both the arrival and the smoothing skip must be counted.");
    motion = navigation.fixed_update(3, grid, {request({30.0F, 10.0F}, {50.0F, 30.0F})});
    CHECK_MESSAGE((near(motion.front().movement_direction.x, 1.0F)),
                  "The route must continue along the clear side of the block.");
    motion = navigation.fixed_update(4, grid, {request({50.0F, 10.0F}, {50.0F, 30.0F})});
    CHECK_MESSAGE((near(motion.front().movement_direction.x, 0.0F) &&
                   near(motion.front().movement_direction.y, 1.0F)),
                  "The final routed leg must turn back toward the target cell.");
}

TEST_CASE("repaths only on bounded or meaningful triggers") {
    const ic2d::NavGrid grid = detour_grid();
    ic2d::NavAgentSystem navigation{{.repath_interval_ticks = 3, .waypoint_tolerance = 1.0F}};
    CHECK_MESSAGE((navigation.register_agent({200})), "The repath fixture must register.");
    static_cast<void>(navigation.fixed_update(1, grid, {request({10.0F, 30.0F}, {50.0F, 30.0F})}));
    const auto second = navigation.fixed_update(2, grid, {request({10.0F, 25.0F}, {49.0F, 31.0F})});
    const auto third = navigation.fixed_update(3, grid, {request({10.0F, 20.0F}, {48.0F, 32.0F})});
    CHECK_MESSAGE((!second.front().repathed && !third.front().repathed &&
                   navigation.snapshot().total_search_count == 1),
                  "Movement inside the same route and target cell must reuse one search.");

    const auto interval =
        navigation.fixed_update(4, grid, {request({10.0F, 15.0F}, {48.0F, 32.0F})});
    CHECK_MESSAGE((interval.front().repathed && navigation.snapshot().total_search_count == 2),
                  "An unchanged route must refresh on the exact bounded interval tick.");

    const auto changed_goal =
        navigation.fixed_update(5, grid, {request({10.0F, 15.0F}, {50.0F, 50.0F})});
    CHECK_MESSAGE(
        (changed_goal.front().repathed && navigation.snapshot().total_search_count == 3),
        "A target-cell change must repath immediately rather than waiting for the interval.");
}

TEST_CASE("route loss failure inactive and reset are explicit") {
    const ic2d::NavGrid grid = detour_grid();
    ic2d::NavAgentSystem navigation{{.repath_interval_ticks = 30, .waypoint_tolerance = 1.0F}};
    CHECK_MESSAGE((navigation.register_agent({200})), "The lifecycle fixture must register.");
    static_cast<void>(navigation.fixed_update(1, grid, {request({10.0F, 30.0F}, {50.0F, 30.0F})}));

    const auto off_route =
        navigation.fixed_update(2, grid, {request({10.0F, 50.0F}, {50.0F, 30.0F})});
    CHECK_MESSAGE((off_route.front().repathed),
                  "Leaving every remaining route cell must force an immediate recovery search.");

    const auto blocked_goal =
        navigation.fixed_update(3, grid, {request({10.0F, 50.0F}, {30.0F, 30.0F})});
    CHECK_MESSAGE((blocked_goal.front().path_status == ic2d::NavPathStatus::goal_blocked &&
                   near(blocked_goal.front().movement_direction.x, 0.0F) &&
                   near(blocked_goal.front().movement_direction.y, 0.0F)),
                  "A blocked goal must stop instead of falling back to wall-pushing pursuit.");

    const auto bounded_failure =
        navigation.fixed_update(4, grid, {request({10.0F, 50.0F}, {30.0F, 30.0F})});
    CHECK_MESSAGE(
        (!bounded_failure.front().repathed && navigation.snapshot().total_search_count == 3),
        "A failed unchanged route must wait for its bounded retry tick.");

    const auto inactive =
        navigation.fixed_update(5, grid, {request({10.0F, 50.0F}, {50.0F, 30.0F}, false)});
    CHECK_MESSAGE(
        (!navigation.snapshot().actors.front().active && inactive.front().path_cell_count == 0),
        "An inactive request must clear transient route state and return no motion.");

    navigation.reset();
    const ic2d::NavAgentSnapshot reset = navigation.snapshot();
    CHECK_MESSAGE((reset.tick == 0 && reset.total_search_count == 0 && reset.actors.size() == 1 &&
                   !reset.actors.front().active && reset.actors.front().search_count == 0),
                  "Reset must preserve registrations while clearing every route counter and path.");
}

TEST_CASE("request validation is atomic") {
    const ic2d::NavGrid grid = detour_grid();
    ic2d::NavAgentSystem navigation;
    CHECK_MESSAGE((navigation.register_agent({200})), "The validation fixture must register.");
    bool rejected = false;
    try {
        static_cast<void>(navigation.fixed_update(1, grid, {}));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK_MESSAGE((rejected && navigation.snapshot().tick == 0),
                  "A missing request must fail before mutating navigation tick state.");
}

} // namespace

// The point of smoothing: across open ground an agent must walk the straight
// line to its goal rather than the chain of cell centres A-star returned.
TEST_CASE("open ground is walked straight rather than centre to centre") {
    const ic2d::NavGrid grid{
        {.walkable_bounds = {0.0F, 0.0F, 200.0F, 200.0F}, .max_step_height = 0.0F},
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };
    ic2d::NavAgentSystem navigation{{.repath_interval_ticks = 30, .waypoint_tolerance = 1.0F}};
    CHECK_MESSAGE((navigation.register_agent({200})), "The open-ground fixture must register.");

    // A target that is not on a cell centre and not on a diagonal: centre
    // chasing would visibly zig-zag toward it.
    const ic2d::Vec2 start{10.0F, 10.0F};
    const ic2d::Vec2 target{147.0F, 63.0F};
    const auto motion = navigation.fixed_update(1, grid, {request(start, target)});
    CHECK_MESSAGE((motion.front().path_status == ic2d::NavPathStatus::found),
                  "The open route must be found.");

    const float dx = target.x - start.x;
    const float dz = target.y - start.y;
    const float length = std::sqrt(dx * dx + dz * dz);
    CHECK_MESSAGE((near(motion.front().movement_direction.x, dx / length) &&
                   near(motion.front().movement_direction.y, dz / length)),
                  "With the target in plain sight the agent must steer straight at it.");

    // And it must keep steering straight, not re-acquire a cell centre as it
    // moves along that line.
    const ic2d::Vec2 midway{78.5F, 36.5F};
    const auto later = navigation.fixed_update(2, grid, {request(midway, target)});
    const float mid_dx = target.x - midway.x;
    const float mid_dz = target.y - midway.y;
    const float mid_length = std::sqrt(mid_dx * mid_dx + mid_dz * mid_dz);
    CHECK_MESSAGE((near(later.front().movement_direction.x, mid_dx / mid_length) &&
                   near(later.front().movement_direction.y, mid_dz / mid_length)),
                  "A visible target must stay the destination for the whole approach.");
}
