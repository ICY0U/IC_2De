#include <doctest/doctest.h>

#include "editor/editor_layout.hpp"
#include "ic2d/editor.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("editor focus decides who owns gameplay input") {
    CHECK_MESSAGE((ic2d::editor_blocks_gameplay_input(true, true, false)),
                  "Typing in an editor field must keep gameplay input blocked.");
    CHECK_MESSAGE((!ic2d::editor_blocks_gameplay_input(false, true, false)),
                  "A mouse-active editor item must not block held movement keys.");
    CHECK_MESSAGE((!ic2d::editor_blocks_gameplay_input(false, false, false)),
                  "An idle editor must not block gameplay input.");
    CHECK_MESSAGE((ic2d::editor_blocks_gameplay_input(false, false, true)),
                  "A held gizmo drag must own the frame so dragging cannot also fire.");
}

TEST_CASE("a workspace layout file is prepared, recognised and reset") {
    // One case rather than several: each step asserts about the file the
    // previous step left on disk, so splitting them would only make each
    // depend on the others having run first.
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path test_root = std::filesystem::temp_directory_path() /
                                            ("ic2de-editor-layout-test-" + std::to_string(nonce));
    const std::filesystem::path requested = test_root / "nested" / "layout.ini";
    const std::filesystem::path resolved = ic2d::editor_detail::resolve_layout_path(requested);
    CHECK_MESSAGE((resolved.is_absolute() && resolved.filename() == "layout.ini"),
                  "An explicit workspace override must resolve to one absolute file.");

    std::string diagnostic;
    CHECK_MESSAGE((ic2d::editor_detail::prepare_layout_path(resolved, diagnostic) &&
                   std::filesystem::is_directory(resolved.parent_path())),
                  "Layout preparation must create only the requested parent directory.");
    CHECK_MESSAGE((!ic2d::editor_detail::layout_file_is_usable(resolved)),
                  "A missing layout file must select the first-run workspace.");
    {
        std::ofstream output{resolved, std::ios::binary};
        output << "[Window][Viewport]\nPos=0,0\nSize=640,360\n";
    }
    CHECK_MESSAGE((!ic2d::editor_detail::layout_file_is_usable(resolved)),
                  "Window positions without a dock tree must not suppress the default workspace.");
    {
        std::ofstream output{resolved, std::ios::binary | std::ios::trunc};
        output << "[Window][Viewport]\nDockId=0x1\n"
                  "[Docking][Data]\nDockSpace ID=0x1 Pos=0,0 Size=1280,720\n";
    }
    CHECK_MESSAGE((ic2d::editor_detail::layout_file_is_usable(resolved)),
                  "A persisted dock tree must be recognized for restoration.");
    CHECK_MESSAGE((ic2d::editor_detail::remove_layout_file(resolved, diagnostic) &&
                   !std::filesystem::exists(resolved)),
                  "Reset must remove exactly the configured layout file.");

    std::error_code cleanup_error;
    std::filesystem::remove_all(test_root, cleanup_error);
}
