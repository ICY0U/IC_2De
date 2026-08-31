#include "ic2d/presentation.hpp"

#include <algorithm>
#include <cmath>

namespace ic2d {

CanvasViewport compute_canvas_viewport(
    const int output_width,
    const int output_height,
    const int canvas_width,
    const int canvas_height
) noexcept {
    if (output_width <= 0 || output_height <= 0 || canvas_width <= 0 || canvas_height <= 0) {
        return {};
    }

    const float horizontal_scale = static_cast<float>(output_width) / static_cast<float>(canvas_width);
    const float vertical_scale = static_cast<float>(output_height) / static_cast<float>(canvas_height);
    const float fitted_scale = std::min(horizontal_scale, vertical_scale);
    const float scale = fitted_scale >= 1.0F ? std::floor(fitted_scale) : fitted_scale;
    const float width = static_cast<float>(canvas_width) * scale;
    const float height = static_cast<float>(canvas_height) * scale;

    return CanvasViewport{
        .x = (static_cast<float>(output_width) - width) * 0.5F,
        .y = (static_cast<float>(output_height) - height) * 0.5F,
        .width = width,
        .height = height,
        .scale = scale,
    };
}

} // namespace ic2d
