#include "ic2d/input.hpp"

#include <cmath>

namespace ic2d {
namespace {

[[nodiscard]] std::size_t action_index(const GameplayAction action) noexcept {
    return static_cast<std::size_t>(action);
}

[[nodiscard]] ButtonState transition(const bool previous, const bool current) noexcept {
    return ButtonState{
        .down = current,
        .pressed = current && !previous,
        .released = !current && previous,
    };
}

void bound_axis_pair(float& horizontal, float& depth) noexcept {
    const float length = std::sqrt(horizontal * horizontal + depth * depth);
    if (length > 1.0F) {
        horizontal /= length;
        depth /= length;
    }
}

} // namespace

std::string_view gameplay_action_name(const GameplayAction action) noexcept {
    switch (action) {
        case GameplayAction::fire: return "Fire";
        case GameplayAction::reload: return "Reload";
        case GameplayAction::dodge: return "Dodge";
        case GameplayAction::interact: return "Interact";
        case GameplayAction::swap_weapon: return "Swap weapon";
        case GameplayAction::choose_extraction: return "Choose extraction";
        case GameplayAction::count: break;
    }
    return "Unknown";
}

void GameplayInputSample::set(const GameplayAction action, const bool down) noexcept {
    const std::size_t index = action_index(action);
    if (index < actions_.size()) {
        actions_[index] = down;
    }
}

bool GameplayInputSample::down(const GameplayAction action) const noexcept {
    const std::size_t index = action_index(action);
    return index < actions_.size() && actions_[index];
}

ButtonState GameplayInputFrame::action(const GameplayAction action) const noexcept {
    const std::size_t index = action_index(action);
    return index < actions_.size() ? actions_[index] : ButtonState{};
}

InputFrame InputTracker::update(const InputSample& sample) noexcept {
    float move_horizontal = sample.move_horizontal;
    float move_depth = sample.move_depth;
    bound_axis_pair(move_horizontal, move_depth);

    GameplayInputFrame gameplay;
    gameplay.aim = sample.gameplay.aim;
    bound_axis_pair(gameplay.aim.horizontal, gameplay.aim.depth);
    for (const GameplayAction action : gameplay_actions) {
        const std::size_t index = action_index(action);
        gameplay.actions_[index] =
            transition(previous_.gameplay.down(action), sample.gameplay.down(action));
    }

    const InputFrame frame{
        .move_horizontal = move_horizontal,
        .move_depth = move_depth,
        .gameplay = gameplay,
        .pause = transition(previous_.pause, sample.pause),
        .step_simulation = transition(previous_.step_simulation, sample.step_simulation),
        .reset = transition(previous_.reset, sample.reset),
        .cycle_render_pacing = transition(previous_.cycle_render_pacing, sample.cycle_render_pacing),
        .toggle_gpu_background = transition(previous_.toggle_gpu_background, sample.toggle_gpu_background),
        .toggle_post_process = transition(previous_.toggle_post_process, sample.toggle_post_process),
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
