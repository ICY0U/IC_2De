#pragma once

#include "ic2d/types.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace ic2d {

enum class GroundAreaKind {
    solid,
    elevation,
    trigger,
};

struct GroundArea {
    RectXZ bounds{};
    GroundAreaKind kind{GroundAreaKind::solid};
    float elevation{0.0F};
    std::uint32_t tag{0};
};

struct GroundMapDefinition {
    RectXZ walkable_bounds{};
    float max_step_height{0.0F};
    std::vector<GroundArea> areas;
};

struct GroundMoveResult {
    Vec3 position{};
    bool blocked_x{false};
    bool blocked_z{false};
    std::optional<std::uint32_t> trigger_tag{};
};

// Resolves an actor footprint on the X/Z ground plane. Elevation areas are
// discrete floors; desired_ground_position stores X in .x and Z in .y.
class GroundMap final {
public:
    explicit GroundMap(GroundMapDefinition definition);

    [[nodiscard]] float elevation_at(const Vec2& ground_position) const noexcept;
    [[nodiscard]] GroundMoveResult move(
        const Vec3& start,
        const Vec2& desired_ground_position,
        const Vec2& half_extents
    ) const;

private:
    RectXZ walkable_bounds_{};
    float max_step_height_{0.0F};
    std::vector<GroundArea> areas_;
};

} // namespace ic2d
