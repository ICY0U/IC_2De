#include "ic2d/input.hpp"

#include <algorithm>
#include <cmath>

namespace ic2d {
namespace {

[[nodiscard]] ButtonState transition(const bool previous, const bool current) noexcept {
    return ButtonState{
        .down = current,
        .pressed = current && !previous,
        .released = !current && previous,
    };
}

} // namespace

InputFrame InputTracker::update(const InputSample& sample) noexcept {
    float move_horizontal = std::clamp(sample.move_horizontal, -1.0F, 1.0F);
    float move_depth = std::clamp(sample.move_depth, -1.0F, 1.0F);
    const float movement_length = std::sqrt(move_horizontal * move_horizontal + move_depth * move_depth);
    if (movement_length > 1.0F) {
        move_horizontal /= movement_length;
        move_depth /= movement_length;
    }

    const InputFrame frame{
        .move_horizontal = move_horizontal,
        .move_depth = move_depth,
        .pause = transition(previous_.pause, sample.pause),
        .step_simulation = transition(previous_.step_simulation, sample.step_simulation),
        .reset = transition(previous_.reset, sample.reset),
        .cycle_render_pacing = transition(previous_.cycle_render_pacing, sample.cycle_render_pacing),
        .toggle_gpu_background = transition(previous_.toggle_gpu_background, sample.toggle_gpu_background),
        .toggle_debug_visuals = transition(previous_.toggle_debug_visuals, sample.toggle_debug_visuals),
        .toggle_editor = transition(previous_.toggle_editor, sample.toggle_editor),
    };
    previous_ = sample;
    return frame;
}

void InputTracker::reset() noexcept {
    previous_ = {};
}

} // namespace ic2d
