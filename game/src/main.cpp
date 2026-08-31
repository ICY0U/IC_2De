#include "ic2d/application.hpp"

#include <charconv>
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

} // namespace

int main(const int argc, const char* const argv[]) {
    ic2d::ApplicationConfig config;
    config.start_with_editor = IC2DE_DEFAULT_START_WITH_EDITOR != 0;
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
        } else if (argument == "--validate-content") {
            config.validate_content_only = true;
        } else if (argument == "--editor") {
            config.start_with_editor = true;
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
        } else if (argument == "--help") {
            std::cout << "IC_2DE Testbed\n"
                         "  --uncapped      Disable the render-rate cap (default).\n"
                         "  --monitor-hz    Synchronize presentation to the active monitor (VSync).\n"
                         "  --fps=N         Apply an optional render cap; zero is uncapped.\n"
                         "  --validate-content  Validate adjacent/source content and exit.\n"
                         "  --editor        Open the development editor shell at startup (F2 toggles).\n"
                         "  --no-debug-visuals  Start with debug channels hidden (F1 toggles).\n"
                         "  --no-post-process  Bypass the external post-process shader (F7 toggles).\n"
                         "  --smoke-window  Capture a frame and close after 120 fixed ticks.\n"
                         "  --smoke-movement  Move diagonally for 300 ticks to verify XYZ projection and camera.\n";
            return 0;
        }
    }

    return ic2d::run_application(config);
}
