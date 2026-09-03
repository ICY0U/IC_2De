#include "input/raylib_input_adapter.hpp"

#include <algorithm>
#include <cmath>

#include <raylib.h>

namespace ic2d {

InputSample RaylibInputAdapter::sample() noexcept {
    const float keyboard_right = (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) ? 1.0F : 0.0F;
    const float keyboard_left = (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) ? 1.0F : 0.0F;
    const float keyboard_down = (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) ? 1.0F : 0.0F;
    const float keyboard_up = (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) ? 1.0F : 0.0F;
    float move_horizontal = keyboard_right - keyboard_left;
    float move_depth = keyboard_down - keyboard_up;
    bool pause = IsKeyDown(KEY_P);
    float aim_horizontal = 0.0F;
    float aim_depth = 0.0F;
    bool gamepad_fire = false;
    bool gamepad_reload = false;
    bool gamepad_dodge = false;
    bool gamepad_interact = false;
    bool gamepad_swap_weapon = false;
    bool gamepad_choose_extraction = false;

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

        const float dpad_right =
            IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) ? 1.0F : 0.0F;
        const float dpad_left = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT) ? 1.0F : 0.0F;
        const float dpad_down = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN) ? 1.0F : 0.0F;
        const float dpad_up = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP) ? 1.0F : 0.0F;
        move_horizontal = std::clamp(move_horizontal + stick + dpad_right - dpad_left, -1.0F, 1.0F);
        move_depth = std::clamp(move_depth + stick_depth + dpad_down - dpad_up, -1.0F, 1.0F);
        pause = pause || IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT);

        aim_horizontal = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X);
        aim_depth = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y);
        if (std::sqrt(aim_horizontal * aim_horizontal + aim_depth * aim_depth) < dead_zone) {
            aim_horizontal = 0.0F;
            aim_depth = 0.0F;
        } else {
            pointer_aim_active_ = false;
        }

        gamepad_fire = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.25F;
        gamepad_reload = IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
        gamepad_dodge = IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        gamepad_interact = IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
        gamepad_swap_weapon = IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_UP);
        gamepad_choose_extraction = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1);
    }

    const Vector2 mouse_delta = GetMouseDelta();
    const bool mouse_fire = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (mouse_delta.x != 0.0F || mouse_delta.y != 0.0F || mouse_fire) {
        pointer_aim_active_ = true;
    }
    const Vector2 pointer = GetMousePosition();

    InputSample sample{
        .move_horizontal = move_horizontal,
        .move_depth = move_depth,
        .pause = pause,
        .step_simulation = IsKeyDown(KEY_O),
        .reset = IsKeyDown(KEY_F5),
        .cycle_render_pacing = IsKeyDown(KEY_F6),
        .toggle_gpu_background = IsKeyDown(KEY_G),
        .toggle_post_process = IsKeyDown(KEY_F7),
        .toggle_debug_visuals = IsKeyDown(KEY_F1),
        .toggle_editor = IsKeyDown(KEY_F2),
    };
    sample.gameplay.aim = {
        .horizontal = aim_horizontal,
        .depth = aim_depth,
        .pointer_screen_x = pointer.x,
        .pointer_screen_y = pointer.y,
        .pointer_active = pointer_aim_active_,
    };
    sample.gameplay.set(GameplayAction::fire, mouse_fire || gamepad_fire);
    sample.gameplay.set(GameplayAction::reload, IsKeyDown(KEY_R) || gamepad_reload);
    sample.gameplay.set(GameplayAction::dodge, IsKeyDown(KEY_SPACE) || gamepad_dodge);
    sample.gameplay.set(GameplayAction::interact, IsKeyDown(KEY_E) || gamepad_interact);
    sample.gameplay.set(GameplayAction::swap_weapon,
                        IsKeyDown(KEY_Q) || GetMouseWheelMove() != 0.0F || gamepad_swap_weapon);
    sample.gameplay.set(GameplayAction::choose_extraction,
                        IsKeyDown(KEY_X) || gamepad_choose_extraction);
    return sample;
}

} // namespace ic2d
