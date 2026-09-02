#pragma once

#include "ic2d/nav_grid.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace ic2d {

enum class NavPathStatus {
    found,
    start_out_of_bounds,
    goal_out_of_bounds,
    start_blocked,
    goal_blocked,
    unreachable,
};

[[nodiscard]] std::string_view nav_path_status_name(NavPathStatus status) noexcept;

struct NavPathResult {
    NavPathStatus status{NavPathStatus::unreachable};
    // Start and goal are both present when status is found.
    std::vector<NavCell> cells;
    float total_distance{0.0F};
    std::size_t expanded_cell_count{0};
};

// Deterministic eight-way A* over NavGrid's public movement contract. Results
// are copied and retain no pointer into the grid. Equal candidates use a stable
// heuristic/row/column tie-break; hard blocks are never assigned a high cost.
[[nodiscard]] NavPathResult find_nav_path(
    const NavGrid& grid,
    NavCell start,
    NavCell goal
);

// World bounds are converted through NavGrid's half-open cell policy before
// the same cell search runs.
[[nodiscard]] NavPathResult find_nav_path_world(
    const NavGrid& grid,
    Vec2 start_world,
    Vec2 goal_world
);

} // namespace ic2d
