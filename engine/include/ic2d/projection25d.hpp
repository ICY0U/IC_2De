#pragma once

#include "ic2d/types.hpp"

namespace ic2d {

// Orthographic 2.5D camera. The ground plane is X/Z and +Y is elevation.
// Pitch is measured down from the horizon: 0 is side-on, 90 is top-down.
struct Camera25DState {
    Vec3 focus{};
    float yaw_degrees{0.0F};
    float pitch_degrees{50.0F};
    float pixels_per_world_unit{1.0F};
    float zoom{1.0F};
};

struct ProjectedPoint25D {
    Vec2 position{};
    float depth{0.0F};
};

[[nodiscard]] bool valid(const Camera25DState& camera) noexcept;

// Projects a world point relative to the camera focus. The returned position is
// canvas-centred; depth is suitable for back-to-front billboard ordering.
[[nodiscard]] ProjectedPoint25D project_world_point(
    const Vec3& world_position,
    const Camera25DState& camera
) noexcept;

// Converts screen-relative ground input (right, forward) into an X/Z direction.
[[nodiscard]] Vec3 camera_ground_direction_to_world(
    const Vec2& camera_direction,
    const Camera25DState& camera
) noexcept;

} // namespace ic2d
