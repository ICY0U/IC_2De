#include <doctest/doctest.h>

#include "ic2d/nav_grid.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

[[nodiscard]] bool near(const float left, const float right) {
    return std::abs(left - right) < 0.001F;
}

[[nodiscard]] bool contains(const std::vector<ic2d::NavGridNeighbor>& neighbors,
                            const ic2d::NavCell cell) {
    for (const ic2d::NavGridNeighbor& neighbor : neighbors) {
        if (neighbor.cell == cell) {
            return true;
        }
    }
    return false;
}

TEST_CASE("dense row major bake and queries") {
    const ic2d::NavGrid grid{
        {
            .walkable_bounds = {0.0F, 0.0F, 60.0F, 40.0F},
            .max_step_height = 5.0F,
            .areas =
                {
                    {.bounds = {20.0F, 0.0F, 20.0F, 20.0F}, .kind = ic2d::GroundAreaKind::solid},
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
    CHECK_MESSAGE((snapshot.columns == 3 && snapshot.rows == 2 && snapshot.cells.size() == 6),
                  "The known bounds must bake into a dense row-major grid.");
    CHECK_MESSAGE((snapshot.walkable_cell_count == 5 && snapshot.blocked_cell_count == 1),
                  "Only solid areas may structurally remove cells from the graph.");
    CHECK_MESSAGE((snapshot.cells[1].cell == ic2d::NavCell{1, 0} && !snapshot.cells[1].walkable),
                  "The solid cell must occupy its deterministic row-major slot.");
    CHECK_MESSAGE(
        (snapshot.cells[5].cell == ic2d::NavCell{2, 1} && near(snapshot.cells[5].elevation, 4.0F)),
        "The 2.5D snapshot must retain sampled World Y elevation.");

    CHECK_MESSAGE((grid.cell_at({0.0F, 0.0F}) == ic2d::NavCell{0, 0} &&
                   grid.cell_at({19.999F, 19.999F}) == ic2d::NavCell{0, 0} &&
                   grid.cell_at({20.0F, 20.0F}) == ic2d::NavCell{1, 1}),
                  "World-to-cell conversion must use deterministic half-open cells.");
    CHECK_MESSAGE((!grid.cell_at({60.0F, 10.0F}) && !grid.cell_at({-0.001F, 10.0F})),
                  "The far X edge and positions outside the bounds must not name a cell.");
    const auto sampled = grid.cell({2, 1});
    CHECK_MESSAGE((sampled && near(sampled->center.x, 50.0F) && near(sampled->center.y, 30.0F)),
                  "Cell sampling must return the canonical world-space center.");
}

TEST_CASE("agent clearance hard blocks overlapping cells") {
    const ic2d::NavGrid grid{
        {
            .walkable_bounds = {0.0F, 0.0F, 100.0F, 100.0F},
            .max_step_height = 0.0F,
            .areas =
                {
                    {.bounds = {40.0F, 0.0F, 20.0F, 100.0F}, .kind = ic2d::GroundAreaKind::solid},
                },
        },
        {.cell_size = 20.0F, .agent_half_extents = {10.0F, 6.0F}},
    };

    const ic2d::NavGridSnapshot snapshot = grid.snapshot();
    CHECK_MESSAGE((snapshot.columns == 5 && snapshot.rows == 5 && snapshot.blocked_cell_count == 5),
                  "A solid wall must hard-block every footprint-overlapping cell.");
    CHECK_MESSAGE((grid.cell({0, 0})->walkable && grid.cell({4, 4})->walkable),
                  "Cells whose actor footprint exactly touches world bounds must remain usable.");
    CHECK_MESSAGE((!grid.cell({2, 2})->walkable),
                  "A non-walkable cell must be structurally identifiable before search exists.");
}

TEST_CASE("neighbors prevent diagonal corner cutting") {
    const ic2d::NavGrid grid{
        {
            .walkable_bounds = {0.0F, 0.0F, 60.0F, 60.0F},
            .max_step_height = 0.0F,
            .areas =
                {
                    {.bounds = {20.0F, 0.0F, 20.0F, 20.0F}, .kind = ic2d::GroundAreaKind::solid},
                },
        },
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };

    const std::vector<ic2d::NavGridNeighbor> neighbors = grid.neighbors({0, 0});
    CHECK_MESSAGE((contains(neighbors, {0, 1})),
                  "A free cardinal neighbor must remain traversable.");
    CHECK_MESSAGE((!contains(neighbors, {1, 0})),
                  "A hard-blocked destination must never be returned as a neighbor.");
    CHECK_MESSAGE((!contains(neighbors, {1, 1})),
                  "A diagonal step must be rejected when either orthogonal flank is blocked.");
    CHECK_MESSAGE((grid.neighbors({1, 0}).empty()),
                  "A structurally blocked source cell must expose no graph edges.");
}

TEST_CASE("height step limit is part of connectivity") {
    const ic2d::NavGrid grid{
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

    CHECK_MESSAGE((grid.cell({1, 0})->walkable && near(grid.cell({1, 0})->elevation, 10.0F)),
                  "A high cell remains a valid surface even when this agent cannot step onto it.");
    CHECK_MESSAGE((!contains(grid.neighbors({0, 0}), {1, 0})),
                  "Neighbor connectivity must reject elevation changes above max step height.");
}

TEST_CASE("open grid neighbor order and distance are stable") {
    const ic2d::NavGrid grid{
        {.walkable_bounds = {0.0F, 0.0F, 60.0F, 60.0F}, .max_step_height = 0.0F},
        {.cell_size = 20.0F, .agent_half_extents = {}},
    };
    const std::vector<ic2d::NavGridNeighbor> neighbors = grid.neighbors({1, 1});
    CHECK_MESSAGE((neighbors.size() == 8 && neighbors.front().cell == ic2d::NavCell{1, 0} &&
                   neighbors.back().cell == ic2d::NavCell{0, 0}),
                  "Neighbors must use the documented clockwise order beginning at negative Z.");
    CHECK_MESSAGE(
        (near(neighbors.front().distance, 20.0F) && near(neighbors[1].distance, 28.284271F)),
        "Neighbor distance must be expressed in world units for later A-star costs.");
}

TEST_CASE("invalid bake settings are rejected") {
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
        static_cast<void>(
            ic2d::NavGrid{ground, {.cell_size = 20.0F, .agent_half_extents = {-1.0F, 0.0F}}});
    } catch (const std::invalid_argument&) {
        rejected_negative_clearance = true;
    }
    try {
        static_cast<void>(ic2d::NavGrid{
            ground,
            {.cell_size = std::numeric_limits<float>::infinity(), .agent_half_extents = {}},
        });
    } catch (const std::invalid_argument&) {
        rejected_non_finite = true;
    }
    CHECK_MESSAGE((rejected_zero_cell && rejected_negative_clearance && rejected_non_finite),
                  "Invalid or non-finite bake settings must fail before allocation.");
}

} // namespace
