#include "ic2d/runtime_project.hpp"

#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace ic2d {
namespace {

constexpr std::uint32_t supported_schema_version = 1;

[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

[[nodiscard]] bool safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_path()) {
        return false;
    }
    for (const auto& part : path) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint32_t parse_schema(const std::string& value) {
    std::uint32_t schema = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), schema);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        throw std::runtime_error{"Runtime project schema must be an unsigned integer."};
    }
    return schema;
}

} // namespace

RuntimeProject RuntimeProject::load(const std::filesystem::path& manifest_path) {
    const std::filesystem::path absolute_manifest =
        std::filesystem::absolute(manifest_path).lexically_normal();
    std::ifstream stream{absolute_manifest};
    if (!stream) {
        throw std::runtime_error{"Runtime project manifest could not be opened: " +
                                 absolute_manifest.string()};
    }

    std::unordered_map<std::string, std::string> values;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(stream, line)) {
        ++line_number;
        const std::string_view cleaned = trim(line);
        if (cleaned.empty() || cleaned.front() == '#') {
            continue;
        }

        const std::size_t separator = cleaned.find('=');
        if (separator == std::string_view::npos) {
            throw std::runtime_error{"Runtime project line " + std::to_string(line_number) +
                                     " must use key=value syntax."};
        }
        const std::string key{trim(cleaned.substr(0, separator))};
        const std::string value{trim(cleaned.substr(separator + 1))};
        if (key.empty() || value.empty() || !values.emplace(key, value).second) {
            throw std::runtime_error{"Runtime project contains an empty or duplicate setting."};
        }
    }

    constexpr std::string_view required_keys[]{"schema", "name", "asset_directory", "start_scene"};
    for (const std::string_view key : required_keys) {
        if (!values.contains(std::string{key})) {
            throw std::runtime_error{"Runtime project is missing required setting: " +
                                     std::string{key}};
        }
    }
    if (values.size() != std::size(required_keys)) {
        throw std::runtime_error{"Runtime project contains an unsupported setting."};
    }

    RuntimeProject project;
    project.schema_version_ = parse_schema(values.at("schema"));
    if (project.schema_version_ != supported_schema_version) {
        throw std::runtime_error{"Unsupported runtime project schema version: " +
                                 std::to_string(project.schema_version_)};
    }
    project.name_ = values.at("name");
    project.start_scene_ = values.at("start_scene");
    project.project_directory_ = absolute_manifest.parent_path();
    project.asset_directory_ = values.at("asset_directory");

    if (!safe_relative_path(project.asset_directory_) ||
        !safe_relative_path(std::filesystem::path{project.start_scene_})) {
        throw std::runtime_error{"Runtime project content paths must be safe relative paths."};
    }
    const std::filesystem::path asset_root = project.project_directory_ / project.asset_directory_;
    if (!std::filesystem::is_directory(asset_root)) {
        throw std::runtime_error{"Runtime project asset directory is missing: " +
                                 asset_root.string()};
    }
    const std::filesystem::path scene_path = project.start_scene_path();
    if (!std::filesystem::is_regular_file(scene_path)) {
        throw std::runtime_error{"Runtime project start scene is missing: " + scene_path.string()};
    }
    return project;
}

std::uint32_t RuntimeProject::schema_version() const noexcept { return schema_version_; }

const std::string& RuntimeProject::name() const noexcept { return name_; }

const std::string& RuntimeProject::start_scene() const noexcept { return start_scene_; }

const std::filesystem::path& RuntimeProject::project_directory() const noexcept {
    return project_directory_;
}

const std::filesystem::path& RuntimeProject::asset_directory() const noexcept {
    return asset_directory_;
}

std::filesystem::path RuntimeProject::start_scene_path() const {
    return resolve_asset(start_scene_);
}

std::filesystem::path
RuntimeProject::resolve_asset(const std::filesystem::path& relative_path) const {
    if (!safe_relative_path(relative_path)) {
        throw std::invalid_argument{"Asset paths must remain relative to the runtime project."};
    }
    return (project_directory_ / asset_directory_ / relative_path).lexically_normal();
}

} // namespace ic2d
