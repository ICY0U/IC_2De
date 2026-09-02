#pragma once

#include <filesystem>
#include <string>

namespace ic2d::editor_detail {

// An explicit override keeps automated probes isolated. An empty override uses
// the per-user Windows application-data root and never the content directory.
[[nodiscard]] std::filesystem::path resolve_layout_path(
    const std::filesystem::path& override_path
) noexcept;

// A usable file must contain docking data. Window-only ini files are treated
// as first-run state so the editor can build its complete default workspace.
[[nodiscard]] bool layout_file_is_usable(const std::filesystem::path& path) noexcept;

// Creates only the parent directory. Dear ImGui remains the owner of the ini
// bytes and its normal dirty/save cadence.
[[nodiscard]] bool prepare_layout_path(
    const std::filesystem::path& path,
    std::string& diagnostic
) noexcept;

[[nodiscard]] bool remove_layout_file(
    const std::filesystem::path& path,
    std::string& diagnostic
) noexcept;

} // namespace ic2d::editor_detail
