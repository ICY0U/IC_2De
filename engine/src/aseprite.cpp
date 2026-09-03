#include "ic2d/aseprite.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace ic2d {
namespace {

using Json = nlohmann::json;

[[noreturn]] void fail(const std::filesystem::path& path, const std::string& message) {
    throw std::runtime_error{"Aseprite metadata " + path.string() + ": " + message};
}

[[nodiscard]] bool safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_path()) {
        return false;
    }
    for (const std::filesystem::path& part : path) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_id(const std::string_view id) noexcept {
    if (id.empty()) {
        return false;
    }
    return std::ranges::all_of(id, [](const char character) {
        const bool alpha_numeric = (character >= 'a' && character <= 'z') ||
                                   (character >= 'A' && character <= 'Z') ||
                                   (character >= '0' && character <= '9');
        return alpha_numeric || character == '_' || character == '-' || character == '.';
    });
}

[[nodiscard]] const Json& required(const Json& object, const std::string_view key,
                                   const std::filesystem::path& path) {
    if (!object.is_object()) {
        fail(path, "Expected a JSON object while reading '" + std::string{key} + "'.");
    }
    const auto found = object.find(key);
    if (found == object.end()) {
        fail(path, "Missing required field '" + std::string{key} + "'.");
    }
    return *found;
}

[[nodiscard]] std::string string_field(const Json& object, const std::string_view key,
                                       const std::filesystem::path& path) {
    const Json& value = required(object, key, path);
    if (!value.is_string()) {
        fail(path, "Field '" + std::string{key} + "' must be a string.");
    }
    return value.get<std::string>();
}

[[nodiscard]] bool optional_bool_field(const Json& object, const std::string_view key,
                                       const std::filesystem::path& path) {
    const auto found = object.find(key);
    if (found == object.end()) {
        return false;
    }
    if (!found->is_boolean()) {
        fail(path, "Optional field '" + std::string{key} + "' must be a boolean.");
    }
    return found->get<bool>();
}

[[nodiscard]] std::uint32_t unsigned_field(const Json& object, const std::string_view key,
                                           const std::filesystem::path& path,
                                           const bool allow_zero) {
    const Json& value = required(object, key, path);
    if (!value.is_number_integer()) {
        fail(path, "Field '" + std::string{key} + "' must be an integer.");
    }
    std::uint64_t parsed = 0;
    if (value.is_number_unsigned()) {
        parsed = value.get<std::uint64_t>();
    } else {
        const std::int64_t signed_value = value.get<std::int64_t>();
        if (signed_value < (allow_zero ? 0 : 1)) {
            fail(path, "Field '" + std::string{key} + "' is outside its supported range.");
        }
        parsed = static_cast<std::uint64_t>(signed_value);
    }
    if ((!allow_zero && parsed == 0) || parsed > std::numeric_limits<std::uint32_t>::max()) {
        fail(path, "Field '" + std::string{key} + "' is outside its supported range.");
    }
    return static_cast<std::uint32_t>(parsed);
}

[[nodiscard]] std::uint32_t duration_ticks(const std::uint32_t duration_ms,
                                           const std::uint32_t fixed_update_hz,
                                           const std::filesystem::path& path) {
    const std::uint64_t rounded =
        (static_cast<std::uint64_t>(duration_ms) * fixed_update_hz + 500U) / 1000U;
    const std::uint64_t clamped = std::max<std::uint64_t>(1U, rounded);
    if (clamped > std::numeric_limits<std::uint32_t>::max()) {
        fail(path, "A frame duration overflows deterministic animation ticks.");
    }
    return static_cast<std::uint32_t>(clamped);
}

[[nodiscard]] std::vector<std::string> frame_events(const Json& frame,
                                                    const std::filesystem::path& path) {
    const auto found = frame.find("ic2d_events");
    if (found == frame.end()) {
        return {};
    }
    if (!found->is_array()) {
        fail(path, "Optional 'ic2d_events' must be an array of event IDs.");
    }
    std::vector<std::string> events;
    std::unordered_set<std::string> unique;
    for (const Json& value : *found) {
        if (!value.is_string()) {
            fail(path, "Every 'ic2d_events' entry must be a string.");
        }
        std::string event = value.get<std::string>();
        if (!valid_id(event) || !unique.insert(event).second) {
            fail(path, "Frame event IDs must be valid and unique per frame.");
        }
        events.push_back(std::move(event));
    }
    return events;
}

struct ImportedFrame {
    AnimationFrame animation;
};

[[nodiscard]] ImportedFrame import_frame(const Json& authored, const std::uint32_t sheet_width,
                                         const std::uint32_t sheet_height,
                                         const std::uint32_t fixed_update_hz,
                                         const std::filesystem::path& path) {
    if (!authored.is_object()) {
        fail(path, "Every 'frames' entry must be an object; use --format json-array.");
    }
    for (const std::string_view flag : {std::string_view{"rotated"}, std::string_view{"trimmed"}}) {
        const auto found = authored.find(flag);
        if (found != authored.end() && (!found->is_boolean() || found->get<bool>())) {
            fail(path, "Rotated or trimmed frames are unsupported; export an untrimmed sheet.");
        }
    }
    const Json& rectangle = required(authored, "frame", path);
    const std::uint32_t x = unsigned_field(rectangle, "x", path, true);
    const std::uint32_t y = unsigned_field(rectangle, "y", path, true);
    const std::uint32_t width = unsigned_field(rectangle, "w", path, false);
    const std::uint32_t height = unsigned_field(rectangle, "h", path, false);
    if (static_cast<std::uint64_t>(x) + width > sheet_width ||
        static_cast<std::uint64_t>(y) + height > sheet_height) {
        fail(path, "A frame rectangle lies outside 'meta.size'.");
    }
    const std::uint32_t milliseconds = unsigned_field(authored, "duration", path, false);
    return {
        .animation =
            {
                .source =
                    {
                        static_cast<float>(x),
                        static_cast<float>(y),
                        static_cast<float>(width),
                        static_cast<float>(height),
                    },
                .duration_ticks = duration_ticks(milliseconds, fixed_update_hz, path),
                .events = frame_events(authored, path),
                .flip_x = optional_bool_field(authored, "ic2d_flip_x", path),
            },
    };
}

[[nodiscard]] AnimationLoopMode loop_mode(const std::string_view direction,
                                          const std::filesystem::path& path) {
    if (direction == "forward" || direction == "reverse") {
        return AnimationLoopMode::loop;
    }
    if (direction == "pingpong" || direction == "pingpong_reverse") {
        return AnimationLoopMode::ping_pong;
    }
    fail(path, "Tag direction must be forward, reverse, pingpong, or pingpong_reverse.");
}

} // namespace

AsepriteImportResult import_aseprite_json(const std::filesystem::path& metadata_path,
                                          const std::uint32_t fixed_update_hz) {
    const std::filesystem::path absolute_path =
        std::filesystem::absolute(metadata_path).lexically_normal();
    if (fixed_update_hz == 0) {
        fail(absolute_path, "The fixed update rate must be positive.");
    }

    std::ifstream stream{absolute_path};
    if (!stream) {
        fail(absolute_path, "File could not be opened.");
    }
    Json document;
    try {
        stream >> document;
    } catch (const Json::exception& error) {
        fail(absolute_path, "Invalid JSON: " + std::string{error.what()});
    }
    if (!document.is_object()) {
        fail(absolute_path, "The document root must be an object.");
    }

    const Json& frames_json = required(document, "frames", absolute_path);
    if (!frames_json.is_array() || frames_json.empty()) {
        fail(absolute_path, "'frames' must be a non-empty array; export with --format json-array.");
    }
    const Json& meta = required(document, "meta", absolute_path);
    const Json& size = required(meta, "size", absolute_path);
    const std::uint32_t sheet_width = unsigned_field(size, "w", absolute_path, false);
    const std::uint32_t sheet_height = unsigned_field(size, "h", absolute_path, false);

    const std::filesystem::path image = string_field(meta, "image", absolute_path);
    if (!safe_relative_path(image)) {
        fail(absolute_path, "'meta.image' must be a safe relative path.");
    }

    std::vector<ImportedFrame> frames;
    frames.reserve(frames_json.size());
    for (const Json& frame : frames_json) {
        frames.push_back(
            import_frame(frame, sheet_width, sheet_height, fixed_update_hz, absolute_path));
    }

    const Json& tags = required(meta, "frameTags", absolute_path);
    if (!tags.is_array() || tags.empty()) {
        fail(absolute_path, "'meta.frameTags' must be non-empty; export with --list-tags.");
    }

    AsepriteImportResult result{
        .atlas_path = (absolute_path.parent_path() / image).lexically_normal(),
    };
    result.clips.reserve(tags.size());
    std::unordered_set<std::string> clip_ids;
    for (const Json& tag : tags) {
        const std::string name = string_field(tag, "name", absolute_path);
        if (!valid_id(name) || !clip_ids.insert(name).second) {
            fail(absolute_path, "Tag names must be valid, unique engine clip IDs.");
        }
        const std::uint32_t from = unsigned_field(tag, "from", absolute_path, true);
        const std::uint32_t to = unsigned_field(tag, "to", absolute_path, true);
        if (from > to || to >= frames.size()) {
            fail(absolute_path, "Tag '" + name + "' has an invalid inclusive frame range.");
        }
        std::string direction = "forward";
        const auto direction_field = tag.find("direction");
        if (direction_field != tag.end()) {
            if (!direction_field->is_string()) {
                fail(absolute_path, "Tag 'direction' must be a string.");
            }
            direction = direction_field->get<std::string>();
        }

        AnimationLoopMode clip_loop_mode = loop_mode(direction, absolute_path);
        const auto loop_mode_field = tag.find("ic2d_loop_mode");
        if (loop_mode_field != tag.end()) {
            if (!loop_mode_field->is_string() || loop_mode_field->get<std::string>() != "once") {
                fail(absolute_path, "Tag 'ic2d_loop_mode' must be 'once' when present.");
            }
            clip_loop_mode = AnimationLoopMode::once;
        }

        AnimationClip clip{.id = name, .loop_mode = clip_loop_mode};
        clip.frames.reserve(static_cast<std::size_t>(to - from) + 1U);
        if (direction == "reverse" || direction == "pingpong_reverse") {
            for (std::uint32_t index = to;; --index) {
                clip.frames.push_back(frames[index].animation);
                if (index == from) {
                    break;
                }
            }
        } else {
            for (std::uint32_t index = from; index <= to; ++index) {
                clip.frames.push_back(frames[index].animation);
            }
        }
        result.clips.push_back(std::move(clip));
    }

    try {
        static_cast<void>(AnimationPlayer{result.clips, result.clips.front().id});
    } catch (const std::exception& error) {
        fail(absolute_path, "Imported clip validation failed: " + std::string{error.what()});
    }
    return result;
}

} // namespace ic2d
