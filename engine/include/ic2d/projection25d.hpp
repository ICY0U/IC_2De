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

struct GroundMovement25D {
    Vec2 world_direction{};
    Vec2 presentation_direction{};
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

// Inverse yaw transform used when world-space aim must select a screen-facing
// animation direction.
[[nodiscard]] Vec2 world_ground_direction_to_camera(
    const Vec2& world_direction,
    const Camera25DState& camera
) noexcept;

// Resolves a virtual-canvas point into the world position on the ground plane
// at a given elevation. This is the full inverse of the ground projection, so a
// pointer becomes a place rather than only a bearing: aiming needs the range as
// well as the direction. An unusable camera or canvas returns the origin.
[[nodiscard]] Vec3 canvas_ground_point(
    Vec2 canvas_point,
    float ground_elevation,
    int canvas_width,
    int canvas_height,
    const Camera25DState& camera
) noexcept;

// Resolves a virtual-canvas pointer into a normalized X/Z direction from a
// world origin. This is the inverse of the camera's ground projection at the
// origin's elevation; invalid inputs or a pointer exactly on the origin return
// zero rather than manufacturing an aim direction.
[[nodiscard]] Vec2 canvas_ground_direction(
    const Vec3& world_origin,
    const Vec2& canvas_point,
    int canvas_width,
    int canvas_height,
    const Camera25DState& camera
) noexcept;

// A billboard sprite carries one position and one sort depth, so it can only
// represent geometry that occupies a single depth. A wall running along the
// depth axis does not: both its screen position and its ordering against other
// sprites vary continuously along its length. The engine therefore resolves a
// depth-spanning sprite into a run of overlapping slices, each submitted at its
// own depth, which is what lets one authored wall sort correctly against actors
// standing anywhere beside it.
struct DepthSlicePlan {
    int count{1};
    float step{0.0F};           // World depth between neighbouring centres.
    float first_center_z{0.0F}; // Centre of the nearest slice.
};

// Chooses the fewest slices whose projected heights still overlap, so the run
// reads as one unbroken surface rather than a staircase. A span at or below
// zero, or an unusable camera pitch, plans the single slice a plain sprite
// already draws.
[[nodiscard]] DepthSlicePlan plan_depth_slices(
    float center_z,
    float depth_span,
    float sprite_height,
    float pitch_degrees
) noexcept;

// Converts an offset measured on the canvas into the world X/Z offset that
// produced it. This is the inverse of the ground projection: screen X is the
// camera's right axis at the current scale and screen Y is its forward axis
// foreshortened by pitch, so the same pixel count covers more world depth than
// width. Exact on the ground plane, which is where placements are dragged and
// where the view is panned. An unusable camera returns a zero offset.
[[nodiscard]] Vec3 canvas_ground_offset_to_world(
    Vec2 canvas_offset,
    const Camera25DState& camera
) noexcept;

// Keeps screen-relative facing separate from the camera-rotated world vector
// used by simulation.
[[nodiscard]] GroundMovement25D camera_ground_movement(
    Vec2 camera_direction,
    const Camera25DState& camera
) noexcept;

} // namespace ic2d
