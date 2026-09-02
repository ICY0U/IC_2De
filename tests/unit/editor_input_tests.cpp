#include "ic2d/editor.hpp"
#include "editor/editor_layout.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main() {
    int failures = 0;
    const auto expect = [&failures](const bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    };

    expect(ic2d::editor_blocks_gameplay_input(true, true),
           "Typing in an editor field must keep gameplay input blocked.");
    expect(!ic2d::editor_blocks_gameplay_input(false, true),
           "A mouse-active editor item must not block held movement keys.");
    expect(!ic2d::editor_blocks_gameplay_input(false, false),
           "An idle editor must not block gameplay input.");

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path test_root =
        std::filesystem::temp_directory_path() /
        ("ic2de-editor-layout-test-" + std::to_string(nonce));
    const std::filesystem::path requested = test_root / "nested" / "layout.ini";
    const std::filesystem::path resolved =
        ic2d::editor_detail::resolve_layout_path(requested);
    expect(resolved.is_absolute() && resolved.filename() == "layout.ini",
           "An explicit workspace override must resolve to one absolute file.");

    std::string diagnostic;
    expect(ic2d::editor_detail::prepare_layout_path(resolved, diagnostic) &&
               std::filesystem::is_directory(resolved.parent_path()),
           "Layout preparation must create only the requested parent directory.");
    expect(!ic2d::editor_detail::layout_file_is_usable(resolved),
           "A missing layout file must select the first-run workspace.");
    {
        std::ofstream output{resolved, std::ios::binary};
        output << "[Window][Viewport]\nPos=0,0\nSize=640,360\n";
    }
    expect(!ic2d::editor_detail::layout_file_is_usable(resolved),
           "Window positions without a dock tree must not suppress the default workspace.");
    {
        std::ofstream output{resolved, std::ios::binary | std::ios::trunc};
        output << "[Window][Viewport]\nDockId=0x1\n"
                  "[Docking][Data]\nDockSpace ID=0x1 Pos=0,0 Size=1280,720\n";
    }
    expect(ic2d::editor_detail::layout_file_is_usable(resolved),
           "A persisted dock tree must be recognized for restoration.");
    expect(ic2d::editor_detail::remove_layout_file(resolved, diagnostic) &&
               !std::filesystem::exists(resolved),
           "Reset must remove exactly the configured layout file.");
    std::error_code cleanup_error;
    std::filesystem::remove_all(test_root, cleanup_error);

    if (failures == 0) {
        std::cout << "Editor input and workspace-layout tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
