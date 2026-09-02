#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace ic2d {

// Engine-owned Dear ImGui platform and renderer backend for raylib. It exists
// so the editor depends on ImGui alone: input translation, the font atlas, and
// rlgl draw-list submission are ours and stay private to the editor module.
//
// An instance owns the ImGui context. Construction requires a live raylib
// window because the font atlas is uploaded immediately.
class ImGuiRaylibBackend final {
public:
    // Typeface selection is the editor's decision, but the atlas is uploaded
    // during construction, so the hook has to run inside it. It is invoked once
    // with a live context and before any glyph is rasterized; an empty hook
    // keeps the built-in Dear ImGui font.
    using FontConfigurator = std::function<void()>;

    explicit ImGuiRaylibBackend(FontConfigurator configure_fonts = {});
    ~ImGuiRaylibBackend();

    ImGuiRaylibBackend(const ImGuiRaylibBackend&) = delete;
    ImGuiRaylibBackend& operator=(const ImGuiRaylibBackend&) = delete;

    [[nodiscard]] bool available() const noexcept;

    // Configure before the first frame. The backend owns the narrow path bytes
    // for as long as Dear ImGui may read io.IniFilename.
    [[nodiscard]] bool configure_layout_file(const std::filesystem::path& path) noexcept;
    [[nodiscard]] bool save_layout_now() noexcept;

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
    std::string layout_path_bytes_;
};

} // namespace ic2d
