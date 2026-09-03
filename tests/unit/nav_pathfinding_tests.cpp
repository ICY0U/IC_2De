#include "ic2d/nav_pathfinding.hpp"

#include <cmath>
#include <iostream>
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

[[nodiscard]] ic2d::NavGrid open_grid(const float width = 60.0F, const float depth = 60.0F) {
    return ic2d::NavGrid{
        {.walkable_bounds = {0.0F, 0.0F, width, depth}, .max_step_height = 0.0F},
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };
}

void test_open_grid_uses_optimal_octile_route() {
    const ic2d::NavGrid grid = open_grid();
    const ic2d::NavPathResult path = ic2d::find_nav_path(grid, {0, 0}, {2, 2});

    expect(path.status == ic2d::NavPathStatus::found,
           "An open-grid request must return a found result.");
    expect(path.cells == std::vector<ic2d::NavCell>{{0, 0}, {1, 1}, {2, 2}},
           "A-star must choose the optimal two-diagonal octile route.");
    expect(near(path.total_distance, 56.568542F),
           "Path distance must use NavGrid's physical world-unit edge costs.");
    expect(path.expanded_cell_count > 0,
           "A found result must report honest search expansion work.");
}

void test_hard_block_routes_around_with_stable_tie_breaking() {
    const ic2d::NavGrid grid{
        {
            .walkable_bounds = {0.0F, 0.0F, 60.0F, 60.0F},
            .max_step_height = 0.0F,
            .areas =
                {
                    {.bounds = {20.0F, 20.0F, 20.0F, 20.0F}, .kind = ic2d::GroundAreaKind::solid},
                },
        },
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };
    const std::vector<ic2d::NavCell> expected{
        {0, 1}, {0, 0}, {1, 0}, {2, 0}, {2, 1},
    };

    for (int iteration = 0; iteration < 8; ++iteration) {
        const ic2d::NavPathResult path = ic2d::find_nav_path(grid, {0, 1}, {2, 1});
        expect(path.status == ic2d::NavPathStatus::found && path.cells == expected,
               "Equal routes must resolve identically and never enter the hard block.");
        expect(near(path.total_distance, 80.0F),
               "No-corner-cutting must force four cardinal steps around the block.");
    }
}

void test_corner_trap_and_elevation_barrier_are_unreachable() {
    const ic2d::NavGrid corner_grid{
        {
            .walkable_bounds = {0.0F, 0.0F, 40.0F, 40.0F},
            .max_step_height = 0.0F,
            .areas =
                {
                    {.bounds = {20.0F, 0.0F, 20.0F, 20.0F}, .kind = ic2d::GroundAreaKind::solid},
                    {.bounds = {0.0F, 20.0F, 20.0F, 20.0F}, .kind = ic2d::GroundAreaKind::solid},
                },
        },
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };
    const ic2d::NavPathResult corner = ic2d::find_nav_path(corner_grid, {0, 0}, {1, 1});
    expect(corner.status == ic2d::NavPathStatus::unreachable && corner.cells.empty(),
           "A-star must inherit NavGrid's diagonal corner-cutting prohibition.");

    const ic2d::NavGrid height_grid{
        {
            .walkable_bounds = {0.0F, 0.0F, 40.0F, 20.0F},
            .max_step_height = 5.0F,
            .areas =
                {
                    {.bounds = {20.0F, 0.0F, 20.0F, 20.0F},
                     .kind = ic2d::GroundAreaKind::elevation,
                     .elevation = 10.0F},
                },
        },
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };
    expect(ic2d::find_nav_path(height_grid, {0, 0}, {1, 0}).status ==
               ic2d::NavPathStatus::unreachable,
           "A-star must inherit NavGrid's maximum-step connectivity barrier.");
}

void test_endpoints_fail_explicitly() {
    const ic2d::NavGrid grid{
        {
            .walkable_bounds = {0.0F, 0.0F, 60.0F, 20.0F},
            .max_step_height = 0.0F,
            .areas =
                {
                    {.bounds = {20.0F, 0.0F, 20.0F, 20.0F}, .kind = ic2d::GroundAreaKind::solid},
                },
        },
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };

    expect(ic2d::find_nav_path(grid, {-1, 0}, {2, 0}).status ==
               ic2d::NavPathStatus::start_out_of_bounds,
           "An invalid start cell must have its own result status.");
    expect(ic2d::find_nav_path(grid, {0, 0}, {3, 0}).status ==
               ic2d::NavPathStatus::goal_out_of_bounds,
           "An invalid goal cell must have its own result status.");
    expect(ic2d::find_nav_path(grid, {1, 0}, {2, 0}).status == ic2d::NavPathStatus::start_blocked,
           "A hard-blocked start must fail before search.");
    expect(ic2d::find_nav_path(grid, {0, 0}, {1, 0}).status == ic2d::NavPathStatus::goal_blocked,
           "A hard-blocked goal must fail before search.");
}

void test_world_queries_use_half_open_conversion_and_copy_results() {
    const ic2d::NavGrid grid = open_grid(60.0F, 20.0F);
    ic2d::NavPathResult path =
        ic2d::find_nav_path_world(grid, ic2d::Vec2{0.0F, 0.0F}, ic2d::Vec2{59.999F, 19.999F});
    expect(path.status == ic2d::NavPathStatus::found &&
               path.cells == std::vector<ic2d::NavCell>{{0, 0}, {1, 0}, {2, 0}},
           "World queries must reuse NavGrid's canonical half-open conversion.");
    expect(ic2d::find_nav_path_world(grid, ic2d::Vec2{60.0F, 10.0F}, ic2d::Vec2{10.0F, 10.0F})
                   .status == ic2d::NavPathStatus::start_out_of_bounds,
           "The far world edge must report an explicit out-of-bounds start.");

    path.cells.clear();
    const ic2d::NavPathResult second =
        ic2d::find_nav_path_world(grid, ic2d::Vec2{0.0F, 0.0F}, ic2d::Vec2{59.999F, 19.999F});
    expect(second.cells.size() == 3,
           "A returned path must own its cells independently of prior result mutation.");
}

void test_same_cell_and_status_names() {
    const ic2d::NavGrid grid = open_grid();
    const ic2d::NavPathResult path = ic2d::find_nav_path(grid, {1, 1}, {1, 1});
    expect(path.status == ic2d::NavPathStatus::found &&
               path.cells == std::vector<ic2d::NavCell>{{1, 1}} && near(path.total_distance, 0.0F),
           "A valid same-cell query must return one zero-distance cell.");
    expect(ic2d::nav_path_status_name(ic2d::NavPathStatus::found) == "Found" &&
               ic2d::nav_path_status_name(ic2d::NavPathStatus::unreachable) == "Unreachable",
           "Every result status must have stable editor-facing text.");
}

} // namespace

void test_sealed_region_is_rejected_without_expanding_the_grid() {
    // A ring of solid cells seals the centre off completely. The search must
    // recognize the two regions from baked topology rather than expanding the
    // whole outer region to rediscover it on every request.
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
    expect(sealed.component_count() == 2,
           "A sealed centre and its surroundings are two connected regions.");
    expect(sealed.component_of({2, 2}) != sealed.component_of({0, 0}),
           "The enclosed cell does not share the outer region.");
    expect(sealed.component_of({1, 1}) == 0U, "A blocked cell belongs to no region.");

    const ic2d::NavPathResult inward = ic2d::find_nav_path(sealed, {0, 0}, {2, 2});
    expect(inward.status == ic2d::NavPathStatus::unreachable && inward.cells.empty(),
           "A route into a sealed region is unreachable.");
    expect(inward.expanded_cell_count == 0, "An impossible request expands no cells at all.");

    const ic2d::NavPathResult around = ic2d::find_nav_path(sealed, {0, 0}, {4, 4});
    expect(around.status == ic2d::NavPathStatus::found,
           "Routes inside one region are unaffected by the region check.");
}

// Line of sight is what lets a route be walked directly instead of through
// every cell centre, so it has to be exact: a segment that clips solid ground,
// however narrowly, must be rejected.
void test_line_of_sight_respects_blocks_corners_and_elevation() {
    const ic2d::NavGrid open = open_grid();
    expect(ic2d::nav_line_of_sight(open, {10.0F, 10.0F}, {50.0F, 50.0F}),
           "An open diagonal must be visible.");
    expect(ic2d::nav_line_of_sight(open, {10.0F, 10.0F}, {10.0F, 10.0F}),
           "A point must see itself.");
    expect(ic2d::nav_line_of_sight(open, {12.0F, 10.0F}, {48.0F, 11.0F}),
           "A shallow line across open cells must be visible.");
    expect(!ic2d::nav_line_of_sight(open, {10.0F, 10.0F}, {200.0F, 10.0F}),
           "A point outside the grid cannot be seen.");
    expect(!ic2d::nav_line_of_sight(open, {10.0F, 10.0F}, {std::nanf(""), 10.0F}),
           "A non-finite endpoint must be rejected rather than searched.");

    // One solid cell in the middle of a three by three grid.
    const ic2d::NavGrid blocked{
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
    expect(!ic2d::nav_line_of_sight(blocked, {10.0F, 30.0F}, {50.0F, 30.0F}),
           "A line straight through a solid cell must be rejected.");
    expect(!ic2d::nav_line_of_sight(blocked, {30.0F, 30.0F}, {10.0F, 10.0F}),
           "A line starting inside a solid cell must be rejected.");
    expect(ic2d::nav_line_of_sight(blocked, {10.0F, 10.0F}, {50.0F, 10.0F}),
           "A line along a clear row beside the block must stay visible.");

    // The exact diagonal through the block's corner. Point sampling can step
    // over this; the boundary walk must refuse it, for the same reason the
    // search refuses to cut a corner.
    expect(!ic2d::nav_line_of_sight(blocked, {10.0F, 10.0F}, {50.0F, 50.0F}),
           "A diagonal through a blocked corner must be rejected.");

    // A step taller than the agent may climb blocks sight the same way it
    // blocks a neighbour edge.
    const ic2d::NavGrid stepped{
        {
            .walkable_bounds = {0.0F, 0.0F, 60.0F, 20.0F},
            .max_step_height = 4.0F,
            .areas = {{
                .bounds = {20.0F, 0.0F, 20.0F, 20.0F},
                .kind = ic2d::GroundAreaKind::elevation,
                .elevation = 40.0F,
            }},
        },
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };
    expect(!ic2d::nav_line_of_sight(stepped, {10.0F, 10.0F}, {50.0F, 10.0F}),
           "A step the agent cannot climb must block sight.");
}

// Clearance is what stops a smoothed route being walked into the wall it just
// cut the corner of.
void test_obstacle_clearance_pushes_away_without_reversing_intent() {
    const ic2d::NavGrid blocked{
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

    // Walking north along the block's west face. The push must be westward,
    // away from the wall, while still going north.
    const ic2d::Vec2 hugging{18.0F, 30.0F};
    const ic2d::Vec2 north{0.0F, -1.0F};
    const ic2d::Vec2 steered = ic2d::nav_avoid_obstacles(blocked, hugging, north);
    expect(steered.x < 0.0F, "An actor beside a wall must be pushed away from it.");
    expect(steered.y < 0.0F, "Clearance must not reverse the direction the actor wanted.");
    expect(near(std::sqrt(steered.x * steered.x + steered.y * steered.y), 1.0F),
           "The steered direction must stay unit length.");

    // Far from anything solid, the desired direction is returned untouched.
    const ic2d::NavGrid open = open_grid(200.0F, 200.0F);
    const ic2d::Vec2 clear =
        ic2d::nav_avoid_obstacles(open, {100.0F, 100.0F}, {0.70710678F, 0.70710678F});
    expect(near(clear.x, 0.70710678F) && near(clear.y, 0.70710678F),
           "Open ground must leave the desired direction unchanged.");

    // A stationary actor pressed against a wall is still pushed clear.
    const ic2d::Vec2 stuck = ic2d::nav_avoid_obstacles(blocked, {18.0F, 30.0F}, {});
    expect(stuck.x < 0.0F, "A stationary actor beside a wall must still be pushed out of it.");

    // The grid edge is a wall too, or actors would grind along the boundary.
    const ic2d::Vec2 edge = ic2d::nav_avoid_obstacles(open, {2.0F, 100.0F}, {0.0F, -1.0F});
    expect(edge.x > 0.0F && edge.y < 0.0F,
           "Moving along the world edge must be nudged inward while still moving.");

    // Walking straight into a wall must never turn into walking away from it.
    // Reversing the intent is what makes an actor leave the radius, be aimed at
    // the wall again, and oscillate; pressing against the wall and letting
    // ground collision stop it is the stable answer.
    const ic2d::Vec2 head_on = ic2d::nav_avoid_obstacles(blocked, {18.0F, 30.0F}, {1.0F, 0.0F});
    expect(head_on.x >= 0.0F, "Clearance must not reverse an actor walking straight into a wall.");

    // Rejected settings and positions leave the intent alone rather than
    // inventing a direction.
    const ic2d::Vec2 outside = ic2d::nav_avoid_obstacles(open, {1000.0F, 1000.0F}, {1.0F, 0.0F});
    expect(near(outside.x, 1.0F) && near(outside.y, 0.0F),
           "A position off the grid must return the desired direction unchanged.");
    const ic2d::Vec2 bad_settings =
        ic2d::nav_avoid_obstacles(blocked, hugging, north, {.radius = 0.0F});
    expect(near(bad_settings.x, 0.0F) && near(bad_settings.y, -1.0F),
           "A non-positive radius must return the desired direction unchanged.");
}

// An attacker whose target sits behind a wall walks its route into that wall and
// stays there. The clearance term used to answer one way inside its radius and
// the opposite way outside it, so the actor was turned away, drifted clear,
// was aimed at the wall again, and flipped its facing every few ticks: the
// spin-in-place that a blocked crowd showed against every barrier.
void test_clearance_settles_against_a_wall_instead_of_oscillating() {
    const ic2d::NavGrid blocked{
        {
            .walkable_bounds = {0.0F, 0.0F, 120.0F, 120.0F},
            .max_step_height = 0.0F,
            .areas = {{
                .bounds = {60.0F, 0.0F, 60.0F, 120.0F},
                .kind = ic2d::GroundAreaKind::solid,
            }},
        },
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };

    // Due east, straight at the barrier, from open ground well outside the
    // clearance radius. Nothing about the intent changes from tick to tick, so
    // any change in the steered direction is the term contradicting itself.
    const ic2d::Vec2 intent{1.0F, 0.0F};
    constexpr float step = 2.0F;
    ic2d::Vec2 position{20.0F, 70.0F};
    ic2d::Vec2 previous = ic2d::nav_avoid_obstacles(blocked, position, intent);
    for (int tick = 0; tick < 200; ++tick) {
        const ic2d::Vec2 steered = ic2d::nav_avoid_obstacles(blocked, position, intent);
        expect(steered.x * intent.x + steered.y * intent.y >= 0.0F,
               "Approaching a wall must never be steered back the way the actor came.");
        expect(steered.x * previous.x + steered.y * previous.y > 0.0F,
               "Consecutive steps toward a wall must not flip the steered direction.");
        previous = steered;
        // Ground collision owns the wall itself; this only has to stop the
        // sample from walking through it.
        const ic2d::Vec2 moved{position.x + steered.x * step, position.y + steered.y * step};
        if (moved.x < 58.0F) {
            position = moved;
        }
    }
    expect(position.x > 40.0F,
           "An actor pressing on a wall must end up at it, not bounced away from it.");
}

int main() {
    test_open_grid_uses_optimal_octile_route();
    test_hard_block_routes_around_with_stable_tie_breaking();
    test_corner_trap_and_elevation_barrier_are_unreachable();
    test_sealed_region_is_rejected_without_expanding_the_grid();
    test_endpoints_fail_explicitly();
    test_world_queries_use_half_open_conversion_and_copy_results();
    test_same_cell_and_status_names();
    test_line_of_sight_respects_blocks_corners_and_elevation();
    test_obstacle_clearance_pushes_away_without_reversing_intent();
    test_clearance_settles_against_a_wall_instead_of_oscillating();

    if (failures == 0) {
        std::cout << "NavPathfinding tests passed.\n";
        return 0;
    }
    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
