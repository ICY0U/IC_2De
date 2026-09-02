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

// Pushing out of one solid can seat a body inside a neighbouring one, so a
// few passes settle a corner. The bound keeps a pathological arrangement from
// costing an unbounded amount of work in a fixed tick.
constexpr int depenetration_passes = 4;

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
        switch (area.kind) {
        case GroundAreaKind::solid:
            solid_areas_.push_back(area);
            break;
        case GroundAreaKind::elevation:
            elevation_areas_.push_back(area);
            break;
        case GroundAreaKind::trigger:
            trigger_areas_.push_back(area);
            break;
        }
    }
}

float GroundMap::elevation_at(const Vec2& ground_position) const noexcept {
    float elevation = 0.0F;
    for (const GroundArea& area : elevation_areas_) {
        if (contains(area.bounds, ground_position)) {
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

    const auto can_enter = [this](const Vec2& position, const float from_elevation) {
        return std::abs(elevation_at(position) - from_elevation) <= max_step_height_;
    };

    // Resolve the nearest solid face crossed by one axis. Destination-only
    // overlap tests let fast movement tunnel through thin geometry; swept axis
    // contact preserves the map's deliberate X-then-Z sliding semantics.
    const auto sweep_x = [this, &half_extents](
                             const Vec2& start_position,
                             const float desired_x,
                             bool& blocked_axis) {
        float resolved_x = desired_x;
        for (const GroundArea& area : solid_areas_) {
            const bool overlaps_z =
                start_position.y + half_extents.y > area.bounds.z &&
                start_position.y - half_extents.y < area.bounds.z + area.bounds.depth;
            if (!overlaps_z) {
                continue;
            }
            if (desired_x > start_position.x) {
                const float contact = area.bounds.x - half_extents.x;
                if (start_position.x <= contact && desired_x > contact) {
                    resolved_x = std::min(resolved_x, contact);
                    blocked_axis = true;
                }
            } else if (desired_x < start_position.x) {
                const float contact = area.bounds.x + area.bounds.width + half_extents.x;
                if (start_position.x >= contact && desired_x < contact) {
                    resolved_x = std::max(resolved_x, contact);
                    blocked_axis = true;
                }
            }
        }
        return resolved_x;
    };
    const auto sweep_z = [this, &half_extents](
                             const Vec2& start_position,
                             const float desired_z,
                             bool& blocked_axis) {
        float resolved_z = desired_z;
        for (const GroundArea& area : solid_areas_) {
            const bool overlaps_x =
                start_position.x + half_extents.x > area.bounds.x &&
                start_position.x - half_extents.x < area.bounds.x + area.bounds.width;
            if (!overlaps_x) {
                continue;
            }
            if (desired_z > start_position.y) {
                const float contact = area.bounds.z - half_extents.y;
                if (start_position.y <= contact && desired_z > contact) {
                    resolved_z = std::min(resolved_z, contact);
                    blocked_axis = true;
                }
            } else if (desired_z < start_position.y) {
                const float contact = area.bounds.z + area.bounds.depth + half_extents.y;
                if (start_position.y >= contact && desired_z < contact) {
                    resolved_z = std::max(resolved_z, contact);
                    blocked_axis = true;
                }
            }
        }
        return resolved_z;
    };

    GroundMoveResult result{};
    const float clamped_x = std::clamp(desired_ground_position.x, minimum_x, maximum_x);
    const float desired_x = sweep_x(resolved, clamped_x, result.blocked_x);
    Vec2 x_candidate{desired_x, resolved.y};
    if (can_enter(x_candidate, current_elevation)) {
        resolved.x = desired_x;
        current_elevation = elevation_at(resolved);
    } else {
        result.blocked_x = true;
    }
    result.blocked_x = result.blocked_x || clamped_x != desired_ground_position.x;

    const float clamped_z = std::clamp(desired_ground_position.y, minimum_z, maximum_z);
    const float desired_z = sweep_z(resolved, clamped_z, result.blocked_z);
    Vec2 z_candidate{resolved.x, desired_z};
    if (can_enter(z_candidate, current_elevation)) {
        resolved.y = desired_z;
        current_elevation = elevation_at(resolved);
    } else {
        result.blocked_z = true;
    }
    result.blocked_z = result.blocked_z || clamped_z != desired_ground_position.y;

    // The swept faces above only stop a body from crossing into a solid; they
    // deliberately ignore a body that is already inside one, so nothing ever
    // pushes it back out. A dense crowd shoves actors into geometry from
    // angles a per-axis sweep cannot catch, and a single actor that gets in
    // that way is then free to walk through walls indefinitely. Resolving any
    // residual overlap along its shallowest axis closes that hole for good.
    for (int pass = 0; pass < depenetration_passes; ++pass) {
        bool corrected = false;
        for (const GroundArea& area : solid_areas_) {
            if (!overlaps(area.bounds, resolved, half_extents)) {
                continue;
            }
            const float exit_west = area.bounds.x - half_extents.x - resolved.x;
            const float exit_east =
                area.bounds.x + area.bounds.width + half_extents.x - resolved.x;
            const float exit_north = area.bounds.z - half_extents.y - resolved.y;
            const float exit_south =
                area.bounds.z + area.bounds.depth + half_extents.y - resolved.y;
            const float shift_x = std::abs(exit_west) <= std::abs(exit_east)
                                      ? exit_west
                                      : exit_east;
            const float shift_z = std::abs(exit_north) <= std::abs(exit_south)
                                      ? exit_north
                                      : exit_south;
            if (std::abs(shift_x) <= std::abs(shift_z)) {
                resolved.x += shift_x;
                result.blocked_x = true;
            } else {
                resolved.y += shift_z;
                result.blocked_z = true;
            }
            corrected = true;
        }
        if (!corrected) {
            break;
        }
    }
    resolved.x = std::clamp(resolved.x, minimum_x, maximum_x);
    resolved.y = std::clamp(resolved.y, minimum_z, maximum_z);
    current_elevation = elevation_at(resolved);

    result.position = {resolved.x, current_elevation, resolved.y};
    for (const GroundArea& area : trigger_areas_) {
        if (overlaps(area.bounds, resolved, half_extents)) {
            result.trigger_tag = area.tag;
            break;
        }
    }
    return result;
}

} // namespace ic2d
