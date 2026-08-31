#pragma once

namespace ic2d {

struct CanvasViewport {
    float x{0.0F};
    float y{0.0F};
    float width{0.0F};
    float height{0.0F};
    float scale{0.0F};
};

// Fits a virtual canvas inside an output while preferring integer upscaling.
[[nodiscard]] CanvasViewport compute_canvas_viewport(
    int output_width,
    int output_height,
    int canvas_width,
    int canvas_height
) noexcept;

} // namespace ic2d
