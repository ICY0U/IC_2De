#include "ic2d/nav_grid.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ic2d {
namespace {

[[nodiscard]] bool finite(const Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool finite(const RectXZ& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.z) &&
           std::isfinite(value.width) && std::isfinite(value.depth);
}

[[nodiscard]] bool valid(const RectXZ& value) noexcept {
    return finite(value) && value.width > 0.0F && value.depth > 0.0F;
}

[[nodiscard]] bool contains_sample(const RectXZ& bounds, const Vec2 point) noexcept {
    return point.x >= bounds.x && point.x <= bounds.x + bounds.width &&
           point.y >= bounds.z && point.y <= bounds.z + bounds.depth;
}

[[nodiscard]] bool footprint_overlaps(
    const RectXZ& bounds,
    const Vec2 center,
    const Vec2 half_extents
) noexcept {
    return center.x + half_extents.x > bounds.x &&
           center.x - half_extents.x < bounds.x + bounds.width &&
           center.y + half_extents.y > bounds.z &&
           center.y - half_extents.y < bounds.z + bounds.depth;
}

[[nodiscard]] bool footprint_inside(
    const RectXZ& bounds,
    const Vec2 center,
    const Vec2 half_extents
) noexcept {
    return center.x - half_extents.x >= bounds.x &&
           center.x + half_extents.x <= bounds.x + bounds.width &&
           center.y - half_extents.y >= bounds.z &&
           center.y + half_extents.y <= bounds.z + bounds.depth;
}

[[nodiscard]] std::int32_t dimension(const float extent, const float cell_size) {
    const double value = std::ceil(
        static_cast<double>(extent) / static_cast<double>(cell_size));
    if (!std::isfinite(value) || value < 1.0 ||
        value > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        throw std::invalid_argument{"NavGrid dimensions exceed the supported dense grid range."};
    }
    return static_cast<std::int32_t>(value);
}

} // namespace

struct NavGrid::Impl {
    NavGridSnapshot snapshot;
    // Row-major, parallel to snapshot.cells. Zero means blocked.
    std::vector<std::uint32_t> components;
    std::size_t component_count{0};

    [[nodiscard]] bool contains(const NavCell cell) const noexcept {
        return cell.column >= 0 && cell.row >= 0 &&
               cell.column < snapshot.columns && cell.row < snapshot.rows;
    }

    [[nodiscard]] std::size_t offset(const NavCell cell) const noexcept {
        return static_cast<std::size_t>(cell.row) *
                   static_cast<std::size_t>(snapshot.columns) +
               static_cast<std::size_t>(cell.column);
    }

    [[nodiscard]] const NavGridCell* find(const NavCell cell) const noexcept {
        return contains(cell) ? &snapshot.cells[offset(cell)] : nullptr;
    }

    [[nodiscard]] bool can_step(const NavCell source, const NavCell destination) const noexcept {
        const NavGridCell* from = find(source);
        const NavGridCell* to = find(destination);
        return from != nullptr && to != nullptr && from->walkable && to->walkable &&
               std::abs(to->elevation - from->elevation) <= snapshot.max_step_height;
    }
};

NavGrid::NavGrid(
    const GroundMapDefinition& ground,
    const NavGridBakeSettings& settings
) : impl_{std::make_unique<Impl>()} {
    if (!valid(ground.walkable_bounds) || !std::isfinite(ground.max_step_height) ||
        ground.max_step_height < 0.0F || !std::isfinite(settings.cell_size) ||
        !(settings.cell_size > 0.0F) || !finite(settings.agent_half_extents) ||
        settings.agent_half_extents.x < 0.0F || settings.agent_half_extents.y < 0.0F ||
        settings.agent_half_extents.x * 2.0F > ground.walkable_bounds.width ||
        settings.agent_half_extents.y * 2.0F > ground.walkable_bounds.depth) {
        throw std::invalid_argument{"NavGrid requires valid bounds, cell size, step height, and agent clearance."};
    }
    for (const GroundArea& area : ground.areas) {
        if (!valid(area.bounds) || !std::isfinite(area.elevation)) {
            throw std::invalid_argument{"NavGrid ground areas require finite positive bounds and elevation."};
        }
    }

    NavGridSnapshot& snapshot = impl_->snapshot;
    snapshot.bounds = ground.walkable_bounds;
    snapshot.cell_size = settings.cell_size;
    snapshot.agent_half_extents = settings.agent_half_extents;
    snapshot.max_step_height = ground.max_step_height;
    snapshot.columns = dimension(snapshot.bounds.width, snapshot.cell_size);
    snapshot.rows = dimension(snapshot.bounds.depth, snapshot.cell_size);

    const std::size_t columns = static_cast<std::size_t>(snapshot.columns);
    const std::size_t rows = static_cast<std::size_t>(snapshot.rows);
    if (columns > std::numeric_limits<std::size_t>::max() / rows) {
        throw std::invalid_argument{"NavGrid cell count overflows the platform size type."};
    }
    const std::size_t cell_count = columns * rows;
    if (cell_count > snapshot.cells.max_size()) {
        throw std::invalid_argument{"NavGrid cell count exceeds the dense storage limit."};
    }
    snapshot.cells.reserve(cell_count);

    for (std::int32_t row = 0; row < snapshot.rows; ++row) {
        for (std::int32_t column = 0; column < snapshot.columns; ++column) {
            const Vec2 center{
                snapshot.bounds.x +
                    (static_cast<float>(column) + 0.5F) * snapshot.cell_size,
                snapshot.bounds.z +
                    (static_cast<float>(row) + 0.5F) * snapshot.cell_size,
            };
            float elevation = 0.0F;
            bool walkable = footprint_inside(
                snapshot.bounds, center, snapshot.agent_half_extents);
            for (const GroundArea& area : ground.areas) {
                if (area.kind == GroundAreaKind::elevation &&
                    contains_sample(area.bounds, center)) {
                    elevation = std::max(elevation, area.elevation);
                } else if (area.kind == GroundAreaKind::solid &&
                           footprint_overlaps(
                               area.bounds, center, snapshot.agent_half_extents)) {
                    walkable = false;
                }
            }
            snapshot.cells.push_back({
                .cell = {column, row},
                .center = center,
                .elevation = elevation,
                .walkable = walkable,
            });
            if (walkable) {
                ++snapshot.walkable_cell_count;
            } else {
                ++snapshot.blocked_cell_count;
            }
        }
    }

    // Label connected regions once, using the same movement contract the
    // search uses, so component equality and reachability cannot disagree.
    impl_->components.assign(snapshot.cells.size(), 0U);
    std::vector<NavCell> frontier;
    for (const NavGridCell& seed : snapshot.cells) {
        if (!seed.walkable || impl_->components[impl_->offset(seed.cell)] != 0U) {
            continue;
        }
        ++impl_->component_count;
        const auto label = static_cast<std::uint32_t>(impl_->component_count);
        impl_->components[impl_->offset(seed.cell)] = label;
        frontier.clear();
        frontier.push_back(seed.cell);
        while (!frontier.empty()) {
            const NavCell cell = frontier.back();
            frontier.pop_back();
            for (const NavGridNeighbor& neighbor : neighbors(cell)) {
                std::uint32_t& slot = impl_->components[impl_->offset(neighbor.cell)];
                if (slot == 0U) {
                    slot = label;
                    frontier.push_back(neighbor.cell);
                }
            }
        }
    }
}

NavGrid::~NavGrid() = default;
NavGrid::NavGrid(NavGrid&&) noexcept = default;
NavGrid& NavGrid::operator=(NavGrid&&) noexcept = default;

std::optional<NavCell> NavGrid::cell_at(const Vec2 world_position) const noexcept {
    const RectXZ& bounds = impl_->snapshot.bounds;
    if (!finite(world_position) || world_position.x < bounds.x ||
        world_position.y < bounds.z || world_position.x >= bounds.x + bounds.width ||
        world_position.y >= bounds.z + bounds.depth) {
        return std::nullopt;
    }
    const NavCell result{
        static_cast<std::int32_t>(
            std::floor((world_position.x - bounds.x) / impl_->snapshot.cell_size)),
        static_cast<std::int32_t>(
            std::floor((world_position.y - bounds.z) / impl_->snapshot.cell_size)),
    };
    return impl_->contains(result) ? std::optional<NavCell>{result} : std::nullopt;
}

std::optional<NavGridCell> NavGrid::cell(const NavCell index) const noexcept {
    const NavGridCell* found = impl_->find(index);
    return found != nullptr ? std::optional<NavGridCell>{*found} : std::nullopt;
}

std::vector<NavGridNeighbor> NavGrid::neighbors(const NavCell source) const {
    const NavGridCell* source_cell = impl_->find(source);
    if (source_cell == nullptr || !source_cell->walkable) {
        return {};
    }

    struct Direction {
        std::int32_t column;
        std::int32_t row;
    };
    constexpr std::array<Direction, 8> directions{{
        {0, -1},
        {1, -1},
        {1, 0},
        {1, 1},
        {0, 1},
        {-1, 1},
        {-1, 0},
        {-1, -1},
    }};
    constexpr float diagonal_scale = 1.4142135623730950488F;
    std::vector<NavGridNeighbor> result;
    result.reserve(directions.size());
    for (const Direction direction : directions) {
        const NavCell destination{
            source.column + direction.column,
            source.row + direction.row,
        };
        if (!impl_->can_step(source, destination)) {
            continue;
        }
        const bool diagonal = direction.column != 0 && direction.row != 0;
        if (diagonal &&
            (!impl_->can_step(source, {source.column + direction.column, source.row}) ||
             !impl_->can_step(source, {source.column, source.row + direction.row}))) {
            continue;
        }
        result.push_back({
            .cell = destination,
            .distance = impl_->snapshot.cell_size * (diagonal ? diagonal_scale : 1.0F),
        });
    }
    return result;
}

NavGridSnapshot NavGrid::snapshot() const { return impl_->snapshot; }

const NavGridSnapshot& NavGrid::topology() const noexcept { return impl_->snapshot; }

std::uint32_t NavGrid::component_of(const NavCell cell) const noexcept {
    return impl_->contains(cell) ? impl_->components[impl_->offset(cell)] : 0U;
}

std::size_t NavGrid::component_count() const noexcept { return impl_->component_count; }

} // namespace ic2d
