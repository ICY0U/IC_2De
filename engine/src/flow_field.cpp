#include "ic2d/flow_field.hpp"

#include <cmath>
#include <limits>
#include <queue>
#include <vector>

namespace ic2d {
namespace {

struct OpenEntry {
    float cost{0.0F};
    NavCell cell{};
};

// priority_queue yields the largest element, so every key is inverted. Row and
// column settle equal costs, giving one repeatable field for a symmetric map.
struct WorseEntry {
    [[nodiscard]] bool operator()(const OpenEntry& left, const OpenEntry& right) const noexcept {
        if (left.cost != right.cost) {
            return left.cost > right.cost;
        }
        if (left.cell.row != right.cell.row) {
            return left.cell.row > right.cell.row;
        }
        return left.cell.column > right.cell.column;
    }
};

} // namespace

bool FlowField::contains(const NavCell cell) const noexcept {
    return cell.column >= 0 && cell.row >= 0 && cell.column < columns_ && cell.row < rows_;
}

std::size_t FlowField::offset(const NavCell cell) const noexcept {
    return static_cast<std::size_t>(cell.row) * static_cast<std::size_t>(columns_) +
           static_cast<std::size_t>(cell.column);
}

bool FlowField::rebuild(const NavGrid& grid, const NavCell goal, const Vec2 goal_position) {
    const NavGridSnapshot& topology = grid.topology();
    bounds_ = topology.bounds;
    cell_size_ = topology.cell_size;
    columns_ = topology.columns;
    rows_ = topology.rows;
    built_ = false;
    reachable_cell_count_ = 0;

    const std::size_t cell_count = topology.cells.size();
    cost_.assign(cell_count, std::numeric_limits<float>::infinity());
    direction_.assign(cell_count, Vec2{});
    if (cell_count == 0 || !contains(goal) || !topology.cells[offset(goal)].walkable) {
        return false;
    }

    // Dijkstra outward from the goal. Edges come from the grid's own neighbor
    // contract, so the field inherits hard blocking, step limits and the
    // no-corner-cutting rule rather than re-deriving them and disagreeing.
    std::priority_queue<OpenEntry, std::vector<OpenEntry>, WorseEntry> open;
    cost_[offset(goal)] = 0.0F;
    open.push({.cost = 0.0F, .cell = goal});

    while (!open.empty()) {
        const OpenEntry current = open.top();
        open.pop();
        const std::size_t current_offset = offset(current.cell);
        if (current.cost > cost_[current_offset]) {
            continue;
        }
        for (const NavGridNeighbor& neighbor : grid.neighbors(current.cell)) {
            // Edges are symmetric, so a route found outward from the goal is a
            // route inward to it.
            const std::size_t neighbor_offset = offset(neighbor.cell);
            const float candidate = current.cost + neighbor.distance;
            if (candidate < cost_[neighbor_offset]) {
                cost_[neighbor_offset] = candidate;
                open.push({.cost = candidate, .cell = neighbor.cell});
            }
        }
    }

    // Cells the search could not reach are walled off from the goal. Giving
    // that whole region one routed destination funnels every actor in it to a
    // single point, which is what turns a crowd meant to surround its target
    // into one heap on whichever side happens to be nearest. Instead each such
    // cell takes its own straight-line distance to the goal as its potential.
    // Descending that leads an actor to the closest point of the barrier it is
    // already standing against, rather than around the outside to a global
    // minimum, so the crowd distributes across every facing wall.
    for (std::size_t index = 0; index < cell_count; ++index) {
        const NavGridCell& cell = topology.cells[index];
        if (!cell.walkable || std::isfinite(cost_[index])) {
            continue;
        }
        const float delta_x = cell.center.x - goal_position.x;
        const float delta_z = cell.center.y - goal_position.y;
        cost_[index] = std::sqrt(delta_x * delta_x + delta_z * delta_z);
    }

    // Take each cell's direction from the gradient of the cost field rather
    // than from whichever neighbour happens to be cheapest.
    //
    // Steepest-neighbour has two faults that a crowd makes obvious. It admits
    // only the eight neighbour directions, and where several neighbours tie it
    // resolves them by their fixed iteration order, so each side of the goal
    // acquires a different diagonal preference. Actors approaching from
    // opposite sides then circulate the same way round instead of converging,
    // and the crowd slowly winds onto one side of the target. A central
    // difference has no preferred direction and is continuous, so opposite
    // sides mirror each other exactly.
    const auto sampled_cost = [&](const NavCell cell, const float fallback) {
        if (!contains(cell)) {
            return fallback;
        }
        const float value = cost_[offset(cell)];
        // A blocked or unreached neighbour reads as the cell's own cost, which
        // leaves the gradient pointing along the obstacle rather than into it.
        return std::isfinite(value) ? value : fallback;
    };
    for (std::int32_t row = 0; row < rows_; ++row) {
        for (std::int32_t column = 0; column < columns_; ++column) {
            const NavCell cell{column, row};
            const std::size_t cell_offset = offset(cell);
            const float own_cost = cost_[cell_offset];
            if (!std::isfinite(own_cost)) {
                continue;
            }
            ++reachable_cell_count_;
            if (cell == goal) {
                // Arrived: no direction, and no spurious gradient from the
                // grid edge when the goal sits against one.
                continue;
            }
            const float west = sampled_cost({column - 1, row}, own_cost);
            const float east = sampled_cost({column + 1, row}, own_cost);
            const float north = sampled_cost({column, row - 1}, own_cost);
            const float south = sampled_cost({column, row + 1}, own_cost);
            // Cost rises away from the goal, so travel is the negative gradient.
            const Vec2 downhill{west - east, north - south};
            const float length = std::sqrt(downhill.x * downhill.x + downhill.y * downhill.y);
            if (length > 0.0F) {
                direction_[cell_offset] = {downhill.x / length, downhill.y / length};
            }
        }
    }

    goal_ = goal;
    built_ = true;
    return true;
}

bool FlowField::built() const noexcept { return built_; }

NavCell FlowField::goal() const noexcept { return goal_; }

std::size_t FlowField::reachable_cell_count() const noexcept { return reachable_cell_count_; }

Vec2 FlowField::direction_at(const Vec2 world_position) const noexcept {
    if (!built_ || !(cell_size_ > 0.0F) || !std::isfinite(world_position.x) ||
        !std::isfinite(world_position.y)) {
        return {};
    }
    // Sample relative to cell centres, so the four cells being blended are the
    // ones actually surrounding the position.
    const float sample_x = (world_position.x - bounds_.x) / cell_size_ - 0.5F;
    const float sample_z = (world_position.y - bounds_.z) / cell_size_ - 0.5F;
    const auto base_column = static_cast<std::int32_t>(std::floor(sample_x));
    const auto base_row = static_cast<std::int32_t>(std::floor(sample_z));
    const float fraction_x = sample_x - static_cast<float>(base_column);
    const float fraction_z = sample_z - static_cast<float>(base_row);

    Vec2 blended{};
    for (std::int32_t row_step = 0; row_step <= 1; ++row_step) {
        for (std::int32_t column_step = 0; column_step <= 1; ++column_step) {
            const NavCell cell{base_column + column_step, base_row + row_step};
            if (!contains(cell)) {
                continue;
            }
            const Vec2 direction = direction_[offset(cell)];
            if (direction.x == 0.0F && direction.y == 0.0F) {
                // A blocked or arrived cell contributes nothing rather than
                // dragging the blend toward a direction it does not have.
                continue;
            }
            const float weight_x = column_step == 0 ? 1.0F - fraction_x : fraction_x;
            const float weight_z = row_step == 0 ? 1.0F - fraction_z : fraction_z;
            const float weight = weight_x * weight_z;
            blended.x += direction.x * weight;
            blended.y += direction.y * weight;
        }
    }
    const float length = std::sqrt(blended.x * blended.x + blended.y * blended.y);
    if (length > 0.0F) {
        return {blended.x / length, blended.y / length};
    }
    // Nothing usable nearby: fall back to the cell the position sits in.
    const NavCell cell{
        static_cast<std::int32_t>(std::floor((world_position.x - bounds_.x) / cell_size_)),
        static_cast<std::int32_t>(std::floor((world_position.y - bounds_.z) / cell_size_)),
    };
    return contains(cell) ? direction_[offset(cell)] : Vec2{};
}

float FlowField::cost_at(const Vec2 world_position) const noexcept {
    if (!built_ || !(cell_size_ > 0.0F) || !std::isfinite(world_position.x) ||
        !std::isfinite(world_position.y)) {
        return std::numeric_limits<float>::infinity();
    }
    const NavCell cell{
        static_cast<std::int32_t>(std::floor((world_position.x - bounds_.x) / cell_size_)),
        static_cast<std::int32_t>(std::floor((world_position.y - bounds_.z) / cell_size_)),
    };
    return contains(cell) ? cost_[offset(cell)] : std::numeric_limits<float>::infinity();
}

} // namespace ic2d
