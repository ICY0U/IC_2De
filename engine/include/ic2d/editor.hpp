#pragma once

#include "ic2d/debug_visuals.hpp"
#include "ic2d/scene_editor.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace ic2d {

// Read-only runtime figures the shell reports. The editor never reaches into
// the running scene for them, so it cannot disturb simulation state.
struct EditorStats {
    int frames_per_second{0};
    double fixed_update_hz{60.0};
    std::uint64_t simulated_ticks{0};
    std::size_t entity_count{0};
    std::size_t physics_body_count{0};
    std::size_t loaded_texture_count{0};
    std::size_t visible_sprites{0};
    std::size_t culled_sprites{0};
    std::size_t estimated_batches{0};
    std::size_t estimated_draw_calls{0};
    std::size_t visible_vertices{0};
    double frame_time_p50_ms{0.0};
    double frame_time_p95_ms{0.0};
    double frame_time_p99_ms{0.0};
    std::uint32_t estimated_gpu_passes{0};
    std::uint32_t render_target_switches{0};
    std::uint32_t shader_passes{0};
    bool post_process_active{false};
    bool post_process_available{false};
    bool paused{false};
};

// What the shell asks the application to do after a frame of panels. Anything
// the editor can do safely by itself, such as saving, it already did.
struct EditorActions {
    bool apply_document_to_running_scene{false};
    bool reset_running_scene{false};
    bool toggle_pause{false};
};

// The identity of the canvas the viewport panel displays. The value is a
// backend texture name; only the development editor consumes it.
struct EditorCanvas {
    std::uint32_t texture_id{0};
    int width{0};
    int height{0};
};

// Development-only docked editor shell. Dear ImGui, the docking layout, and
// the raylib draw backend stay private to this module. Every scene mutation it
// performs goes through SceneEditor, so undo, validation, and atomic saving
// behave exactly as they do for any other caller.
class EditorShell final {
public:
    EditorShell();
    ~EditorShell();

    EditorShell(const EditorShell&) = delete;
    EditorShell& operator=(const EditorShell&) = delete;
    EditorShell(EditorShell&&) noexcept;
    EditorShell& operator=(EditorShell&&) noexcept;

    // False when the shell could not create its context or font atlas.
    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] bool visible() const noexcept;
    void set_visible(bool visible) noexcept;
    void toggle_visible() noexcept;

    // Reported from the previous drawn frame, which is when the shell last
    // observed the pointer and keyboard.
    //
    // The game keeps running behind the panels, so gameplay keys keep reaching
    // it exactly as they do with the shell hidden. They are withheld only while
    // a panel field is actually collecting them: a text field being typed in, or
    // a widget being dragged or held.
    [[nodiscard]] bool blocks_gameplay_input() const noexcept;
    [[nodiscard]] bool wants_mouse() const noexcept;

    // Call once per frame inside an active drawing pass. Does nothing and
    // returns no actions while the shell is hidden or unavailable.
    [[nodiscard]] EditorActions draw(
        SceneEditor& scene_editor,
        DebugVisuals& debug_visuals,
        const EditorStats& stats,
        const EditorCanvas& canvas
    );

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
