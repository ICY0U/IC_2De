#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "ic2d/types.hpp"

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

struct PostProcessConfig {
    bool enabled{true};
    float exposure{1.0F};
    float saturation{1.0F};
    float vignette_strength{0.16F};
};

struct ApplicationConfig {
    std::string title{"IC_2DE 2.5D World Testbed"};
    int window_width{1280};
    int window_height{720};
    int canvas_width{640};
    int canvas_height{360};
    RenderPacingConfig render_pacing{};
    PostProcessConfig post_process{};
    double fixed_update_hz{60.0};
    int max_frames{0};
    int capture_frame{0};
    std::uint64_t max_fixed_ticks{0};
    std::uint64_t capture_tick{0};
    bool automated_movement{false};
    Vec2 automated_movement_direction{0.70710678F, 0.70710678F};
    bool automated_aim{false};
    Vec2 automated_aim_direction{1.0F, 0.0F};
    std::uint64_t automated_fire_hold_ticks{0};
    std::uint64_t automated_dodge_tick{0};
    std::uint64_t minimum_automated_projectile_spawns{0};
    std::uint64_t minimum_automated_projectile_impacts{0};
    std::uint64_t minimum_automated_target_deaths{0};
    std::uint64_t minimum_automated_dodge_starts{0};
    float expected_automated_dodge_distance{0.0F};
    std::uint64_t minimum_automated_enemy_acquisitions{0};
    std::uint64_t minimum_automated_enemy_attacks{0};
    float minimum_automated_enemy_distance{0.0F};
    float minimum_automated_player_damage{0.0F};
    bool require_automated_zero_player_damage{false};
    std::uint64_t minimum_automated_navigation_searches{0};
    std::uint64_t minimum_automated_navigation_waypoint_advances{0};
    std::size_t minimum_automated_navigation_agents{0};
    bool report_gameplay_state_digest{false};
    bool validate_automated_route{true};
    bool validate_content_only{false};
    bool start_with_editor{false};
    bool enable_editor_texture_hot_reload{false};
    bool close_after_editor_texture_hot_reload{false};
    bool start_with_debug_visuals{true};
    bool start_with_navigation_grid_debug{false};
    bool start_with_navigation_path_debug{false};
    // Development editor only. Zero keeps the authored Runner count.
    std::size_t initial_editor_enemy_stress_count{0};
    // Crowd actors hold no rigid body, so their killability rests on the
    // scene's own actor index rather than the physics broadphase. This
    // requires a run to prove that path end to end.
    std::uint64_t minimum_automated_crowd_actor_retirements{0};
    std::filesystem::path editor_layout_path{};
    std::string capture_path{};
    // Automatic RenderDoc frame captures, spaced by wall-clock time rather than
    // frame count (an uncapped debug build can render 1000+ fps, so a frame
    // count would capture far too often). Inert unless renderdoc.dll is
    // already injected into the process (see tools/launch-renderdoc.ps1). A
    // value <= 0 disables automatic captures.
    double renderdoc_capture_interval_seconds{5.0};
    std::filesystem::path development_scene_path{"game/assets/runtime/test_area.scene"};
    std::filesystem::path runtime_project_manifest{};
};

// Owns the complete platform lifecycle. No raylib type crosses this interface.
[[nodiscard]] int run_application(const ApplicationConfig& config);

} // namespace ic2d
