#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ic2d {

enum class RenderPacingMode {
    uncapped,
    monitor_synced,
    fixed_hz,
};

struct RenderPacingConfig {
    RenderPacingMode mode{RenderPacingMode::uncapped};
    int fixed_hz{0};
};

struct ApplicationConfig {
    std::string title{"IC_2DE 2.5D World Testbed"};
    int window_width{1280};
    int window_height{720};
    int canvas_width{640};
    int canvas_height{360};
    RenderPacingConfig render_pacing{};
    double fixed_update_hz{60.0};
    int max_frames{0};
    int capture_frame{0};
    std::uint64_t max_fixed_ticks{0};
    std::uint64_t capture_tick{0};
    bool automated_movement{false};
    bool validate_content_only{false};
    bool start_with_editor{false};
    bool start_with_debug_visuals{true};
    std::string capture_path{};
    std::filesystem::path development_scene_path{"game/assets/runtime/test_area.scene"};
    std::filesystem::path runtime_project_manifest{};
};

// Owns the complete platform lifecycle. No raylib type crosses this interface.
[[nodiscard]] int run_application(const ApplicationConfig& config);

} // namespace ic2d
