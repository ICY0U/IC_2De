#include "ic2d/scene_document.hpp"

#include "ic2d/scene.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace ic2d {
namespace {

constexpr std::uint32_t current_scene_schema = 10;
constexpr std::size_t entity_field_count = 17;
constexpr std::size_t prefab_field_count = 13;
constexpr std::size_t prefab_instance_field_count = 8;
constexpr std::size_t prefab_override_field_count = 3;
constexpr std::size_t animation_binding_field_count = 4;
constexpr std::size_t animation_auto_field_count = 3;

struct TextRecord {
    std::string key;
    std::vector<std::string> fields;
};

// Entity and prefab-instance records place the same authored concepts at
// different offsets. Tools address both through one layout instead of
// branching on the record key at every call site.
struct PlacementLayout {
    std::size_t uuid{0};
    std::size_t prefab{0}; // Zero when the record is not a prefab instance.
    std::size_t name{0};
    std::size_t binding{0};
    std::size_t position{0};
};

[[nodiscard]] std::string_view trim(const std::string_view value) noexcept {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

[[nodiscard]] std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error{"Scene document could not be opened: " + path.string()};
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
    }
    if (!stream.eof()) {
        throw std::runtime_error{"Scene document could not be read completely: " + path.string()};
    }
    return lines;
}

[[nodiscard]] bool parse_record(const std::string& line, TextRecord& output) {
    const std::string_view stripped = trim(line);
    if (stripped.empty() || stripped.starts_with('#')) {
        return false;
    }
    const std::size_t equals = stripped.find('=');
    if (equals == std::string_view::npos) {
        return false;
    }
    output = {};
    output.key = trim(stripped.substr(0, equals));
    const std::string_view value = stripped.substr(equals + 1);
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t separator = value.find('|', start);
        const std::size_t end = separator == std::string_view::npos ? value.size() : separator;
        output.fields.emplace_back(trim(value.substr(start, end - start)));
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    return true;
}

[[nodiscard]] std::string format_record(const TextRecord& record) {
    std::string result = record.key + '=';
    for (std::size_t index = 0; index < record.fields.size(); ++index) {
        if (index > 0) {
            result.push_back('|');
        }
        result += record.fields[index];
    }
    return result;
}

template <typename Number>
[[nodiscard]] Number parse_number(const std::string_view value, const std::string_view label) {
    Number parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (value.empty() || result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        throw std::runtime_error{"Scene document has an invalid " + std::string{label} + '.'};
    }
    return parsed;
}

[[nodiscard]] std::string format_float(const float value) {
    char buffer[64]{};
    const auto result = std::to_chars(
        std::begin(buffer), std::end(buffer), value, std::chars_format::general,
        std::numeric_limits<float>::max_digits10);
    if (result.ec != std::errc{}) {
        throw std::runtime_error{"Scene document could not format a position."};
    }
    return std::string{buffer, result.ptr};
}

[[nodiscard]] bool valid_name(const std::string_view name) noexcept {
    return !trim(name).empty() && name.find_first_of("|\r\n") == std::string_view::npos;
}

[[nodiscard]] bool valid_id(const std::string_view id) noexcept {
    if (id.empty()) {
        return false;
    }
    for (const char character : id) {
        const bool alpha_numeric = (character >= 'a' && character <= 'z') ||
                                   (character >= 'A' && character <= 'Z') ||
                                   (character >= '0' && character <= '9');
        if (!alpha_numeric && character != '_' && character != '-' && character != '.') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool placement_layout(const TextRecord& record, PlacementLayout& layout) noexcept {
    // Schema 10 entity records carry an optional trailing depth span. Every
    // field this class edits sits before it, and formatting rewrites the whole
    // record, so a spanned wall keeps its span across a rename or a move.
    if (record.key == "entity" &&
        (record.fields.size() == entity_field_count ||
         record.fields.size() == entity_field_count + 1)) {
        layout = {.uuid = 1, .prefab = 0, .name = 2, .binding = 3, .position = 4};
        return true;
    }
    if (record.key == "prefab_instance" &&
        record.fields.size() == prefab_instance_field_count) {
        layout = {.uuid = 1, .prefab = 2, .name = 3, .binding = 4, .position = 5};
        return true;
    }
    return false;
}

[[nodiscard]] bool find_placement(
    const std::vector<std::string>& lines,
    const EntityUuid uuid,
    std::size_t& line_index,
    TextRecord& found_record,
    PlacementLayout& found_layout
) {
    for (std::size_t index = 0; index < lines.size(); ++index) {
        TextRecord record;
        PlacementLayout layout;
        if (!parse_record(lines[index], record) || !placement_layout(record, layout)) {
            continue;
        }
        if (parse_number<std::uint64_t>(record.fields[layout.uuid], "entity UUID") != uuid.value) {
            continue;
        }
        line_index = index;
        found_record = std::move(record);
        found_layout = layout;
        return true;
    }
    return false;
}

[[nodiscard]] std::string scene_id_of(const std::vector<std::string>& lines) {
    for (const std::string& line : lines) {
        TextRecord record;
        if (parse_record(line, record) && record.key == "id" && record.fields.size() == 1) {
            return record.fields[0];
        }
    }
    return {};
}

[[nodiscard]] std::uint64_t stable_uuid(
    const std::string_view scene_id,
    const std::string_view entity_id
) noexcept {
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t value = offset;
    const auto append = [&](const std::string_view text) {
        for (const unsigned char character : text) {
            value ^= character;
            value *= prime;
        }
    };
    append(scene_id);
    value ^= static_cast<unsigned char>('/');
    value *= prime;
    append(entity_id);
    return value == 0 ? 1 : value;
}

void write_candidate(
    const std::filesystem::path& path,
    const std::vector<std::string>& lines
) {
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream) {
        throw std::runtime_error{"Scene candidate could not be created: " + path.string()};
    }
    for (const std::string& line : lines) {
        stream << line << '\n';
    }
    stream.flush();
    if (!stream) {
        throw std::runtime_error{"Scene candidate could not be written completely: " + path.string()};
    }
}

void replace_file(
    const std::filesystem::path& candidate,
    const std::filesystem::path& destination
) {
#if defined(_WIN32)
    const bool destination_exists = std::filesystem::exists(destination);
    const BOOL replaced = destination_exists
                              ? ReplaceFileW(destination.c_str(), candidate.c_str(), nullptr,
                                             REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)
                              : MoveFileExW(candidate.c_str(), destination.c_str(),
                                            MOVEFILE_WRITE_THROUGH);
    if (replaced == FALSE) {
        throw std::runtime_error{
            "Atomic scene replacement failed with Windows error " +
            std::to_string(static_cast<unsigned long>(GetLastError())) + '.'};
    }
#else
    std::filesystem::rename(candidate, destination);
#endif
}

// Schema 5 predates persistent entity identity. Schemas 6 and 7 remain
// readable because later schemas only add optional prefab/animation records.
void migrate_five_to_six(std::vector<std::string>& lines, const std::string_view scene_id) {
    std::unordered_set<std::uint64_t> assigned;
    for (std::string& line : lines) {
        TextRecord record;
        if (!parse_record(line, record) || record.key != "entity") {
            continue;
        }
        if (record.fields.size() != entity_field_count - 1 || record.fields[0].empty()) {
            throw std::runtime_error{"Schema 5 entity records must contain sixteen fields."};
        }
        std::uint64_t uuid = stable_uuid(scene_id, record.fields[0]);
        while (!assigned.insert(uuid).second) {
            ++uuid;
            if (uuid == 0) {
                uuid = 1;
            }
        }
        record.fields.insert(record.fields.begin() + 1, std::to_string(uuid));
        line = format_record(record);
    }
}

void set_schema_version(std::vector<std::string>& lines, const std::uint32_t schema) {
    for (std::string& line : lines) {
        TextRecord record;
        if (!parse_record(line, record) || record.key != "schema" || record.fields.size() != 1) {
            continue;
        }
        record.fields[0] = std::to_string(schema);
        line = format_record(record);
        return;
    }
    throw std::runtime_error{"Scene document has no schema setting."};
}

} // namespace

SceneDocument::SceneDocument(
    std::filesystem::path source_path,
    std::vector<std::string> lines,
    const std::uint32_t schema_version
)
    : source_path_{std::move(source_path)},
      lines_{std::move(lines)},
      schema_version_{schema_version} {}

SceneDocument SceneDocument::open(const std::filesystem::path& path) {
    const std::filesystem::path absolute_path = std::filesystem::absolute(path).lexically_normal();
    const SceneDefinition validated = SceneDefinition::load(absolute_path);
    return SceneDocument{absolute_path, read_lines(absolute_path), validated.schema_version()};
}

SceneDocument SceneDocument::migrate_to_current(const std::filesystem::path& path) {
    const std::filesystem::path absolute_path = std::filesystem::absolute(path).lexically_normal();
    std::vector<std::string> lines = read_lines(absolute_path);
    std::uint32_t schema = 0;
    for (const std::string& line : lines) {
        TextRecord record;
        if (parse_record(line, record) && record.key == "schema" && record.fields.size() == 1) {
            schema = parse_number<std::uint32_t>(record.fields[0], "schema version");
            break;
        }
    }
    if (schema == current_scene_schema) {
        return open(absolute_path);
    }
    const std::string scene_id = scene_id_of(lines);
    if ((schema < 5 || schema > 9) || scene_id.empty()) {
        throw std::runtime_error{
            "Only scene schema 5, 6, 7, 8, or 9 can be migrated to schema 10."};
    }
    if (schema == 5) {
        migrate_five_to_six(lines, scene_id);
    }
    set_schema_version(lines, current_scene_schema);

    SceneDocument migrated{absolute_path, std::move(lines), current_scene_schema};
    migrated.save_atomic(absolute_path);
    return open(absolute_path);
}

std::uint32_t SceneDocument::schema_version() const noexcept { return schema_version_; }
const std::filesystem::path& SceneDocument::source_path() const noexcept { return source_path_; }

std::vector<SceneDocumentEntity> SceneDocument::entities() const {
    std::vector<SceneDocumentEntity> result;
    for (const std::string& line : lines_) {
        TextRecord record;
        PlacementLayout layout;
        if (!parse_record(line, record) || !placement_layout(record, layout)) {
            continue;
        }
        result.push_back({
            .uuid = {parse_number<std::uint64_t>(record.fields[layout.uuid], "entity UUID")},
            .id = record.fields[0],
            .name = record.fields[layout.name],
            .prefab_id = layout.prefab == 0 ? std::string{} : record.fields[layout.prefab],
            .physics_bound = record.fields[layout.binding] != "-",
            .position = {
                parse_number<float>(record.fields[layout.position], "entity X"),
                parse_number<float>(record.fields[layout.position + 1], "entity Y"),
                parse_number<float>(record.fields[layout.position + 2], "entity Z"),
            },
        });
    }
    return result;
}

std::vector<SceneDocumentPrefab> SceneDocument::prefabs() const {
    std::vector<SceneDocumentPrefab> result;
    for (const std::string& line : lines_) {
        TextRecord record;
        if (!parse_record(line, record) || record.key != "prefab" ||
            record.fields.size() != prefab_field_count) {
            continue;
        }
        result.push_back({
            .uuid = {parse_number<std::uint64_t>(record.fields[1], "prefab UUID")},
            .id = record.fields[0],
            .name = record.fields[2],
        });
    }
    return result;
}

bool SceneDocument::rename_entity(const EntityUuid uuid, const std::string_view name) {
    if (!uuid || !valid_name(name)) {
        throw std::invalid_argument{"Entity rename requires a non-zero UUID and a non-empty plain name."};
    }
    std::size_t index = 0;
    TextRecord record;
    PlacementLayout layout;
    if (!find_placement(lines_, uuid, index, record, layout)) {
        return false;
    }
    record.fields[layout.name] = std::string{trim(name)};
    lines_[index] = format_record(record);
    return true;
}

bool SceneDocument::set_unbound_entity_position(const EntityUuid uuid, const Vec3 position) {
    if (!uuid || !std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z)) {
        throw std::invalid_argument{"Entity movement requires a non-zero UUID and finite position."};
    }
    std::size_t index = 0;
    TextRecord record;
    PlacementLayout layout;
    if (!find_placement(lines_, uuid, index, record, layout)) {
        return false;
    }
    if (record.fields[layout.binding] != "-") {
        throw std::invalid_argument{
            "Physics-bound entities must be moved through a future body-edit command."};
    }
    record.fields[layout.position] = format_float(position.x);
    record.fields[layout.position + 1] = format_float(position.y);
    record.fields[layout.position + 2] = format_float(position.z);
    lines_[index] = format_record(record);
    return true;
}

EntityUuid SceneDocument::create_prefab_instance(const ScenePrefabPlacement& placement) {
    if (!valid_id(placement.prefab_id) || !valid_id(placement.instance_id) ||
        !valid_name(placement.name) || !std::isfinite(placement.position.x) ||
        !std::isfinite(placement.position.y) || !std::isfinite(placement.position.z)) {
        throw std::invalid_argument{
            "Prefab placement requires valid ids, a non-empty plain name, and a finite position."};
    }
    const std::string scene_id = scene_id_of(lines_);
    if (scene_id.empty()) {
        throw std::runtime_error{"Scene document has no id setting."};
    }

    bool prefab_exists = false;
    std::unordered_set<std::string> placed_ids;
    std::unordered_set<std::uint64_t> used_uuids;
    std::size_t last_placement_line = 0;
    bool has_placement = false;
    for (std::size_t index = 0; index < lines_.size(); ++index) {
        TextRecord record;
        PlacementLayout layout;
        if (!parse_record(lines_[index], record)) {
            continue;
        }
        if (record.key == "prefab" && record.fields.size() == prefab_field_count) {
            prefab_exists = prefab_exists || record.fields[0] == placement.prefab_id;
            used_uuids.insert(parse_number<std::uint64_t>(record.fields[1], "prefab UUID"));
        } else if (placement_layout(record, layout)) {
            placed_ids.insert(record.fields[0]);
            used_uuids.insert(
                parse_number<std::uint64_t>(record.fields[layout.uuid], "entity UUID"));
            has_placement = true;
            last_placement_line = index;
        }
    }
    if (!prefab_exists) {
        return {};
    }
    if (placed_ids.contains(placement.instance_id)) {
        throw std::invalid_argument{
            "Prefab instance ids must be unique within a scene: " + placement.instance_id};
    }

    std::uint64_t uuid = stable_uuid(scene_id, placement.instance_id);
    while (!used_uuids.insert(uuid).second) {
        ++uuid;
        if (uuid == 0) {
            uuid = 1;
        }
    }
    const TextRecord record{
        .key = "prefab_instance",
        .fields = {
            placement.instance_id,
            std::to_string(uuid),
            placement.prefab_id,
            std::string{trim(placement.name)},
            "-",
            format_float(placement.position.x),
            format_float(placement.position.y),
            format_float(placement.position.z),
        },
    };
    const std::size_t insert_at = has_placement ? last_placement_line + 1 : lines_.size();
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(insert_at), format_record(record));
    return {uuid};
}

bool SceneDocument::destroy_prefab_instance(const EntityUuid uuid) {
    if (!uuid) {
        throw std::invalid_argument{"Prefab instance removal requires a non-zero UUID."};
    }
    std::size_t index = 0;
    TextRecord record;
    PlacementLayout layout;
    if (!find_placement(lines_, uuid, index, record, layout)) {
        return false;
    }
    if (layout.prefab == 0) {
        throw std::invalid_argument{
            "Authored entity records are not prefab instances and cannot be removed this way."};
    }
    const std::string instance_id = record.fields[0];
    for (const std::string& line : lines_) {
        TextRecord referencing;
        if (parse_record(line, referencing) && referencing.key == "animation_binding" &&
            referencing.fields.size() == animation_binding_field_count &&
            referencing.fields[0] == instance_id) {
            throw std::invalid_argument{
                "Animation bindings still reference this prefab instance: " + instance_id};
        }
        if (parse_record(line, referencing) && referencing.key == "animation_auto" &&
            referencing.fields.size() == animation_auto_field_count &&
            referencing.fields[0] == instance_id) {
            throw std::invalid_argument{
                "Automatic animations still reference this prefab instance: " + instance_id};
        }
    }

    std::vector<std::string> remaining;
    remaining.reserve(lines_.size());
    for (std::size_t line_index = 0; line_index < lines_.size(); ++line_index) {
        if (line_index == index) {
            continue;
        }
        TextRecord candidate;
        if (parse_record(lines_[line_index], candidate) && candidate.key == "prefab_override" &&
            candidate.fields.size() == prefab_override_field_count &&
            candidate.fields[0] == instance_id) {
            continue;
        }
        remaining.push_back(lines_[line_index]);
    }
    lines_ = std::move(remaining);
    return true;
}

SceneDefinition SceneDocument::runtime_copy() const {
    const std::filesystem::path candidate = source_path_.parent_path() /
        (source_path_.filename().string() + ".ic2de.runtime-copy.tmp");
    std::error_code cleanup_error;
    try {
        write_candidate(candidate, lines_);
        SceneDefinition result = SceneDefinition::load(candidate);
        std::filesystem::remove(candidate, cleanup_error);
        return result;
    } catch (...) {
        std::filesystem::remove(candidate, cleanup_error);
        throw;
    }
}

void SceneDocument::save_atomic(const std::filesystem::path& destination) const {
    const std::filesystem::path absolute_destination =
        std::filesystem::absolute(destination).lexically_normal();
    if (absolute_destination.filename().empty() ||
        !std::filesystem::is_directory(absolute_destination.parent_path())) {
        throw std::invalid_argument{"Scene save destination must have an existing parent directory."};
    }
    const std::filesystem::path candidate = absolute_destination.parent_path() /
        (absolute_destination.filename().string() + ".ic2de.tmp");
    std::error_code cleanup_error;
    try {
        write_candidate(candidate, lines_);
        static_cast<void>(SceneDefinition::load(candidate));
        replace_file(candidate, absolute_destination);
    } catch (...) {
        std::filesystem::remove(candidate, cleanup_error);
        throw;
    }
}

} // namespace ic2d
