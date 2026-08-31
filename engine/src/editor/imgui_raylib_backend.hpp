#pragma once

namespace ic2d {

// Engine-owned Dear ImGui platform and renderer backend for raylib. It exists
// so the editor depends on ImGui alone: input translation, the font atlas, and
// rlgl draw-list submission are ours and stay private to the editor module.
//
// An instance owns the ImGui context. Construction requires a live raylib
// window because the font atlas is uploaded immediately.
class ImGuiRaylibBackend final {
public:
    ImGuiRaylibBackend();
    ~ImGuiRaylibBackend();

    ImGuiRaylibBackend(const ImGuiRaylibBackend&) = delete;
    ImGuiRaylibBackend& operator=(const ImGuiRaylibBackend&) = delete;

    [[nodiscard]] bool available() const noexcept;

    // Feeds display size, timing, pointer, and keyboard state, then opens a
    // frame. Returns false when the backend is unavailable.
    [[nodiscard]] bool new_frame();
    // Closes the frame and submits its draw lists through rlgl.
    void render();

    // True only while a text field is collecting characters.
    [[nodiscard]] bool wants_text_input() const noexcept;
    // True while a widget is being typed in, dragged, or held.
    [[nodiscard]] bool item_active() const noexcept;
    [[nodiscard]] bool wants_mouse() const noexcept;

private:
    void release() noexcept;

    unsigned int font_texture_id_{0};
    bool context_created_{false};
    bool frame_open_{false};
    bool wants_text_input_{false};
    bool item_active_{false};
    bool wants_mouse_{false};
};

} // namespace ic2d
