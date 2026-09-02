#include "ic2d/nav_agent.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

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

[[nodiscard]] ic2d::NavAgentRequest request(
    const ic2d::Vec2 actor_position,
    const ic2d::Vec2 target_position,
    const bool active = true
) {
    return {
        .actor = {200},
        .target = {100},
        .actor_position = actor_position,
        .target_position = target_position,
        .active = active,
    };
}

void test_follows_cell_centres_around_a_hard_block() {
    const ic2d::NavGrid grid = detour_grid();
    ic2d::NavAgentSystem navigation{{.repath_interval_ticks = 30,
                                     .waypoint_tolerance = 1.0F}};
    expect(navigation.register_agent({200}), "A valid path-following actor must register.");

    std::vector<ic2d::NavAgentMotion> motion =
        navigation.fixed_update(1, grid, {request({10.0F, 30.0F}, {50.0F, 30.0F})});
    expect(motion.size() == 1 && motion.front().repathed &&
               motion.front().path_status == ic2d::NavPathStatus::found,
           "The first active request must search immediately.");
    expect(near(motion.front().movement_direction.x, 0.0F) &&
               near(motion.front().movement_direction.y, -1.0F),
           "The first movement must turn away from the blocked direct line.");
    const std::vector<ic2d::NavCell> expected{
        {0, 1}, {0, 0}, {1, 0}, {2, 0}, {2, 1},
    };
    expect(navigation.snapshot().actors.front().path == expected,
           "The agent must retain A-star's stable no-corner-cutting detour.");

    motion = navigation.fixed_update(
        2, grid, {request({10.0F, 10.0F}, {50.0F, 30.0F})});
    expect(near(motion.front().movement_direction.x, 1.0F) &&
               near(motion.front().movement_direction.y, 0.0F) &&
               navigation.snapshot().actors.front().waypoint_advance_count == 1,
           "Reaching a waypoint centre must advance exactly once to the next cell.");
    motion = navigation.fixed_update(
        3, grid, {request({30.0F, 10.0F}, {50.0F, 30.0F})});
    expect(near(motion.front().movement_direction.x, 1.0F),
           "The route must continue along the clear side of the block.");
    motion = navigation.fixed_update(
        4, grid, {request({50.0F, 10.0F}, {50.0F, 30.0F})});
    expect(near(motion.front().movement_direction.x, 0.0F) &&
               near(motion.front().movement_direction.y, 1.0F),
           "The final routed leg must turn back toward the target cell.");
}

void test_repaths_only_on_bounded_or_meaningful_triggers() {
    const ic2d::NavGrid grid = detour_grid();
    ic2d::NavAgentSystem navigation{{.repath_interval_ticks = 3,
                                     .waypoint_tolerance = 1.0F}};
    expect(navigation.register_agent({200}), "The repath fixture must register.");
    static_cast<void>(navigation.fixed_update(
        1, grid, {request({10.0F, 30.0F}, {50.0F, 30.0F})}));
    const auto second = navigation.fixed_update(
        2, grid, {request({10.0F, 25.0F}, {49.0F, 31.0F})});
    const auto third = navigation.fixed_update(
        3, grid, {request({10.0F, 20.0F}, {48.0F, 32.0F})});
    expect(!second.front().repathed && !third.front().repathed &&
               navigation.snapshot().total_search_count == 1,
           "Movement inside the same route and target cell must reuse one search.");

    const auto interval = navigation.fixed_update(
        4, grid, {request({10.0F, 15.0F}, {48.0F, 32.0F})});
    expect(interval.front().repathed && navigation.snapshot().total_search_count == 2,
           "An unchanged route must refresh on the exact bounded interval tick.");

    const auto changed_goal = navigation.fixed_update(
        5, grid, {request({10.0F, 15.0F}, {50.0F, 50.0F})});
    expect(changed_goal.front().repathed &&
               navigation.snapshot().total_search_count == 3,
           "A target-cell change must repath immediately rather than waiting for the interval.");
}

void test_route_loss_failure_inactive_and_reset_are_explicit() {
    const ic2d::NavGrid grid = detour_grid();
    ic2d::NavAgentSystem navigation{{.repath_interval_ticks = 30,
                                     .waypoint_tolerance = 1.0F}};
    expect(navigation.register_agent({200}), "The lifecycle fixture must register.");
    static_cast<void>(navigation.fixed_update(
        1, grid, {request({10.0F, 30.0F}, {50.0F, 30.0F})}));

    const auto off_route = navigation.fixed_update(
        2, grid, {request({10.0F, 50.0F}, {50.0F, 30.0F})});
    expect(off_route.front().repathed,
           "Leaving every remaining route cell must force an immediate recovery search.");

    const auto blocked_goal = navigation.fixed_update(
        3, grid, {request({10.0F, 50.0F}, {30.0F, 30.0F})});
    expect(blocked_goal.front().path_status == ic2d::NavPathStatus::goal_blocked &&
               near(blocked_goal.front().movement_direction.x, 0.0F) &&
               near(blocked_goal.front().movement_direction.y, 0.0F),
           "A blocked goal must stop instead of falling back to wall-pushing pursuit.");

    const auto bounded_failure = navigation.fixed_update(
        4, grid, {request({10.0F, 50.0F}, {30.0F, 30.0F})});
    expect(!bounded_failure.front().repathed &&
               navigation.snapshot().total_search_count == 3,
           "A failed unchanged route must wait for its bounded retry tick.");

    const auto inactive = navigation.fixed_update(
        5, grid, {request({10.0F, 50.0F}, {50.0F, 30.0F}, false)});
    expect(!navigation.snapshot().actors.front().active &&
               inactive.front().path_cell_count == 0,
           "An inactive request must clear transient route state and return no motion.");

    navigation.reset();
    const ic2d::NavAgentSnapshot reset = navigation.snapshot();
    expect(reset.tick == 0 && reset.total_search_count == 0 &&
               reset.actors.size() == 1 && !reset.actors.front().active &&
               reset.actors.front().search_count == 0,
           "Reset must preserve registrations while clearing every route counter and path.");
}

void test_request_validation_is_atomic() {
    const ic2d::NavGrid grid = detour_grid();
    ic2d::NavAgentSystem navigation;
    expect(navigation.register_agent({200}), "The validation fixture must register.");
    bool rejected = false;
    try {
        static_cast<void>(navigation.fixed_update(1, grid, {}));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected && navigation.snapshot().tick == 0,
           "A missing request must fail before mutating navigation tick state.");
}

} // namespace

int main() {
    test_follows_cell_centres_around_a_hard_block();
    test_repaths_only_on_bounded_or_meaningful_triggers();
    test_route_loss_failure_inactive_and_reset_are_explicit();
    test_request_validation_is_atomic();
    if (failures == 0) {
        std::cout << "Nav-agent path-following tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
