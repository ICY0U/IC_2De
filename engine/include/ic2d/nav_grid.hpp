#pragma once

#include "ic2d/ground_map.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace ic2d {

struct NavCell {
    std::int32_t column{0};
    std::int32_t row{0};

    auto operator<=>(const NavCell&) const = default;
};

struct NavGridBakeSettings {
    float cell_size{0.0F};
    Vec2 agent_half_extents{};
};

struct NavGridCell {
    NavCell cell{};
    Vec2 center{};
    float elevation{0.0F};
    bool walkable{false};
};

struct NavGridNeighbor {
    NavCell cell{};
    // Physical X/Z distance in world units, ready for a later search cost.
    float distance{0.0F};
};

struct NavGridSnapshot {
    RectXZ bounds{};
    float cell_size{0.0F};
    Vec2 agent_half_extents{};
    float max_step_height{0.0F};
    std::int32_t columns{0};
    std::int32_t rows{0};
    std::size_t walkable_cell_count{0};
    std::size_t blocked_cell_count{0};
    // Canonical row-major order: row * columns + column.
    std::vector<NavGridCell> cells;
};

// Immutable dense X/Z navigation data baked from one GroundMap definition for
// one agent footprint. Solid overlap is a structural hard block; elevation is
// retained as a 2.5D heightfield and max-step rules are enforced by neighbors().
// Search, path caching, dynamic obstacles, and local avoidance are deliberately
// outside this module.
class NavGrid final {
public:
    NavGrid(const GroundMapDefinition& ground, const NavGridBakeSettings& settings);
    ~NavGrid();

    NavGrid(const NavGrid&) = delete;
    NavGrid& operator=(const NavGrid&) = delete;
    NavGrid(NavGrid&&) noexcept;
    NavGrid& operator=(NavGrid&&) noexcept;

    // World bounds are half-open on the far X/Z edge.
    [[nodiscard]] std::optional<NavCell> cell_at(Vec2 world_position) const noexcept;
    [[nodiscard]] std::optional<NavGridCell> cell(NavCell index) const noexcept;

    // Deterministic clockwise order beginning at negative Z. Non-walkable
    // cells never appear, diagonal corner cutting is rejected, and height
    // changes above max_step_height have no edge in the graph.
    [[nodiscard]] std::vector<NavGridNeighbor> neighbors(NavCell source) const;
    [[nodiscard]] NavGridSnapshot snapshot() const;

    // Borrowed view of the same topology, for callers that only read it and
    // would otherwise pay a full cell-vector copy per call. The reference is
    // valid for the lifetime of this grid; results derived from it must still
    // be copied rather than retaining a pointer into it.
    [[nodiscard]] const NavGridSnapshot& topology() const noexcept;

    // Identifier of the connected region a cell belongs to under the exact
    // neighbors() contract, baked once with the grid. Blocked and
    // out-of-bounds cells are zero; every walkable cell is one or greater.
    //
    // Two cells with different identifiers have no route between them, which
    // lets a search reject an impossible request outright instead of expanding
    // an entire region to rediscover that every time it is asked.
    [[nodiscard]] std::uint32_t component_of(NavCell cell) const noexcept;
    [[nodiscard]] std::size_t component_count() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
