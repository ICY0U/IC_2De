#include "ic2d/ground_map.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace ic2d {
namespace {

[[nodiscard]] bool finite(const RectXZ& bounds) noexcept {
    return std::isfinite(bounds.x) && std::isfinite(bounds.z) &&
           std::isfinite(bounds.width) && std::isfinite(bounds.depth);
}

[[nodiscard]] bool valid(const RectXZ& bounds) noexcept {
    return finite(bounds) && bounds.width > 0.0F && bounds.depth > 0.0F;
}

[[nodiscard]] bool contains(const RectXZ& bounds, const Vec2& point) noexcept {
    return point.x >= bounds.x && point.x <= bounds.x + bounds.width &&
           point.y >= bounds.z && point.y <= bounds.z + bounds.depth;
}

[[nodiscard]] bool overlaps(
    const RectXZ& bounds,
    const Vec2& centre,
    const Vec2& half_extents
) noexcept {
    const float actor_left = centre.x - half_extents.x;
    const float actor_right = centre.x + half_extents.x;
    const float actor_near = centre.y - half_extents.y;
    const float actor_far = centre.y + half_extents.y;
    return actor_right > bounds.x && actor_left < bounds.x + bounds.width &&
           actor_far > bounds.z && actor_near < bounds.z + bounds.depth;
}

} // namespace

GroundMap::GroundMap(GroundMapDefinition definition)
    : walkable_bounds_{definition.walkable_bounds},
      max_step_height_{definition.max_step_height},
      areas_{std::move(definition.areas)} {
    if (!valid(walkable_bounds_) || !std::isfinite(max_step_height_) || max_step_height_ < 0.0F) {
        throw std::invalid_argument{"Ground map bounds and step height must be valid."};
    }
    for (const GroundArea& area : areas_) {
        if (!valid(area.bounds) || !std::isfinite(area.elevation)) {
            throw std::invalid_argument{"Ground areas must have valid bounds and elevation."};
        }
    }
}

float GroundMap::elevation_at(const Vec2& ground_position) const noexcept {
    float elevation = 0.0F;
    for (const GroundArea& area : areas_) {
        if (area.kind == GroundAreaKind::elevation && contains(area.bounds, ground_position)) {
            elevation = std::max(elevation, area.elevation);
        }
    }
    return elevation;
}

GroundMoveResult GroundMap::move(
    const Vec3& start,
    const Vec2& desired_ground_position,
    const Vec2& half_extents
) const {
    const bool finite_input = std::isfinite(start.x) && std::isfinite(start.y) &&
                              std::isfinite(start.z) && std::isfinite(desired_ground_position.x) &&
                              std::isfinite(desired_ground_position.y) && std::isfinite(half_extents.x) &&
                              std::isfinite(half_extents.y);
    if (!finite_input || half_extents.x < 0.0F || half_extents.y < 0.0F ||
        half_extents.x * 2.0F > walkable_bounds_.width ||
        half_extents.y * 2.0F > walkable_bounds_.depth) {
        throw std::invalid_argument{"Ground movement requires finite positions and valid half extents."};
    }

    const float minimum_x = walkable_bounds_.x + half_extents.x;
    const float maximum_x = walkable_bounds_.x + walkable_bounds_.width - half_extents.x;
    const float minimum_z = walkable_bounds_.z + half_extents.y;
    const float maximum_z = walkable_bounds_.z + walkable_bounds_.depth - half_extents.y;
    Vec2 resolved{
        std::clamp(start.x, minimum_x, maximum_x),
        std::clamp(start.z, minimum_z, maximum_z),
    };
    float current_elevation = elevation_at(resolved);

    const auto blocked = [this, &half_extents](const Vec2& position) {
        return std::ranges::any_of(areas_, [&position, &half_extents](const GroundArea& area) {
            return area.kind == GroundAreaKind::solid && overlaps(area.bounds, position, half_extents);
        });
    };
    const auto can_enter = [this](const Vec2& position, const float from_elevation) {
        return std::abs(elevation_at(position) - from_elevation) <= max_step_height_;
    };

    GroundMoveResult result{};
    const float desired_x = std::clamp(desired_ground_position.x, minimum_x, maximum_x);
    Vec2 x_candidate{desired_x, resolved.y};
    if (!blocked(x_candidate) && can_enter(x_candidate, current_elevation)) {
        resolved.x = desired_x;
        current_elevation = elevation_at(resolved);
    } else {
        result.blocked_x = true;
    }
    result.blocked_x = result.blocked_x || desired_x != desired_ground_position.x;

    const float desired_z = std::clamp(desired_ground_position.y, minimum_z, maximum_z);
    Vec2 z_candidate{resolved.x, desired_z};
    if (!blocked(z_candidate) && can_enter(z_candidate, current_elevation)) {
        resolved.y = desired_z;
        current_elevation = elevation_at(resolved);
    } else {
        result.blocked_z = true;
    }
    result.blocked_z = result.blocked_z || desired_z != desired_ground_position.y;

    result.position = {resolved.x, current_elevation, resolved.y};
    for (const GroundArea& area : areas_) {
        if (area.kind == GroundAreaKind::trigger && overlaps(area.bounds, resolved, half_extents)) {
            result.trigger_tag = area.tag;
            break;
        }
    }
    return result;
}

} // namespace ic2d
