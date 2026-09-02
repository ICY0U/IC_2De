#include "ic2d/projection25d.hpp"

#include <algorithm>
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

DepthSlicePlan plan_depth_slices(
    const float center_z,
    const float depth_span,
    const float sprite_height,
    const float pitch_degrees
) noexcept {
    const DepthSlicePlan single{.count = 1, .step = 0.0F, .first_center_z = center_z};
    if (!std::isfinite(center_z) || !std::isfinite(depth_span) || depth_span <= 0.0F ||
        !std::isfinite(sprite_height) || sprite_height <= 0.0F ||
        !std::isfinite(pitch_degrees) || pitch_degrees <= 0.0F || pitch_degrees > 90.0F) {
        return single;
    }

    // One world unit of depth advances the projection by sin(pitch) screen
    // units, so a slice may cover at most that much of its own height before a
    // gap opens. Sixty percent keeps a visible overlap at every pitch.
    const float screen_per_depth = std::sin(radians(pitch_degrees));
    if (screen_per_depth <= 0.0F) {
        return single;
    }
    const float maximum_slice_depth = std::max(4.0F, sprite_height * 0.6F / screen_per_depth);
    const float exact = depth_span / maximum_slice_depth;
    // A very long wall must not be able to ask for an unbounded number of
    // draws; the cap degrades overlap rather than the frame.
    constexpr int maximum_slices = 512;
    const int count = std::clamp(static_cast<int>(std::ceil(exact)), 1, maximum_slices);
    const float step = depth_span / static_cast<float>(count);
    return {
        .count = count,
        .step = step,
        .first_center_z = center_z - depth_span * 0.5F + step * 0.5F,
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

Vec2 world_ground_direction_to_camera(
    const Vec2& world_direction,
    const Camera25DState& camera
) noexcept {
    if (!std::isfinite(camera.yaw_degrees) || !std::isfinite(world_direction.x) ||
        !std::isfinite(world_direction.y)) {
        return {};
    }

    const float yaw = radians(camera.yaw_degrees);
    return {
        .x = std::cos(yaw) * world_direction.x - std::sin(yaw) * world_direction.y,
        .y = std::sin(yaw) * world_direction.x + std::cos(yaw) * world_direction.y,
    };
}

Vec2 canvas_ground_direction(
    const Vec3& world_origin,
    const Vec2& canvas_point,
    const int canvas_width,
    const int canvas_height,
    const Camera25DState& camera
) noexcept {
    if (!valid(camera) || canvas_width <= 0 || canvas_height <= 0 ||
        !std::isfinite(world_origin.x) || !std::isfinite(world_origin.y) ||
        !std::isfinite(world_origin.z) || !std::isfinite(canvas_point.x) ||
        !std::isfinite(canvas_point.y)) {
        return {};
    }

    const ProjectedPoint25D projected_origin = project_world_point(world_origin, camera);
    const float scale = camera.pixels_per_world_unit * camera.zoom;
    const float pitch_scale = std::sin(radians(camera.pitch_degrees)) * scale;
    const Vec2 camera_direction{
        (canvas_point.x - static_cast<float>(canvas_width) * 0.5F -
         projected_origin.position.x) /
            scale,
        (canvas_point.y - static_cast<float>(canvas_height) * 0.5F -
         projected_origin.position.y) /
            pitch_scale,
    };
    const float length = std::sqrt(
        camera_direction.x * camera_direction.x + camera_direction.y * camera_direction.y);
    if (!(length > 0.0001F)) {
        return {};
    }

    const Vec3 world = camera_ground_direction_to_world(
        {camera_direction.x / length, camera_direction.y / length}, camera);
    return {world.x, world.z};
}

GroundMovement25D camera_ground_movement(
    const Vec2 camera_direction,
    const Camera25DState& camera
) noexcept {
    const Vec3 world = camera_ground_direction_to_world(camera_direction, camera);
    return {
        .world_direction = {world.x, world.z},
        .presentation_direction = camera_direction,
    };
}

} // namespace ic2d
