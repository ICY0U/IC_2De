#include "ic2d/application.hpp"

#include "smoke_scenarios.hpp"

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
[[nodiscard]] bool parse_count(const std::string_view argument, const std::string_view prefix,
                               Value& output) {
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
    config.interactive_editor_session = config.start_with_editor;
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
        if (argument.starts_with("--smoke-")) {
            const std::string_view name = argument.substr(std::string_view{"--smoke-"}.size());
            const ic2de_game::SmokeScenario* scenario = ic2de_game::find_smoke_scenario(name);
            if (scenario == nullptr) {
                std::cerr << "Unknown smoke scenario: " << argument
                          << "\nRun --list-scenarios to see the registered ones.\n";
                return 64;
            }
            scenario->apply(config);
        } else if (argument == "--list-scenarios") {
            // Named separately from --help so tooling can read the list without
            // parsing prose. The CMake registration is checked against this
            // output, which is what keeps the two from drifting apart.
            for (const ic2de_game::SmokeScenario& registered : ic2de_game::smoke_scenarios()) {
                std::cout << registered.name << '\n';
            }
            return 0;
        } else if (argument == "--validate-content") {
            config.validate_content_only = true;
        } else if (argument == "--editor") {
            config.start_with_editor = true;
            config.enable_editor_texture_hot_reload = true;
            config.interactive_editor_session = true;
        } else if (argument.starts_with("--scene=")) {
            // An explicit scene overrides the adjacent-manifest fallback so a
            // launcher button can open the performance scene directly.
            config.development_scene_path = argument.substr(std::string{"--scene="}.size());
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
            config.render_pacing =
                requested_fps == 0
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
            std::cout
                << "IC_2DE Testbed\n"
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
                   "  --list-scenarios  List the registered smoke scenarios, one per line.\n";
            for (const ic2de_game::SmokeScenario& registered : ic2de_game::smoke_scenarios()) {
                std::cout << "  --smoke-" << registered.name << "  " << registered.description
                          << '\n';
            }
            return 0;
        }
    }

    // Every automated mode names a capture, caps its run, or exits before it
    // opens a window. None of them is a person sitting in front of the editor,
    // so none of them opens paused or takes the window chrome over, however it
    // reached this point.
    if (!config.capture_path.empty() || config.max_fixed_ticks != 0 || config.max_frames != 0 ||
        config.validate_content_only) {
        config.interactive_editor_session = false;
    }

    if (config.start_with_editor && !config.capture_path.empty() &&
        config.editor_layout_path.empty()) {
        config.editor_layout_path = "build/editor-smoke-layout-v1.ini";
    }

    return ic2d::run_application(config);
}
