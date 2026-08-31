#include "input/raylib_input_adapter.hpp"

#include <algorithm>
#include <cmath>

#include <raylib.h>

namespace ic2d {

InputSample RaylibInputAdapter::sample() const noexcept {
    const float keyboard_right = (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) ? 1.0F : 0.0F;
    const float keyboard_left = (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) ? 1.0F : 0.0F;
    const float keyboard_down = (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) ? 1.0F : 0.0F;
    const float keyboard_up = (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) ? 1.0F : 0.0F;
    float move_horizontal = keyboard_right - keyboard_left;
    float move_depth = keyboard_down - keyboard_up;
    bool pause = IsKeyDown(KEY_P);

    if (IsGamepadAvailable(0)) {
        constexpr float dead_zone = 0.18F;
        float stick = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
        float stick_depth = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
        if (std::abs(stick) < dead_zone) {
            stick = 0.0F;
        }
        if (std::abs(stick_depth) < dead_zone) {
            stick_depth = 0.0F;
        }

        const float dpad_right = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) ? 1.0F : 0.0F;
        const float dpad_left = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT) ? 1.0F : 0.0F;
        const float dpad_down = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN) ? 1.0F : 0.0F;
        const float dpad_up = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP) ? 1.0F : 0.0F;
        move_horizontal = std::clamp(move_horizontal + stick + dpad_right - dpad_left, -1.0F, 1.0F);
        move_depth = std::clamp(move_depth + stick_depth + dpad_down - dpad_up, -1.0F, 1.0F);
        pause = pause || IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT);
    }

    return InputSample{
        .move_horizontal = move_horizontal,
        .move_depth = move_depth,
        .pause = pause,
        .step_simulation = IsKeyDown(KEY_O),
        .reset = IsKeyDown(KEY_R),
        .cycle_render_pacing = IsKeyDown(KEY_F6),
        .toggle_gpu_background = IsKeyDown(KEY_G),
        .toggle_debug_visuals = IsKeyDown(KEY_F1),
        .toggle_editor = IsKeyDown(KEY_F2),
    };
}

} // namespace ic2d
