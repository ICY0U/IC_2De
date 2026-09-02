#pragma once

#include "ic2d/projection25d.hpp"
#include "ic2d/types.hpp"

namespace ic2d {

// A development view that can look away from the gameplay camera.
//
// While attached it is exactly the gameplay camera, so opening the editor
// changes nothing about what is on screen. The first pan, zoom, or framing
// request detaches it, after which it owns focus and zoom alone: yaw, pitch,
// and pixel scale keep coming from the running scene. That split is deliberate.
// Movement and aim are camera-relative through yaw, so a detached view can look
// anywhere without changing what a gameplay input means.
//
// The type is pure geometry with no backend or panel dependency, so the mapping
// from a canvas drag to a world offset is testable without a window.
class EditorCamera final {
public:
    // Zoom is bounded so a stray wheel burst cannot leave the view at a scale
    // where nothing is locatable.
    static constexpr float minimum_zoom = 0.2F;
    static constexpr float maximum_zoom = 8.0F;

    [[nodiscard]] bool detached() const noexcept { return detached_; }

    // Returns the frame presentation and pointer math should use. Attached,
    // that is the gameplay camera unchanged.
    [[nodiscard]] Camera25DState resolve(const Camera25DState& gameplay) const noexcept;

    // Resumes following the gameplay camera.
    void attach() noexcept { detached_ = false; }

    // Drags the world under the pointer. The delta is in canvas pixels, which
    // is what the viewport panel measures.
    void pan(Vec2 canvas_delta, const Camera25DState& gameplay) noexcept;

    // Wheel notches: positive zooms in. Zooming is multiplicative so each notch
    // covers the same proportion of the view at any scale.
    void zoom(float notches, const Camera25DState& gameplay) noexcept;

    // Centres the view on a world position without changing zoom.
    void frame(Vec3 world_position, const Camera25DState& gameplay) noexcept;

private:
    // Seeds focus and zoom from the gameplay camera the first time the view is
    // moved, so detaching never jumps the image.
    void detach_from(const Camera25DState& gameplay) noexcept;

    bool detached_{false};
    Vec3 focus_{};
    float zoom_{1.0F};
};

} // namespace ic2d
