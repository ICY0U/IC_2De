#include "ic2d/nav_pathfinding.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>

namespace ic2d {
namespace {

struct CellRecord {
    float distance{std::numeric_limits<float>::infinity()};
    NavCell parent{};
    bool has_parent{false};
    bool closed{false};
};

struct OpenEntry {
    float estimated_total{0.0F};
    float heuristic{0.0F};
    float distance{0.0F};
    NavCell cell{};
};

// priority_queue places the "largest" value first, so this comparator inverts
// every key. Row/column are the final stable choice between symmetric routes.
struct WorseOpenEntry {
    [[nodiscard]] bool operator()(const OpenEntry& left, const OpenEntry& right) const noexcept {
        if (left.estimated_total != right.estimated_total) {
            return left.estimated_total > right.estimated_total;
        }
        if (left.heuristic != right.heuristic) {
            return left.heuristic > right.heuristic;
        }
        if (left.cell.row != right.cell.row) {
            return left.cell.row > right.cell.row;
        }
        if (left.cell.column != right.cell.column) {
            return left.cell.column > right.cell.column;
        }
        return left.distance > right.distance;
    }
};

[[nodiscard]] bool contains(const NavGridSnapshot& grid, const NavCell cell) noexcept {
    return cell.column >= 0 && cell.row >= 0 &&
           cell.column < grid.columns && cell.row < grid.rows;
}

[[nodiscard]] std::size_t offset(const NavGridSnapshot& grid, const NavCell cell) noexcept {
    return static_cast<std::size_t>(cell.row) * static_cast<std::size_t>(grid.columns) +
           static_cast<std::size_t>(cell.column);
}

[[nodiscard]] float octile_distance(
    const NavCell from,
    const NavCell to,
    const float cell_size
) noexcept {
    const std::int32_t delta_column = std::abs(to.column - from.column);
    const std::int32_t delta_row = std::abs(to.row - from.row);
    const std::int32_t diagonal_steps = std::min(delta_column, delta_row);
    const std::int32_t cardinal_steps =
        std::max(delta_column, delta_row) - diagonal_steps;
    constexpr float diagonal_scale = 1.4142135623730950488F;
    return cell_size *
           (static_cast<float>(cardinal_steps) +
            static_cast<float>(diagonal_steps) * diagonal_scale);
}

[[nodiscard]] NavPathResult failed(const NavPathStatus status) {
    return {.status = status};
}

} // namespace

std::string_view nav_path_status_name(const NavPathStatus status) noexcept {
    switch (status) {
    case NavPathStatus::found:
        return "Found";
    case NavPathStatus::start_out_of_bounds:
        return "Start out of bounds";
    case NavPathStatus::goal_out_of_bounds:
        return "Goal out of bounds";
    case NavPathStatus::start_blocked:
        return "Start blocked";
    case NavPathStatus::goal_blocked:
        return "Goal blocked";
    case NavPathStatus::unreachable:
        return "Unreachable";
    }
    return "Unknown";
}

NavPathResult find_nav_path(
    const NavGrid& grid,
    const NavCell start,
    const NavCell goal
) {
    // Borrowed, not copied: a search must not pay for duplicating every cell
    // in the grid before it has even looked at the request.
    const NavGridSnapshot& topology = grid.topology();
    if (!contains(topology, start)) {
        return failed(NavPathStatus::start_out_of_bounds);
    }
    if (!contains(topology, goal)) {
        return failed(NavPathStatus::goal_out_of_bounds);
    }
    if (!topology.cells[offset(topology, start)].walkable) {
        return failed(NavPathStatus::start_blocked);
    }
    if (!topology.cells[offset(topology, goal)].walkable) {
        return failed(NavPathStatus::goal_blocked);
    }
    // Cells in different connected regions have no route between them, and
    // searching for one expands the whole of the start's region every time
    // only to conclude what the baked topology already knows.
    if (grid.component_of(start) != grid.component_of(goal)) {
        return failed(NavPathStatus::unreachable);
    }

    std::vector<CellRecord> records(topology.cells.size());
    std::priority_queue<OpenEntry, std::vector<OpenEntry>, WorseOpenEntry> open;
    CellRecord& start_record = records[offset(topology, start)];
    start_record.distance = 0.0F;
    const float start_heuristic = octile_distance(start, goal, topology.cell_size);
    open.push({
        .estimated_total = start_heuristic,
        .heuristic = start_heuristic,
        .distance = 0.0F,
        .cell = start,
    });

    NavPathResult result{.status = NavPathStatus::unreachable};
    while (!open.empty()) {
        const OpenEntry current_entry = open.top();
        open.pop();
        CellRecord& current = records[offset(topology, current_entry.cell)];
        if (current.closed || current_entry.distance > current.distance) {
            continue;
        }
        current.closed = true;
        ++result.expanded_cell_count;

        if (current_entry.cell == goal) {
            result.status = NavPathStatus::found;
            result.total_distance = current.distance;
            NavCell cursor = goal;
            result.cells.push_back(cursor);
            while (cursor != start) {
                const CellRecord& cursor_record = records[offset(topology, cursor)];
                cursor = cursor_record.parent;
                result.cells.push_back(cursor);
            }
            std::reverse(result.cells.begin(), result.cells.end());
            return result;
        }

        for (const NavGridNeighbor& neighbor : grid.neighbors(current_entry.cell)) {
            CellRecord& next = records[offset(topology, neighbor.cell)];
            if (next.closed) {
                continue;
            }
            const float candidate_distance = current.distance + neighbor.distance;
            if (!(candidate_distance < next.distance)) {
                continue;
            }
            next.distance = candidate_distance;
            next.parent = current_entry.cell;
            next.has_parent = true;
            const float heuristic =
                octile_distance(neighbor.cell, goal, topology.cell_size);
            open.push({
                .estimated_total = candidate_distance + heuristic,
                .heuristic = heuristic,
                .distance = candidate_distance,
                .cell = neighbor.cell,
            });
        }
    }
    return result;
}

NavPathResult find_nav_path_world(
    const NavGrid& grid,
    const Vec2 start_world,
    const Vec2 goal_world
) {
    const std::optional<NavCell> start = grid.cell_at(start_world);
    if (!start) {
        return failed(NavPathStatus::start_out_of_bounds);
    }
    const std::optional<NavCell> goal = grid.cell_at(goal_world);
    if (!goal) {
        return failed(NavPathStatus::goal_out_of_bounds);
    }
    return find_nav_path(grid, *start, *goal);
}

} // namespace ic2d
