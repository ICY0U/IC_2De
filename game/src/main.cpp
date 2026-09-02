#include "ic2d/application.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <system_error>

// The editor executable is this same entry point with the shell open at
// startup, so both outputs stay in step instead of drifting apart.
#ifndef IC2DE_DEFAULT_START_WITH_EDITOR
#define IC2DE_DEFAULT_START_WITH_EDITOR 0
#endif

namespace {

[[nodiscard]] bool parse_fps(const std::string_view argument, int& output) {
    constexpr std::string_view prefix{"--fps="};
    if (!argument.starts_with(prefix)) {
        return false;
    }

    const std::string_view value = argument.substr(prefix.size());
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size() && output >= 0;
}

[[nodiscard]] bool parse_renderdoc_interval(const std::string_view argument, double& output) {
    constexpr std::string_view prefix{"--renderdoc-capture-interval="};
    if (!argument.starts_with(prefix)) {
        return false;
    }

    const std::string_view value = argument.substr(prefix.size());
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

template <typename Value>
[[nodiscard]] bool parse_count(
    const std::string_view argument,
    const std::string_view prefix,
    Value& output
) {
    if (!argument.starts_with(prefix)) {
        return false;
    }
    const std::string_view value = argument.substr(prefix.size());
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

} // namespace

int main(const int argc, const char* const argv[]) {
    ic2d::ApplicationConfig config;
    config.start_with_editor = IC2DE_DEFAULT_START_WITH_EDITOR != 0;
    config.enable_editor_texture_hot_reload = config.start_with_editor;
    if (config.start_with_editor) {
        config.title = "IC_2DE Editor";
    }
    const std::filesystem::path executable_directory =
        std::filesystem::absolute(argv[0]).lexically_normal().parent_path();
    const std::filesystem::path source_scene =
        std::filesystem::absolute(config.development_scene_path).lexically_normal();
    const std::filesystem::path adjacent_manifest = executable_directory / "IC_2DE.runtime";
    if (!std::filesystem::is_regular_file(source_scene) &&
        std::filesystem::is_regular_file(adjacent_manifest)) {
        config.development_scene_path.clear();
        config.runtime_project_manifest = adjacent_manifest;
    }

    for (int argument_index = 1; argument_index < argc; ++argument_index) {
        const std::string_view argument{argv[argument_index]};
        if (argument == "--smoke-window") {
            config.max_fixed_ticks = 120;
            config.capture_tick = 60;
            config.capture_path = "build/runtime-smoke.png";
        } else if (argument == "--smoke-movement") {
            config.automated_movement = true;
            config.max_fixed_ticks = 300;
            config.capture_tick = 285;
            config.capture_path = "build/runtime-camera-smoke.png";
        } else if (argument == "--smoke-left") {
            config.automated_movement = true;
            config.automated_movement_direction = {-1.0F, 0.0F};
            config.validate_automated_route = false;
            config.max_fixed_ticks = 60;
            config.capture_tick = 45;
            config.capture_path = "build/runtime-left-smoke.png";
        } else if (argument == "--smoke-crosshair") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = false;
            config.automated_movement = true;
            config.automated_movement_direction = {0.0F, -1.0F};
            config.automated_aim = true;
            config.automated_aim_direction = {1.0F, 0.0F};
            config.validate_automated_route = false;
            config.max_fixed_ticks = 75;
            config.capture_tick = 60;
            config.capture_path = "build/runtime-crosshair-smoke.png";
        } else if (argument == "--smoke-projectile") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = false;
            config.automated_aim = true;
            config.automated_aim_direction = {1.0F, 0.0F};
            config.automated_fire_hold_ticks = 1;
            config.max_fixed_ticks = 24;
            config.capture_tick = 12;
            config.capture_path = "build/runtime-projectile-smoke.png";
        } else if (argument == "--smoke-held-fire") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = false;
            config.automated_aim = true;
            config.automated_aim_direction = {1.0F, 0.0F};
            config.automated_fire_hold_ticks = 18;
            config.minimum_automated_projectile_spawns = 3;
            config.max_fixed_ticks = 24;
            config.capture_tick = 20;
            config.capture_path = "build/runtime-held-fire-smoke.png";
        } else if (argument == "--smoke-run-and-gun") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = false;
            config.automated_movement = true;
            config.automated_movement_direction = {0.0F, -1.0F};
            config.automated_aim = true;
            config.automated_aim_direction = {1.0F, 0.0F};
            config.automated_fire_hold_ticks = 18;
            config.minimum_automated_projectile_spawns = 3;
            config.validate_automated_route = false;
            config.max_fixed_ticks = 24;
            config.capture_tick = 20;
            config.capture_path = "build/runtime-run-and-gun-smoke.png";
        } else if (argument == "--smoke-projectile-impact") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = false;
            config.automated_aim = true;
            config.automated_aim_direction = {0.70710678F, 0.70710678F};
            config.automated_fire_hold_ticks = 1;
            config.minimum_automated_projectile_spawns = 1;
            config.minimum_automated_projectile_impacts = 1;
            config.max_fixed_ticks = 24;
            config.capture_tick = 10;
            config.capture_path = "build/runtime-projectile-impact-smoke.png";
        } else if (argument == "--smoke-target-death") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = false;
            config.automated_movement = true;
            config.automated_movement_direction = {0.98359346F, 0.18039931F};
            config.automated_aim = true;
            // Automated aim is camera-relative. This is the authored camera-space
            // direction from the player to the patchwork target's X/Z position.
            config.automated_aim_direction = {0.98359346F, 0.18039931F};
            config.automated_fire_hold_ticks = 18;
            config.minimum_automated_projectile_spawns = 3;
            config.minimum_automated_projectile_impacts = 3;
            config.minimum_automated_target_deaths = 1;
            config.validate_automated_route = false;
            config.max_fixed_ticks = 66;
            // Capture after the first two hits while the target and reduced
            // health bar are visible; the run continues through lethal hit 3.
            config.capture_tick = 57;
            config.capture_path = "build/runtime-target-death-smoke.png";
        } else if (argument == "--smoke-dodge") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = false;
            config.automated_dodge_tick = 1;
            config.minimum_automated_dodge_starts = 1;
            config.expected_automated_dodge_distance = 78.0F;
            config.max_fixed_ticks = 18;
            config.capture_tick = 6;
            config.capture_path = "build/runtime-dodge-smoke.png";
        } else if (argument == "--smoke-gameplay-replay") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = false;
            config.automated_aim = true;
            config.automated_aim_direction = {0.98359346F, 0.18039931F};
            config.automated_fire_hold_ticks = 18;
            config.automated_dodge_tick = 1;
            config.minimum_automated_projectile_spawns = 3;
            config.minimum_automated_projectile_impacts = 3;
            config.minimum_automated_target_deaths = 1;
            config.minimum_automated_dodge_starts = 1;
            config.expected_automated_dodge_distance = 78.0F;
            config.minimum_automated_enemy_acquisitions = 1;
            config.minimum_automated_enemy_attacks = 1;
            config.minimum_automated_enemy_distance = 80.0F;
            config.minimum_automated_player_damage = 12.0F;
            config.report_gameplay_state_digest = true;
            config.validate_automated_route = false;
            config.max_fixed_ticks = 180;
            config.capture_tick = 150;
            config.capture_path = "build/runtime-gameplay-digest-smoke.png";
        } else if (argument == "--smoke-moving-attacker") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = false;
            config.minimum_automated_enemy_acquisitions = 1;
            config.minimum_automated_enemy_attacks = 1;
            config.minimum_automated_enemy_distance = 100.0F;
            config.minimum_automated_player_damage = 12.0F;
            config.validate_automated_route = false;
            config.max_fixed_ticks = 150;
            config.capture_tick = 138;
            config.capture_path = "build/runtime-moving-attacker-smoke.png";
        } else if (argument == "--smoke-nav-grid") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = false;
            config.start_with_navigation_grid_debug = true;
            config.validate_automated_route = false;
            config.max_fixed_ticks = 30;
            config.capture_tick = 15;
            config.capture_path = "build/runtime-nav-grid-smoke.png";
        } else if (argument == "--smoke-nav-path") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = false;
            config.start_with_navigation_path_debug = true;
            config.validate_automated_route = false;
            config.max_fixed_ticks = 30;
            config.capture_tick = 15;
            config.capture_path = "build/runtime-nav-path-smoke.png";
        } else if (argument == "--smoke-runner-path") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = false;
            config.start_with_navigation_path_debug = true;
            config.minimum_automated_navigation_searches = 3;
            config.minimum_automated_navigation_waypoint_advances = 2;
            config.validate_automated_route = false;
            config.max_fixed_ticks = 90;
            config.capture_tick = 60;
            config.capture_path = "build/runtime-runner-path-smoke.png";
        } else if (argument == "--smoke-enemy-stress") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = false;
            config.start_with_navigation_path_debug = true;
            config.initial_editor_enemy_stress_count = 50;
            config.minimum_automated_navigation_agents = 50;
            config.minimum_automated_navigation_searches = 100;
            config.minimum_automated_navigation_waypoint_advances = 50;
            config.require_automated_zero_player_damage = true;
            config.validate_automated_route = false;
            config.max_fixed_ticks = 90;
            config.capture_tick = 60;
            config.capture_path = "build/runtime-enemy-stress-smoke.png";
        } else if (argument == "--smoke-crowd-kill") {
            // Fires into a spawned crowd and requires that a body-less actor
            // is hit, killed and retired, which is the whole of what taking
            // crowd actors out of the physics world put at risk.
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = false;
            config.initial_editor_enemy_stress_count = 400;
            config.automated_aim = true;
            config.automated_aim_direction = {1.0F, 0.0F};
            config.automated_fire_hold_ticks = 600;
            config.validate_automated_route = false;
            config.minimum_automated_crowd_actor_retirements = 1;
            config.max_fixed_ticks = 600;
            config.capture_tick = 240;
            config.capture_path = "build/runtime-crowd-kill-smoke.png";
        } else if (argument == "--smoke-editor-hot-swap") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = true;
            config.close_after_editor_texture_hot_reload = true;
            config.capture_path = "build/editor-hot-swap-smoke.png";
        } else if (argument == "--validate-content") {
            config.validate_content_only = true;
        } else if (argument == "--editor") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = true;
        } else if (argument.starts_with("--scene=")) {
            // An explicit scene overrides the adjacent-manifest fallback so a
            // launcher button can open the performance scene directly.
            config.development_scene_path =
                argument.substr(std::string{"--scene="}.size());
            config.runtime_project_manifest.clear();
        } else if (argument.starts_with("--stress=")) {
            // Headless and interactive performance runs select their own
            // Runner count instead of relying on the fixed smoke presets.
            std::size_t requested = 0;
            if (!parse_count(argument, "--stress=", requested)) {
                std::cerr << "Invalid stress count. Use --stress=N.\n";
                return 64;
            }
            config.start_with_editor = true;
            config.initial_editor_enemy_stress_count = requested;
        } else if (argument.starts_with("--ticks=")) {
            std::uint64_t requested = 0;
            if (!parse_count(argument, "--ticks=", requested)) {
                std::cerr << "Invalid tick count. Use --ticks=N.\n";
                return 64;
            }
            config.max_fixed_ticks = requested;
        } else if (argument == "--digest") {
            // Reports the future-affecting state hash at exit, so two runs of
            // the same scenario can be compared. This is how the threaded
            // crowd phases are shown to stay deterministic.
            config.report_gameplay_state_digest = true;
        } else if (argument.starts_with("--capture=")) {
            config.capture_path = argument.substr(std::string{"--capture="}.size());
        } else if (argument.starts_with("--capture-tick=")) {
            std::uint64_t requested = 0;
            if (!parse_count(argument, "--capture-tick=", requested)) {
                std::cerr << "Invalid capture tick. Use --capture-tick=N.\n";
                return 64;
            }
            config.capture_tick = requested;
        } else if (argument.starts_with("--editor-layout=")) {
            config.editor_layout_path = argument.substr(std::string{"--editor-layout="}.size());
        } else if (argument == "--no-debug-visuals") {
            config.start_with_debug_visuals = false;
        } else if (argument == "--no-post-process") {
            config.post_process.enabled = false;
        } else if (argument == "--uncapped") {
            config.render_pacing = {.mode = ic2d::RenderPacingMode::uncapped, .fixed_hz = 0};
        } else if (argument == "--monitor-hz" || argument == "--vsync") {
            config.render_pacing = {.mode = ic2d::RenderPacingMode::monitor_synced, .fixed_hz = 0};
        } else if (argument.starts_with("--fps=")) {
            int requested_fps = 0;
            if (!parse_fps(argument, requested_fps)) {
                std::cerr << "Invalid FPS cap. Use --fps=0 or a positive integer.\n";
                return 64;
            }
            config.render_pacing = requested_fps == 0
                                       ? ic2d::RenderPacingConfig{.mode = ic2d::RenderPacingMode::uncapped,
                                                                 .fixed_hz = 0}
                                       : ic2d::RenderPacingConfig{.mode = ic2d::RenderPacingMode::fixed_hz,
                                                                 .fixed_hz = requested_fps};
        } else if (argument.starts_with("--renderdoc-capture-interval=")) {
            double requested_interval = 0.0;
            if (!parse_renderdoc_interval(argument, requested_interval)) {
                std::cerr << "Invalid RenderDoc capture interval. Use a number of seconds; "
                             "0 disables automatic captures.\n";
                return 64;
            }
            config.renderdoc_capture_interval_seconds = requested_interval;
        } else if (argument == "--help") {
            std::cout << "IC_2DE Testbed\n"
                         "  --uncapped      Disable the render-rate cap (default).\n"
                         "  --monitor-hz    Synchronize presentation to the active monitor (VSync).\n"
                         "  --fps=N         Apply an optional render cap; zero is uncapped.\n"
                         "  --validate-content  Validate adjacent/source content and exit.\n"
                          "  --editor        Open the development editor shell at startup (F2 toggles).\n"
                         "  --editor-layout=PATH  Override the per-user editor workspace file.\n"
                         "  --scene=PATH    Load an authored .scene file instead of the default.\n"
                         "  --stress=N      Spawn N total stress-test Runners at startup.\n"
                         "  --ticks=N       Close after N fixed ticks; zero runs until quit.\n"
                         "  --digest        Report the gameplay state digest hash at exit.\n"
                         "  --capture=PATH  Write a screenshot to PATH.\n"
                         "  --capture-tick=N  Fixed tick at which to capture.\n"
                         "  --no-debug-visuals  Start with debug channels hidden (F1 toggles).\n"
                         "  --no-post-process  Bypass the external post-process shader (F7 toggles).\n"
                         "  --renderdoc-capture-interval=S  Auto-capture every S seconds when running\n"
                         "                                   under RenderDoc (default 5; 0 disables).\n"
                         "  --smoke-window  Capture a frame and close after 120 fixed ticks.\n"
                         "  --smoke-movement  Move diagonally for 300 ticks to verify XYZ projection and camera.\n"
                         "  --smoke-left    Move left and capture the west-facing animation.\n"
                         "  --smoke-crosshair  Move north, aim east, and capture the editor crosshair.\n"
                         "  --smoke-projectile  Fire east and capture fixed-tick projectile travel.\n"
                         "  --smoke-held-fire  Hold fire through three cooldown-ready ticks.\n"
                         "  --smoke-run-and-gun  Move north while aiming and firing east.\n"
                         "  --smoke-projectile-impact  Fire at the crate and require one impact.\n"
                         "  --smoke-target-death  Fire three deterministic hits into the NPC target.\n"
                         "  --smoke-dodge  Verify one exact-distance directional dodge and its active window.\n"
                         "  --smoke-gameplay-replay  Replay combat, dodge, and attacker state with a digest.\n"
                         "  --smoke-moving-attacker  Require deterministic acquire, pursuit, and player damage.\n"
                          "  --smoke-nav-grid  Display the read-only 2.5D hard-blocked navigation grid.\n"
                          "  --smoke-nav-path  Display the copied deterministic A-star reference path.\n"
                          "  --smoke-runner-path  Follow a bounded-repath route toward the player.\n"
                          "  --smoke-enemy-stress  Run 50 real navigation/AI/physics/render Runners.\n"
                          "  --smoke-crowd-kill  Require a body-less crowd actor to be shot and retired.\n"
                         "  --smoke-editor-hot-swap  Capture and close after a live editor texture replacement.\n";
            return 0;
        }
    }

    if (config.start_with_editor && !config.capture_path.empty() &&
        config.editor_layout_path.empty()) {
        config.editor_layout_path = "build/editor-smoke-layout-v1.ini";
    }

    return ic2d::run_application(config);
}
