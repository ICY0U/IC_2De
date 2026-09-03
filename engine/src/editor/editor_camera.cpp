#include "ic2d/editor_camera.hpp"

#include <algorithm>
#include <cmath>

namespace ic2d {
namespace {

[[nodiscard]] bool finite(const Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

} // namespace

Camera25DState EditorCamera::resolve(const Camera25DState& gameplay) const noexcept {
    if (!detached_) {
        return gameplay;
    }
    Camera25DState view = gameplay;
    view.focus = focus_;
    view.zoom = zoom_;
    return view;
}

void EditorCamera::detach_from(const Camera25DState& gameplay) noexcept {
    if (detached_) {
        return;
    }
    detached_ = true;
    focus_ = gameplay.focus;
    zoom_ = std::clamp(gameplay.zoom, minimum_zoom, maximum_zoom);
}

void EditorCamera::pan(const Vec2 canvas_delta, const Camera25DState& gameplay) noexcept {
    if (!valid(gameplay) || !finite(canvas_delta) ||
        (canvas_delta.x == 0.0F && canvas_delta.y == 0.0F)) {
        return;
    }
    detach_from(gameplay);
    const Vec3 world_delta = canvas_ground_offset_to_world(canvas_delta, resolve(gameplay));

    // The world follows the pointer, so the focus moves against the drag.
    focus_.x -= world_delta.x;
    focus_.z -= world_delta.z;
}

void EditorCamera::zoom(const float notches, const Camera25DState& gameplay) noexcept {
    if (!valid(gameplay) || !std::isfinite(notches) || notches == 0.0F) {
        return;
    }
    detach_from(gameplay);
    constexpr float step = 1.15F;
    zoom_ = std::clamp(zoom_ * std::pow(step, notches), minimum_zoom, maximum_zoom);
}

void EditorCamera::frame(const Vec3 world_position, const Camera25DState& gameplay) noexcept {
    if (!valid(gameplay) || !std::isfinite(world_position.x) || !std::isfinite(world_position.y) ||
        !std::isfinite(world_position.z)) {
        return;
    }
    detach_from(gameplay);
    // Elevation is deliberately not adopted: the view stays on the ground plane
    // so framing a raised entity does not tilt the horizon away from the scene.
    focus_.x = world_position.x;
    focus_.z = world_position.z;
}

} // namespace ic2d
