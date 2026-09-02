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

[[nodiscard]] ic2d::NavGrid open_grid(
    const float width = 60.0F,
    const float depth = 60.0F
) {
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
            .areas = {
                {.bounds = {20.0F, 20.0F, 20.0F, 20.0F},
                 .kind = ic2d::GroundAreaKind::solid},
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
            .areas = {
                {.bounds = {20.0F, 0.0F, 20.0F, 20.0F},
                 .kind = ic2d::GroundAreaKind::solid},
                {.bounds = {0.0F, 20.0F, 20.0F, 20.0F},
                 .kind = ic2d::GroundAreaKind::solid},
            },
        },
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };
    const ic2d::NavPathResult corner =
        ic2d::find_nav_path(corner_grid, {0, 0}, {1, 1});
    expect(corner.status == ic2d::NavPathStatus::unreachable && corner.cells.empty(),
           "A-star must inherit NavGrid's diagonal corner-cutting prohibition.");

    const ic2d::NavGrid height_grid{
        {
            .walkable_bounds = {0.0F, 0.0F, 40.0F, 20.0F},
            .max_step_height = 5.0F,
            .areas = {
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
            .areas = {
                {.bounds = {20.0F, 0.0F, 20.0F, 20.0F},
                 .kind = ic2d::GroundAreaKind::solid},
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
    expect(ic2d::find_nav_path(grid, {1, 0}, {2, 0}).status ==
               ic2d::NavPathStatus::start_blocked,
           "A hard-blocked start must fail before search.");
    expect(ic2d::find_nav_path(grid, {0, 0}, {1, 0}).status ==
               ic2d::NavPathStatus::goal_blocked,
           "A hard-blocked goal must fail before search.");
}

void test_world_queries_use_half_open_conversion_and_copy_results() {
    const ic2d::NavGrid grid = open_grid(60.0F, 20.0F);
    ic2d::NavPathResult path = ic2d::find_nav_path_world(
        grid, ic2d::Vec2{0.0F, 0.0F}, ic2d::Vec2{59.999F, 19.999F});
    expect(path.status == ic2d::NavPathStatus::found &&
               path.cells == std::vector<ic2d::NavCell>{{0, 0}, {1, 0}, {2, 0}},
           "World queries must reuse NavGrid's canonical half-open conversion.");
    expect(ic2d::find_nav_path_world(
               grid, ic2d::Vec2{60.0F, 10.0F}, ic2d::Vec2{10.0F, 10.0F}).status ==
               ic2d::NavPathStatus::start_out_of_bounds,
           "The far world edge must report an explicit out-of-bounds start.");

    path.cells.clear();
    const ic2d::NavPathResult second = ic2d::find_nav_path_world(
        grid, ic2d::Vec2{0.0F, 0.0F}, ic2d::Vec2{59.999F, 19.999F});
    expect(second.cells.size() == 3,
           "A returned path must own its cells independently of prior result mutation.");
}

void test_same_cell_and_status_names() {
    const ic2d::NavGrid grid = open_grid();
    const ic2d::NavPathResult path = ic2d::find_nav_path(grid, {1, 1}, {1, 1});
    expect(path.status == ic2d::NavPathStatus::found &&
               path.cells == std::vector<ic2d::NavCell>{{1, 1}} &&
               near(path.total_distance, 0.0F),
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
            .areas = {
                {.bounds = {20.0F, 20.0F, 60.0F, 20.0F},
                 .kind = ic2d::GroundAreaKind::solid},
                {.bounds = {20.0F, 60.0F, 60.0F, 20.0F},
                 .kind = ic2d::GroundAreaKind::solid},
                {.bounds = {20.0F, 40.0F, 20.0F, 20.0F},
                 .kind = ic2d::GroundAreaKind::solid},
                {.bounds = {60.0F, 40.0F, 20.0F, 20.0F},
                 .kind = ic2d::GroundAreaKind::solid},
            },
        },
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };
    expect(sealed.component_count() == 2,
           "A sealed centre and its surroundings are two connected regions.");
    expect(sealed.component_of({2, 2}) != sealed.component_of({0, 0}),
           "The enclosed cell does not share the outer region.");
    expect(sealed.component_of({1, 1}) == 0U,
           "A blocked cell belongs to no region.");

    const ic2d::NavPathResult inward = ic2d::find_nav_path(sealed, {0, 0}, {2, 2});
    expect(inward.status == ic2d::NavPathStatus::unreachable && inward.cells.empty(),
           "A route into a sealed region is unreachable.");
    expect(inward.expanded_cell_count == 0,
           "An impossible request expands no cells at all.");

    const ic2d::NavPathResult around = ic2d::find_nav_path(sealed, {0, 0}, {4, 4});
    expect(around.status == ic2d::NavPathStatus::found,
           "Routes inside one region are unaffected by the region check.");
}

int main() {
    test_open_grid_uses_optimal_octile_route();
    test_hard_block_routes_around_with_stable_tie_breaking();
    test_corner_trap_and_elevation_barrier_are_unreachable();
    test_sealed_region_is_rejected_without_expanding_the_grid();
    test_endpoints_fail_explicitly();
    test_world_queries_use_half_open_conversion_and_copy_results();
    test_same_cell_and_status_names();

    if (failures == 0) {
        std::cout << "NavPathfinding tests passed.\n";
        return 0;
    }
    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
