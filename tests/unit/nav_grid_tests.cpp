#include "ic2d/nav_grid.hpp"

#include <cmath>
#include <iostream>
#include <limits>
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

[[nodiscard]] bool contains(
    const std::vector<ic2d::NavGridNeighbor>& neighbors,
    const ic2d::NavCell cell
) {
    for (const ic2d::NavGridNeighbor& neighbor : neighbors) {
        if (neighbor.cell == cell) {
            return true;
        }
    }
    return false;
}

void test_dense_row_major_bake_and_queries() {
    const ic2d::NavGrid grid{
        {
            .walkable_bounds = {0.0F, 0.0F, 60.0F, 40.0F},
            .max_step_height = 5.0F,
            .areas = {
                {.bounds = {20.0F, 0.0F, 20.0F, 20.0F},
                 .kind = ic2d::GroundAreaKind::solid},
                {.bounds = {40.0F, 20.0F, 20.0F, 20.0F},
                 .kind = ic2d::GroundAreaKind::elevation,
                 .elevation = 4.0F},
                {.bounds = {0.0F, 20.0F, 20.0F, 20.0F},
                 .kind = ic2d::GroundAreaKind::trigger,
                 .tag = 7},
            },
        },
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };

    const ic2d::NavGridSnapshot snapshot = grid.snapshot();
    expect(snapshot.columns == 3 && snapshot.rows == 2 && snapshot.cells.size() == 6,
           "The known bounds must bake into a dense row-major grid.");
    expect(snapshot.walkable_cell_count == 5 && snapshot.blocked_cell_count == 1,
           "Only solid areas may structurally remove cells from the graph.");
    expect(snapshot.cells[1].cell == ic2d::NavCell{1, 0} && !snapshot.cells[1].walkable,
           "The solid cell must occupy its deterministic row-major slot.");
    expect(snapshot.cells[5].cell == ic2d::NavCell{2, 1} &&
               near(snapshot.cells[5].elevation, 4.0F),
           "The 2.5D snapshot must retain sampled World Y elevation.");

    expect(grid.cell_at({0.0F, 0.0F}) == ic2d::NavCell{0, 0} &&
               grid.cell_at({19.999F, 19.999F}) == ic2d::NavCell{0, 0} &&
               grid.cell_at({20.0F, 20.0F}) == ic2d::NavCell{1, 1},
           "World-to-cell conversion must use deterministic half-open cells.");
    expect(!grid.cell_at({60.0F, 10.0F}) && !grid.cell_at({-0.001F, 10.0F}),
           "The far X edge and positions outside the bounds must not name a cell.");
    const auto sampled = grid.cell({2, 1});
    expect(sampled && near(sampled->center.x, 50.0F) && near(sampled->center.y, 30.0F),
           "Cell sampling must return the canonical world-space center.");
}

void test_agent_clearance_hard_blocks_overlapping_cells() {
    const ic2d::NavGrid grid{
        {
            .walkable_bounds = {0.0F, 0.0F, 100.0F, 100.0F},
            .max_step_height = 0.0F,
            .areas = {
                {.bounds = {40.0F, 0.0F, 20.0F, 100.0F},
                 .kind = ic2d::GroundAreaKind::solid},
            },
        },
        {.cell_size = 20.0F, .agent_half_extents = {10.0F, 6.0F}},
    };

    const ic2d::NavGridSnapshot snapshot = grid.snapshot();
    expect(snapshot.columns == 5 && snapshot.rows == 5 &&
               snapshot.blocked_cell_count == 5,
           "A solid wall must hard-block every footprint-overlapping cell.");
    expect(grid.cell({0, 0})->walkable && grid.cell({4, 4})->walkable,
           "Cells whose actor footprint exactly touches world bounds must remain usable.");
    expect(!grid.cell({2, 2})->walkable,
           "A non-walkable cell must be structurally identifiable before search exists.");
}

void test_neighbors_prevent_diagonal_corner_cutting() {
    const ic2d::NavGrid grid{
        {
            .walkable_bounds = {0.0F, 0.0F, 60.0F, 60.0F},
            .max_step_height = 0.0F,
            .areas = {
                {.bounds = {20.0F, 0.0F, 20.0F, 20.0F},
                 .kind = ic2d::GroundAreaKind::solid},
            },
        },
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };

    const std::vector<ic2d::NavGridNeighbor> neighbors = grid.neighbors({0, 0});
    expect(contains(neighbors, {0, 1}),
           "A free cardinal neighbor must remain traversable.");
    expect(!contains(neighbors, {1, 0}),
           "A hard-blocked destination must never be returned as a neighbor.");
    expect(!contains(neighbors, {1, 1}),
           "A diagonal step must be rejected when either orthogonal flank is blocked.");
    expect(grid.neighbors({1, 0}).empty(),
           "A structurally blocked source cell must expose no graph edges.");
}

void test_height_step_limit_is_part_of_connectivity() {
    const ic2d::NavGrid grid{
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

    expect(grid.cell({1, 0})->walkable && near(grid.cell({1, 0})->elevation, 10.0F),
           "A high cell remains a valid surface even when this agent cannot step onto it.");
    expect(!contains(grid.neighbors({0, 0}), {1, 0}),
           "Neighbor connectivity must reject elevation changes above max step height.");
}

void test_open_grid_neighbor_order_and_distance_are_stable() {
    const ic2d::NavGrid grid{
        {.walkable_bounds = {0.0F, 0.0F, 60.0F, 60.0F}, .max_step_height = 0.0F},
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };
    const std::vector<ic2d::NavGridNeighbor> neighbors = grid.neighbors({1, 1});
    expect(neighbors.size() == 8 && neighbors.front().cell == ic2d::NavCell{1, 0} &&
               neighbors.back().cell == ic2d::NavCell{0, 0},
           "Neighbors must use the documented clockwise order beginning at negative Z.");
    expect(near(neighbors.front().distance, 20.0F) &&
               near(neighbors[1].distance, 28.284271F),
           "Neighbor distance must be expressed in world units for later A-star costs.");
}

void test_invalid_bake_settings_are_rejected() {
    const ic2d::GroundMapDefinition ground{
        .walkable_bounds = {0.0F, 0.0F, 60.0F, 60.0F},
        .max_step_height = 0.0F,
    };
    bool rejected_zero_cell = false;
    bool rejected_negative_clearance = false;
    bool rejected_non_finite = false;
    try {
        static_cast<void>(ic2d::NavGrid{ground, {.cell_size = 0.0F}});
    } catch (const std::invalid_argument&) {
        rejected_zero_cell = true;
    }
    try {
        static_cast<void>(ic2d::NavGrid{
            ground, {.cell_size = 20.0F, .agent_half_extents = {-1.0F, 0.0F}}});
    } catch (const std::invalid_argument&) {
        rejected_negative_clearance = true;
    }
    try {
        static_cast<void>(ic2d::NavGrid{
            ground,
            {.cell_size = std::numeric_limits<float>::infinity(),
             .agent_half_extents = {}},
        });
    } catch (const std::invalid_argument&) {
        rejected_non_finite = true;
    }
    expect(rejected_zero_cell && rejected_negative_clearance && rejected_non_finite,
           "Invalid or non-finite bake settings must fail before allocation.");
}

} // namespace

int main() {
    test_dense_row_major_bake_and_queries();
    test_agent_clearance_hard_blocks_overlapping_cells();
    test_neighbors_prevent_diagonal_corner_cutting();
    test_height_step_limit_is_part_of_connectivity();
    test_open_grid_neighbor_order_and_distance_are_stable();
    test_invalid_bake_settings_are_rejected();

    if (failures == 0) {
        std::cout << "NavGrid tests passed.\n";
        return 0;
    }
    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
