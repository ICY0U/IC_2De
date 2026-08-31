#include "ic2d/application.hpp"

#include <charconv>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <system_error>

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
    const std::filesystem::path executable_directory =
        std::filesystem::absolute(argv[0]).lexically_normal().parent_path();
    ic2d::ApplicationConfig config{
        .title = "IC_2DE Shipped Test",
        .render_pacing = {.mode = ic2d::RenderPacingMode::monitor_synced, .fixed_hz = 0},
        .runtime_project_manifest = executable_directory / "IC_2DE.runtime",
    };

    for (int argument_index = 1; argument_index < argc; ++argument_index) {
        const std::string_view argument{argv[argument_index]};
        if (argument == "--shipping-smoke") {
            config.automated_movement = true;
            config.max_fixed_ticks = 300;
            config.capture_tick = 285;
            config.capture_path = "shipping-smoke.png";
        } else if (argument == "--validate-content") {
            config.validate_content_only = true;
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
            std::cout << "IC_2DE Shipped Test\n"
                         "  --monitor-hz      Synchronize to the active monitor (default).\n"
                         "  --uncapped        Disable presentation pacing.\n"
                         "  --fps=N           Apply an explicit presentation cap.\n"
                         "  --validate-content  Validate packaged content and exit.\n"
                         "  --shipping-smoke  Exercise the packaged test area and exit.\n";
            return 0;
        } else {
            std::cerr << "Unknown runtime option: " << argument << '\n';
            return 64;
        }
    }

    return ic2d::run_application(config);
}
