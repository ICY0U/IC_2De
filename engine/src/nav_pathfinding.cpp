#include "ic2d/nav_pathfinding.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
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
    return cell.column >= 0 && cell.row >= 0 && cell.column < grid.columns && cell.row < grid.rows;
}

[[nodiscard]] std::size_t offset(const NavGridSnapshot& grid, const NavCell cell) noexcept {
    return static_cast<std::size_t>(cell.row) * static_cast<std::size_t>(grid.columns) +
           static_cast<std::size_t>(cell.column);
}

[[nodiscard]] float octile_distance(const NavCell from, const NavCell to,
                                    const float cell_size) noexcept {
    const std::int32_t delta_column = std::abs(to.column - from.column);
    const std::int32_t delta_row = std::abs(to.row - from.row);
    const std::int32_t diagonal_steps = std::min(delta_column, delta_row);
    const std::int32_t cardinal_steps = std::max(delta_column, delta_row) - diagonal_steps;
    constexpr float diagonal_scale = 1.4142135623730950488F;
    return cell_size * (static_cast<float>(cardinal_steps) +
                        static_cast<float>(diagonal_steps) * diagonal_scale);
}

[[nodiscard]] NavPathResult failed(const NavPathStatus status) { return {.status = status}; }

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

NavPathResult find_nav_path(const NavGrid& grid, const NavCell start, const NavCell goal) {
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
            const float heuristic = octile_distance(neighbor.cell, goal, topology.cell_size);
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

Vec2 nav_avoid_obstacles(const NavGrid& grid, const Vec2 position, const Vec2 desired,
                         const NavClearanceSettings settings) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(desired.x) ||
        !std::isfinite(desired.y) || !std::isfinite(settings.radius) || !(settings.radius > 0.0F) ||
        !std::isfinite(settings.strength) || settings.strength < 0.0F) {
        return desired;
    }
    const NavGridSnapshot& topology = grid.topology();
    if (!(topology.cell_size > 0.0F)) {
        return desired;
    }

    const std::optional<NavCell> origin = grid.cell_at(position);
    if (!origin) {
        return desired;
    }

    // Only the cells the radius can reach are examined, so the cost is a small
    // constant rather than a function of the map.
    const auto span = static_cast<std::int32_t>(std::ceil(settings.radius / topology.cell_size));
    Vec2 push{};
    // How firmly the nearest obstacle is felt, on the same zero-at-the-radius
    // scale as the push itself. Every term below is faded by it, so an actor
    // one step inside the radius is steered almost exactly as it was one step
    // outside it.
    float proximity = 0.0F;
    for (std::int32_t row = origin->row - span; row <= origin->row + span; ++row) {
        for (std::int32_t column = origin->column - span; column <= origin->column + span;
             ++column) {
            const NavCell cell{column, row};
            const std::optional<NavGridCell> found = grid.cell(cell);
            // Off the grid counts as solid: the edge of the world is a wall.
            const bool solid = !found || !found->walkable;
            if (!solid) {
                continue;
            }
            const Vec2 centre = found
                                    ? found->center
                                    : Vec2{topology.bounds.x + (static_cast<float>(column) + 0.5F) *
                                                                   topology.cell_size,
                                           topology.bounds.z + (static_cast<float>(row) + 0.5F) *
                                                                   topology.cell_size};
            const Vec2 away{position.x - centre.x, position.y - centre.y};
            const float distance = std::sqrt(away.x * away.x + away.y * away.y);
            if (!(distance > 0.0001F) || distance >= settings.radius) {
                continue;
            }
            // Linear falloff: firm when touching, nothing at the radius. A
            // sharper curve makes actors jitter as they cross the boundary.
            const float weight = (settings.radius - distance) / settings.radius;
            proximity = std::max(proximity, weight);
            push.x += away.x / distance * weight;
            push.y += away.y / distance * weight;
        }
    }
    const float push_length = std::sqrt(push.x * push.x + push.y * push.y);
    if (!(push_length > 0.0001F)) {
        return desired;
    }
    const Vec2 away{push.x / push_length, push.y / push_length};

    // Adding a push that points straight back along the desired direction only
    // shortens it, and the result is renormalized, so the actor would walk into
    // the wall regardless. Removing the component that heads into the obstacle
    // first is what makes an actor slide along a surface instead of pressing
    // into it, which is also how a smoothed route rounds the corner it cut.
    Vec2 base = desired;
    const float into = base.x * away.x + base.y * away.y;
    if (into < 0.0F) {
        // Faded by proximity rather than removed outright. Removing the whole
        // head-on component the moment an obstacle comes into range, and none
        // of it a step further out, makes this function jump between two very
        // different answers either side of the radius: an actor whose route
        // points at a wall is turned away, leaves the radius, is aimed at the
        // wall again, and spins on the spot instead of settling against it.
        base.x -= away.x * into * proximity;
        base.y -= away.y * into * proximity;
    }

    Vec2 steered{
        base.x + push.x * settings.strength,
        base.y + push.y * settings.strength,
    };

    // Clearance bends an intent; it never reverses one. A corner sums pushes
    // from several cells and can outweigh the intent entirely, which would
    // walk an actor backwards out of the very route it is following. Dropping
    // the opposing component leaves the sideways part, so the actor rounds the
    // corner, and leaves nothing at all when it is head-on into a flat wall,
    // where pressing against it and stopping is the honest answer.
    const float desired_length = std::sqrt(desired.x * desired.x + desired.y * desired.y);
    if (desired_length > 0.0001F) {
        const Vec2 intent{desired.x / desired_length, desired.y / desired_length};
        const float against = steered.x * intent.x + steered.y * intent.y;
        if (against < 0.0F) {
            steered.x -= intent.x * against;
            steered.y -= intent.y * against;
        }
    }

    const float length = std::sqrt(steered.x * steered.x + steered.y * steered.y);
    if (!(length > 0.0001F)) {
        return desired;
    }
    return {steered.x / length, steered.y / length};
}

bool nav_line_of_sight(const NavGrid& grid, const Vec2 from_world, const Vec2 to_world) {
    if (!std::isfinite(from_world.x) || !std::isfinite(from_world.y) ||
        !std::isfinite(to_world.x) || !std::isfinite(to_world.y)) {
        return false;
    }
    const std::optional<NavCell> start = grid.cell_at(from_world);
    const std::optional<NavCell> goal = grid.cell_at(to_world);
    if (!start || !goal) {
        return false;
    }
    const std::optional<NavGridCell> start_cell = grid.cell(*start);
    const std::optional<NavGridCell> goal_cell = grid.cell(*goal);
    if (!start_cell || !goal_cell || !start_cell->walkable || !goal_cell->walkable) {
        return false;
    }
    if (*start == *goal) {
        return true;
    }

    const NavGridSnapshot& topology = grid.topology();
    const float cell_size = topology.cell_size;
    if (!(cell_size > 0.0F)) {
        return false;
    }

    // A cell-boundary walk rather than point sampling: sampling can step over a
    // one-cell gap between two blocks and report a route through solid ground.
    const float delta_x = to_world.x - from_world.x;
    const float delta_z = to_world.y - from_world.y;
    const std::int32_t step_column = delta_x > 0.0F ? 1 : (delta_x < 0.0F ? -1 : 0);
    const std::int32_t step_row = delta_z > 0.0F ? 1 : (delta_z < 0.0F ? -1 : 0);

    // Distance along the segment, in units of the segment's own length, to the
    // next boundary in each axis and between successive boundaries.
    constexpr float never = std::numeric_limits<float>::infinity();
    const auto axis_entry = [&](const float origin, const float direction, const float minimum,
                                const std::int32_t index, const std::int32_t step, float& next,
                                float& stride) {
        if (step == 0) {
            next = never;
            stride = never;
            return;
        }
        const float lower = minimum + static_cast<float>(index) * cell_size;
        const float boundary = step > 0 ? lower + cell_size : lower;
        next = (boundary - origin) / direction;
        stride = cell_size / std::abs(direction);
    };

    float next_x = never;
    float next_z = never;
    float stride_x = never;
    float stride_z = never;
    axis_entry(from_world.x, delta_x, topology.bounds.x, start->column, step_column, next_x,
               stride_x);
    axis_entry(from_world.y, delta_z, topology.bounds.z, start->row, step_row, next_z, stride_z);

    NavCell current = *start;
    float current_elevation = start_cell->elevation;
    const auto walkable_at = [&](const NavCell cell, float& elevation) -> bool {
        const std::optional<NavGridCell> found = grid.cell(cell);
        if (!found || !found->walkable) {
            return false;
        }
        elevation = found->elevation;
        return true;
    };

    // Bounded by the cells the segment can cross, so a malformed input cannot
    // spin here.
    const std::size_t maximum_steps =
        static_cast<std::size_t>(topology.columns) + static_cast<std::size_t>(topology.rows) + 2U;
    for (std::size_t visited = 0; visited < maximum_steps; ++visited) {
        if (current == *goal) {
            return true;
        }
        constexpr float corner_epsilon = 0.0001F;
        const bool crosses_corner =
            step_column != 0 && step_row != 0 && std::abs(next_x - next_z) <= corner_epsilon;
        if (crosses_corner) {
            // The segment passes exactly through a lattice corner. Both
            // orthogonal cells must be usable, which is the same rule that
            // stops the search cutting a corner between two blocks.
            float ignored = 0.0F;
            const NavCell beside_x{current.column + step_column, current.row};
            const NavCell beside_z{current.column, current.row + step_row};
            if (!walkable_at(beside_x, ignored) || !walkable_at(beside_z, ignored)) {
                return false;
            }
            current = {current.column + step_column, current.row + step_row};
            next_x += stride_x;
            next_z += stride_z;
        } else if (next_x < next_z) {
            current = {current.column + step_column, current.row};
            next_x += stride_x;
        } else {
            current = {current.column, current.row + step_row};
            next_z += stride_z;
        }

        float elevation = 0.0F;
        if (!walkable_at(current, elevation)) {
            return false;
        }
        if (std::abs(elevation - current_elevation) > topology.max_step_height) {
            return false;
        }
        current_elevation = elevation;
    }
    return false;
}

NavPathResult find_nav_path_world(const NavGrid& grid, const Vec2 start_world,
                                  const Vec2 goal_world) {
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
