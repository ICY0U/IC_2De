#include <doctest/doctest.h>

#include "ic2d/flow_field.hpp"
#include "ic2d/nav_pathfinding.hpp"

#include <cmath>
#include <string_view>

namespace {

[[nodiscard]] bool near(const float left, const float right) {
    return std::abs(left - right) < 0.001F;
}

[[nodiscard]] bool unit_length(const ic2d::Vec2 value) {
    return std::abs(std::sqrt(value.x * value.x + value.y * value.y) - 1.0F) < 0.001F;
}

[[nodiscard]] ic2d::NavGrid open_grid() {
    return ic2d::NavGrid{
        {
            .walkable_bounds = {0.0F, 0.0F, 100.0F, 100.0F},
            .max_step_height = 0.0F,
            .areas = {},
        },
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };
}

// A wall across the middle with one gap, so the only route is through it.
[[nodiscard]] ic2d::NavGrid gap_grid() {
    return ic2d::NavGrid{
        {
            .walkable_bounds = {0.0F, 0.0F, 100.0F, 100.0F},
            .max_step_height = 0.0F,
            .areas =
                {
                    {.bounds = {0.0F, 40.0F, 40.0F, 20.0F}, .kind = ic2d::GroundAreaKind::solid},
                    {.bounds = {60.0F, 40.0F, 40.0F, 20.0F}, .kind = ic2d::GroundAreaKind::solid},
                },
        },
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };
}

TEST_CASE("field points every cell toward the goal") {
    const ic2d::NavGrid grid = open_grid();
    ic2d::FlowField field;
    CHECK_MESSAGE((field.rebuild(grid, {0, 0}, {10.0F, 10.0F})), "An open grid builds a field.");
    CHECK_MESSAGE((field.built() && field.goal() == ic2d::NavCell{0, 0}),
                  "The field records its goal.");
    CHECK_MESSAGE((field.reachable_cell_count() == 25), "Every cell of an open grid is reachable.");
    CHECK_MESSAGE((field.cost_at({10.0F, 10.0F}) == 0.0F), "The goal cell costs nothing.");

    // A cell diagonally away from the goal steers back toward it.
    const ic2d::Vec2 direction = field.direction_at({90.0F, 90.0F});
    CHECK_MESSAGE((direction.x < 0.0F && direction.y < 0.0F),
                  "A far corner steers back toward the goal corner.");
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    CHECK_MESSAGE((std::abs(length - 1.0F) < 0.001F), "Directions are unit length.");
    const ic2d::Vec2 at_goal = field.direction_at({10.0F, 10.0F});
    CHECK_MESSAGE((at_goal.x == 0.0F && at_goal.y == 0.0F),
                  "The goal cell itself has no direction.");
}

TEST_CASE("field costs agree with a search") {
    // The field is only useful if following it is the route a search would
    // find, so compare its cost against A* over the same topology.
    const ic2d::NavGrid grid = gap_grid();
    ic2d::FlowField field;
    CHECK_MESSAGE((field.rebuild(grid, {0, 0}, {10.0F, 10.0F})),
                  "A grid with a gap builds a field.");

    const ic2d::NavCell start{0, 4};
    const ic2d::NavPathResult path = ic2d::find_nav_path(grid, start, {0, 0});
    CHECK_MESSAGE((path.status == ic2d::NavPathStatus::found),
                  "The search finds the route through the gap.");
    const float field_cost = field.cost_at({10.0F, 90.0F});
    CHECK_MESSAGE((std::abs(field_cost - path.total_distance) < 0.001F),
                  "Field cost matches the searched route distance.");

    // Steering out of the far corner must head for the gap, not into the wall.
    const ic2d::Vec2 direction = field.direction_at({10.0F, 90.0F});
    CHECK_MESSAGE((direction.x > 0.0F), "The route detours toward the gap in the wall.");
}

TEST_CASE("unreachable and invalid goals are reported") {
    const ic2d::NavGrid sealed{
        {
            .walkable_bounds = {0.0F, 0.0F, 100.0F, 100.0F},
            .max_step_height = 0.0F,
            .areas =
                {
                    {.bounds = {20.0F, 20.0F, 60.0F, 20.0F}, .kind = ic2d::GroundAreaKind::solid},
                    {.bounds = {20.0F, 60.0F, 60.0F, 20.0F}, .kind = ic2d::GroundAreaKind::solid},
                    {.bounds = {20.0F, 40.0F, 20.0F, 20.0F}, .kind = ic2d::GroundAreaKind::solid},
                    {.bounds = {60.0F, 40.0F, 20.0F, 20.0F}, .kind = ic2d::GroundAreaKind::solid},
                },
        },
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };
    ic2d::FlowField field;
    CHECK_MESSAGE((field.rebuild(sealed, {2, 2}, {50.0F, 50.0F})),
                  "A sealed goal still builds its own region.");
    // The goal is walled off, but the outer region is seeded at its own closest
    // approach, so a crowd locked out still flows to the barrier rather than
    // being left directionless and beelining at the target through the wall.
    CHECK_MESSAGE((field.reachable_cell_count() == 17),
                  "Every walkable cell receives a route, in its own region.");
    const ic2d::Vec2 outside = field.direction_at({10.0F, 10.0F});
    CHECK_MESSAGE((outside.x > 0.0F),
                  "A walled-off region steers toward its closest approach to the goal.");
    CHECK_MESSAGE((std::isfinite(field.cost_at({10.0F, 10.0F}))),
                  "A seeded region has finite cost.");
    CHECK_MESSAGE((field.cost_at({50.0F, 50.0F}) == 0.0F), "The goal itself still costs nothing.");

    ic2d::FlowField blocked_field;
    CHECK_MESSAGE((!blocked_field.rebuild(sealed, {1, 1}, {30.0F, 30.0F})),
                  "A blocked goal is rejected.");
    CHECK_MESSAGE((!blocked_field.built()), "A rejected rebuild leaves the field unbuilt.");

    ic2d::FlowField out_of_bounds;
    CHECK_MESSAGE((!out_of_bounds.rebuild(open_grid(), {99, 99}, {0.0F, 0.0F})),
                  "An out-of-bounds goal is rejected.");
}

TEST_CASE("rebuilding replaces the previous goal") {
    const ic2d::NavGrid grid = open_grid();
    ic2d::FlowField field;
    CHECK_MESSAGE((field.rebuild(grid, {0, 0}, {10.0F, 10.0F})), "The first goal builds.");
    CHECK_MESSAGE((field.rebuild(grid, {4, 4}, {90.0F, 90.0F})),
                  "A second goal rebuilds in place.");
    CHECK_MESSAGE((field.goal() == ic2d::NavCell{4, 4}), "The field reports the new goal.");
    const ic2d::Vec2 direction = field.direction_at({10.0F, 10.0F});
    CHECK_MESSAGE((direction.x > 0.0F && direction.y > 0.0F),
                  "Steering reverses to follow the new goal.");
}

} // namespace

TEST_CASE("blending removes the eight direction staircase") {
    // Sampling one cell gives every actor inside it the same one of eight
    // directions, which packs a crowd into single-file lanes on the cell
    // boundaries. Blending the surrounding cells produces directions that vary
    // continuously across a cell instead.
    const ic2d::NavGrid grid = open_grid();
    ic2d::FlowField field;
    CHECK_MESSAGE((field.rebuild(grid, {0, 0}, {10.0F, 10.0F})), "The blending fixture builds.");

    // Both samples sit inside cell (1,0) but blend with different neighbours,
    // so a per-cell answer would return the same vector for each and a blended
    // one must not.
    const ic2d::Vec2 near_west = field.direction_at({24.0F, 10.0F});
    const ic2d::Vec2 near_east = field.direction_at({36.0F, 10.0F});
    CHECK_MESSAGE((near_west.x != near_east.x || near_west.y != near_east.y),
                  "Directions vary within a cell rather than snapping to it.");
    CHECK_MESSAGE((near_west.x < 0.0F && near_east.x < 0.0F),
                  "Both samples still travel toward the goal.");
    CHECK_MESSAGE((unit_length(near_west) && unit_length(near_east)),
                  "Blended directions stay unit length.");

    // Blending must not invent a direction where the field has none.
    const ic2d::Vec2 outside = field.direction_at({-500.0F, -500.0F});
    CHECK_MESSAGE((outside.x == 0.0F && outside.y == 0.0F),
                  "Outside the field there is no direction.");
}
