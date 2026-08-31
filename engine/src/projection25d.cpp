#include "ic2d/projection25d.hpp"

#include <cmath>
#include <numbers>

namespace ic2d {
namespace {

[[nodiscard]] float radians(const float degrees) noexcept {
    return degrees * std::numbers::pi_v<float> / 180.0F;
}

} // namespace

bool valid(const Camera25DState& camera) noexcept {
    return std::isfinite(camera.focus.x) && std::isfinite(camera.focus.y) &&
           std::isfinite(camera.focus.z) && std::isfinite(camera.yaw_degrees) &&
           std::isfinite(camera.pitch_degrees) && camera.pitch_degrees > 0.0F &&
           camera.pitch_degrees <= 90.0F && std::isfinite(camera.pixels_per_world_unit) &&
           camera.pixels_per_world_unit > 0.0F && std::isfinite(camera.zoom) && camera.zoom > 0.0F;
}

ProjectedPoint25D project_world_point(
    const Vec3& world_position,
    const Camera25DState& camera
) noexcept {
    if (!valid(camera)) {
        return {};
    }

    const float delta_x = world_position.x - camera.focus.x;
    const float delta_y = world_position.y - camera.focus.y;
    const float delta_z = world_position.z - camera.focus.z;
    const float yaw = radians(camera.yaw_degrees);
    const float pitch = radians(camera.pitch_degrees);
    const float right = std::cos(yaw) * delta_x - std::sin(yaw) * delta_z;
    const float forward = std::sin(yaw) * delta_x + std::cos(yaw) * delta_z;
    const float scale = camera.pixels_per_world_unit * camera.zoom;

    return {
        .position = {
            right * scale,
            (forward * std::sin(pitch) - delta_y * std::cos(pitch)) * scale,
        },
        .depth = forward,
    };
}

Vec3 camera_ground_direction_to_world(
    const Vec2& camera_direction,
    const Camera25DState& camera
) noexcept {
    if (!std::isfinite(camera.yaw_degrees) || !std::isfinite(camera_direction.x) ||
        !std::isfinite(camera_direction.y)) {
        return {};
    }

    const float yaw = radians(camera.yaw_degrees);
    return {
        .x = std::cos(yaw) * camera_direction.x + std::sin(yaw) * camera_direction.y,
        .y = 0.0F,
        .z = -std::sin(yaw) * camera_direction.x + std::cos(yaw) * camera_direction.y,
    };
}

} // namespace ic2d
