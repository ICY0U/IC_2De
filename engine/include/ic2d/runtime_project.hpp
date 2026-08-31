#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ic2d {

// Versioned, package-relative runtime project description. Paths are required
// to stay beneath the project directory so shipped content is relocatable.
class RuntimeProject final {
public:
    [[nodiscard]] static RuntimeProject load(const std::filesystem::path& manifest_path);

    [[nodiscard]] std::uint32_t schema_version() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const std::string& start_scene() const noexcept;
    [[nodiscard]] const std::filesystem::path& project_directory() const noexcept;
    [[nodiscard]] const std::filesystem::path& asset_directory() const noexcept;
    [[nodiscard]] std::filesystem::path start_scene_path() const;
    [[nodiscard]] std::filesystem::path resolve_asset(const std::filesystem::path& relative_path) const;

private:
    std::uint32_t schema_version_{0};
    std::string name_;
    std::string start_scene_;
    std::filesystem::path project_directory_;
    std::filesystem::path asset_directory_;
};

} // namespace ic2d
