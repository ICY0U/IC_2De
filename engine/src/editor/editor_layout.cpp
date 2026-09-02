#include "editor/editor_layout.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

namespace ic2d::editor_detail {
namespace {

[[nodiscard]] std::filesystem::path normalized_absolute(
    const std::filesystem::path& path
) noexcept {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return error ? path.lexically_normal() : absolute.lexically_normal();
}

[[nodiscard]] std::filesystem::path environment_path(const char* name) {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr || length <= 1U) {
        std::free(value);
        return {};
    }
    const std::filesystem::path result{value};
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? std::filesystem::path{value}
                                               : std::filesystem::path{};
#endif
}

} // namespace

std::filesystem::path resolve_layout_path(
    const std::filesystem::path& override_path
) noexcept {
    if (!override_path.empty()) {
        return normalized_absolute(override_path);
    }

    std::filesystem::path root = environment_path("LOCALAPPDATA");
    if (root.empty()) {
        root = environment_path("APPDATA");
    }
    if (root.empty()) {
        std::error_code error;
        root = std::filesystem::temp_directory_path(error);
        if (error) {
            return {};
        }
    }
    return normalized_absolute(root / "IC_2DE" / "Editor" / "layout-v2.ini");
}

bool layout_file_is_usable(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        return false;
    }
    try {
        std::ifstream input{path, std::ios::binary};
        if (!input) {
            return false;
        }
        const std::string bytes{
            std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        return bytes.find("[Docking][Data]") != std::string::npos &&
               bytes.find("DockSpace") != std::string::npos;
    } catch (...) {
        return false;
    }
}

bool prepare_layout_path(
    const std::filesystem::path& path,
    std::string& diagnostic
) noexcept {
    diagnostic.clear();
    if (path.empty() || path.filename().empty()) {
        diagnostic = "Layout persistence requires a file path.";
        return false;
    }
    std::error_code error;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
    }
    if (error) {
        diagnostic = "Could not create the editor layout directory: " + error.message();
        return false;
    }
    return true;
}

bool remove_layout_file(
    const std::filesystem::path& path,
    std::string& diagnostic
) noexcept {
    diagnostic.clear();
    if (path.empty()) {
        diagnostic = "Layout persistence has no configured file.";
        return false;
    }
    std::error_code error;
    static_cast<void>(std::filesystem::remove(path, error));
    if (error) {
        diagnostic = "Could not reset the editor layout: " + error.message();
        return false;
    }
    return true;
}

} // namespace ic2d::editor_detail
