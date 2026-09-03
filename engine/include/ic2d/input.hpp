#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ic2d {

struct ButtonState {
    bool down{false};
    bool pressed{false};
    bool released{false};
};

enum class GameplayAction : std::uint8_t {
    fire,
    reload,
    dodge,
    interact,
    swap_weapon,
    choose_extraction,
    count,
};

inline constexpr std::array gameplay_actions{
    GameplayAction::fire,     GameplayAction::reload,      GameplayAction::dodge,
    GameplayAction::interact, GameplayAction::swap_weapon, GameplayAction::choose_extraction,
};

[[nodiscard]] std::string_view gameplay_action_name(GameplayAction action) noexcept;

struct AimInput {
    // A controller direction in camera-relative X/Z space. The input tracker
    // bounds it to unit length before callers observe it.
    float horizontal{0.0F};
    float depth{0.0F};

    // Desktop pointer position is deliberately screen-space here. The next
    // gameplay seam owns projecting it through the editor/runtime viewport.
    float pointer_screen_x{0.0F};
    float pointer_screen_y{0.0F};
    bool pointer_active{false};
};

struct GameplayInputSample {
    AimInput aim{};

    void set(GameplayAction action, bool down) noexcept;
    [[nodiscard]] bool down(GameplayAction action) const noexcept;

private:
    std::array<bool, gameplay_actions.size()> actions_{};
};

struct GameplayInputFrame {
    AimInput aim{};

    [[nodiscard]] ButtonState action(GameplayAction action) const noexcept;

private:
    friend class InputTracker;
    std::array<ButtonState, gameplay_actions.size()> actions_{};
};

struct InputSample {
    float move_horizontal{0.0F};
    float move_depth{0.0F};
    GameplayInputSample gameplay{};
    bool pause{false};
    bool step_simulation{false};
    bool reset{false};
    bool cycle_render_pacing{false};
    bool toggle_gpu_background{false};
    bool toggle_post_process{false};
    bool toggle_debug_visuals{false};
    bool toggle_editor{false};
};

struct InputFrame {
    float move_horizontal{0.0F};
    float move_depth{0.0F};
    GameplayInputFrame gameplay{};
    ButtonState pause{};
    ButtonState step_simulation{};
    ButtonState reset{};
    ButtonState cycle_render_pacing{};
    ButtonState toggle_gpu_background{};
    ButtonState toggle_post_process{};
    ButtonState toggle_debug_visuals{};
    ButtonState toggle_editor{};
};

// Converts logical samples from any adapter into stable per-frame transitions.
class InputTracker final {
public:
    [[nodiscard]] InputFrame update(const InputSample& sample) noexcept;
    void reset() noexcept;

private:
    InputSample previous_{};
};

} // namespace ic2d
