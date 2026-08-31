#pragma once

namespace ic2d {

struct InputSample {
    float move_horizontal{0.0F};
    float move_depth{0.0F};
    bool pause{false};
    bool step_simulation{false};
    bool reset{false};
    bool cycle_render_pacing{false};
    bool toggle_gpu_background{false};
    bool toggle_post_process{false};
    bool toggle_debug_visuals{false};
    bool toggle_editor{false};
};

struct ButtonState {
    bool down{false};
    bool pressed{false};
    bool released{false};
};

struct InputFrame {
    float move_horizontal{0.0F};
    float move_depth{0.0F};
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
