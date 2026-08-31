#include "ic2d/scene.hpp"

#include "ic2d/aseprite.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ic2d {
namespace {

constexpr std::uint32_t supported_schema_version = 7;

struct AuthoredValue {
    std::string value;
    std::size_t line{0};
};

struct ImportedAsepriteDefinition {
    std::string texture_id;
    std::size_t line{0};
    std::vector<AnimationClip> clips;
};

[[nodiscard]] std::string_view trim(const std::string_view value) noexcept {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

[[noreturn]] void fail(
    const std::filesystem::path& path,
    const std::size_t line,
    const std::string& message
) {
    throw std::runtime_error{
        "Scene " + path.string() + (line > 0 ? ":" + std::to_string(line) : "") + ": " + message};
}

[[nodiscard]] std::vector<std::string_view> fields(const std::string_view value) {
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t separator = value.find('|', start);
        const std::size_t end = separator == std::string_view::npos ? value.size() : separator;
        result.push_back(trim(value.substr(start, end - start)));
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    return result;
}

template <typename Number>
[[nodiscard]] Number number(
    const std::string_view value,
    const std::filesystem::path& path,
    const std::size_t line,
    const std::string_view field_name
) {
    Number parsed{};
    const auto conversion = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (value.empty() || conversion.ec != std::errc{} ||
        conversion.ptr != value.data() + value.size()) {
        fail(path, line, std::string{field_name} + " must be a valid number.");
    }
    if constexpr (std::is_floating_point_v<Number>) {
        if (!std::isfinite(parsed)) {
            fail(path, line, std::string{field_name} + " must be finite.");
        }
    }
    return parsed;
}

[[nodiscard]] bool boolean(
    const std::string_view value,
    const std::filesystem::path& path,
    const std::size_t line,
    const std::string_view field_name
) {
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    fail(path, line, std::string{field_name} + " must be true or false.");
}

[[nodiscard]] std::uint8_t color_channel(
    const std::string_view value,
    const std::filesystem::path& path,
    const std::size_t line
) {
    const std::uint32_t parsed = number<std::uint32_t>(value, path, line, "color channel");
    if (parsed > 255U) {
        fail(path, line, "Color channels must be between 0 and 255.");
    }
    return static_cast<std::uint8_t>(parsed);
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

void require_field_count(
    const std::vector<std::string_view>& parsed_fields,
    const std::size_t expected,
    const std::filesystem::path& path,
    const std::size_t line,
    const std::string_view record_name
) {
    if (parsed_fields.size() != expected) {
        fail(path, line, std::string{record_name} + " requires " + std::to_string(expected) +
                             " pipe-separated fields.");
    }
}

[[nodiscard]] SceneTextureSampling texture_sampling(
    const std::string_view value,
    const std::filesystem::path& path,
    const std::size_t line
) {
    if (value == "pixel") {
        return SceneTextureSampling::pixel;
    }
    if (value == "smooth") {
        return SceneTextureSampling::smooth;
    }
    fail(path, line, "Texture sampling must be pixel or smooth.");
}

[[nodiscard]] PhysicsMotionType motion_type(
    const std::string_view value,
    const std::filesystem::path& path,
    const std::size_t line
) {
    if (value == "static") {
        return PhysicsMotionType::static_body;
    }
    if (value == "kinematic") {
        return PhysicsMotionType::kinematic_body;
    }
    if (value == "dynamic") {
        return PhysicsMotionType::dynamic_body;
    }
    fail(path, line, "Physics motion must be static, kinematic, or dynamic.");
}

[[nodiscard]] ScenePhysicsRole physics_role(
    const std::string_view value,
    const std::filesystem::path& path,
    const std::size_t line
) {
    if (value == "player") {
        return ScenePhysicsRole::player;
    }
    if (value == "primary_prop") {
        return ScenePhysicsRole::primary_prop;
    }
    if (value == "enemy") {
        return ScenePhysicsRole::enemy;
    }
    if (value == "generic") {
        return ScenePhysicsRole::generic;
    }
    fail(path, line, "Physics role must be player, primary_prop, enemy, or generic.");
}

[[nodiscard]] AnimationLoopMode animation_loop_mode(
    const std::string_view value,
    const std::filesystem::path& path,
    const std::size_t line
) {
    if (value == "once") {
        return AnimationLoopMode::once;
    }
    if (value == "loop") {
        return AnimationLoopMode::loop;
    }
    if (value == "ping_pong") {
        return AnimationLoopMode::ping_pong;
    }
    fail(path, line, "Animation loop mode must be once, loop, or ping_pong.");
}

[[nodiscard]] LocomotionState locomotion_state(
    const std::string_view value,
    const std::filesystem::path& path,
    const std::size_t line
) {
    if (value == "idle_south") {
        return LocomotionState::idle_south;
    }
    if (value == "idle_southwest") {
        return LocomotionState::idle_southwest;
    }
    if (value == "idle_west") {
        return LocomotionState::idle_west;
    }
    if (value == "idle_northwest") {
        return LocomotionState::idle_northwest;
    }
    if (value == "idle_north") {
        return LocomotionState::idle_north;
    }
    if (value == "idle_northeast") {
        return LocomotionState::idle_northeast;
    }
    if (value == "idle_east") {
        return LocomotionState::idle_east;
    }
    if (value == "idle_southeast") {
        return LocomotionState::idle_southeast;
    }
    if (value == "move_south") {
        return LocomotionState::move_south;
    }
    if (value == "move_southwest") {
        return LocomotionState::move_southwest;
    }
    if (value == "move_west") {
        return LocomotionState::move_west;
    }
    if (value == "move_northwest") {
        return LocomotionState::move_northwest;
    }
    if (value == "move_north") {
        return LocomotionState::move_north;
    }
    if (value == "move_northeast") {
        return LocomotionState::move_northeast;
    }
    if (value == "move_east") {
        return LocomotionState::move_east;
    }
    if (value == "move_southeast") {
        return LocomotionState::move_southeast;
    }
    fail(path, line,
         "Animation state must be idle/move plus one of eight compass directions.");
}

[[nodiscard]] std::vector<std::string> animation_events(
    const std::string_view value,
    const std::filesystem::path& path,
    const std::size_t line
) {
    if (value == "-") {
        return {};
    }
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t separator = value.find(',', start);
        const std::size_t end = separator == std::string_view::npos ? value.size() : separator;
        const std::string_view event = trim(value.substr(start, end - start));
        if (!valid_id(event)) {
            fail(path, line, "Animation event names contain unsupported characters.");
        }
        result.emplace_back(event);
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    return result;
}

[[nodiscard]] std::vector<std::string_view> comma_fields(const std::string_view value) {
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t separator = value.find(',', start);
        const std::size_t end = separator == std::string_view::npos ? value.size() : separator;
        result.push_back(trim(value.substr(start, end - start)));
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    return result;
}

void validate_sprite(
    const SceneSpriteDefinition& sprite,
    const std::filesystem::path& path,
    const std::size_t line,
    const std::unordered_set<std::string>& texture_ids
) {
    if (sprite.size.x <= 0.0F || sprite.size.y <= 0.0F ||
        sprite.normalized_origin.x < 0.0F || sprite.normalized_origin.x > 1.0F ||
        sprite.normalized_origin.y < 0.0F || sprite.normalized_origin.y > 1.0F) {
        fail(path, line, "Sprite size and normalized origin are invalid.");
    }
    if (!sprite.texture_id.empty() && !texture_ids.contains(sprite.texture_id)) {
        fail(path, line, "Sprite references an unknown texture: " + sprite.texture_id);
    }
}

// Sprite fields have one shape wherever they appear: width, height, origin X,
// origin Y, RGBA, layer, and texture id.
[[nodiscard]] SceneSpriteDefinition sprite_definition(
    const std::vector<std::string_view>& parsed_fields,
    const std::size_t base,
    const std::filesystem::path& path,
    const std::size_t line,
    const std::unordered_set<std::string>& texture_ids
) {
    SceneSpriteDefinition sprite;
    sprite.size = {
        number<float>(parsed_fields[base], path, line, "sprite width"),
        number<float>(parsed_fields[base + 1], path, line, "sprite height"),
    };
    sprite.normalized_origin = {
        number<float>(parsed_fields[base + 2], path, line, "sprite origin X"),
        number<float>(parsed_fields[base + 3], path, line, "sprite origin Y"),
    };
    sprite.tint = {
        color_channel(parsed_fields[base + 4], path, line),
        color_channel(parsed_fields[base + 5], path, line),
        color_channel(parsed_fields[base + 6], path, line),
        color_channel(parsed_fields[base + 7], path, line),
    };
    sprite.layer = number<std::int32_t>(parsed_fields[base + 8], path, line, "sprite layer");
    sprite.texture_id =
        parsed_fields[base + 9] == "-" ? "" : std::string{parsed_fields[base + 9]};
    validate_sprite(sprite, path, line, texture_ids);
    return sprite;
}

struct PrefabOverride {
    std::string field;
    std::string value;
    std::size_t line{0};
};

void apply_prefab_override(
    SceneSpriteDefinition& sprite,
    const PrefabOverride& override_record,
    const std::filesystem::path& path
) {
    const auto values = comma_fields(override_record.value);
    const std::size_t line = override_record.line;
    const auto require_values = [&](const std::size_t expected) {
        if (values.size() != expected) {
            fail(path, line, "Prefab override " + override_record.field + " requires " +
                                 std::to_string(expected) + " comma-separated values.");
        }
    };
    if (override_record.field == "sprite_size") {
        require_values(2);
        sprite.size = {
            number<float>(values[0], path, line, "sprite width"),
            number<float>(values[1], path, line, "sprite height"),
        };
    } else if (override_record.field == "sprite_origin") {
        require_values(2);
        sprite.normalized_origin = {
            number<float>(values[0], path, line, "sprite origin X"),
            number<float>(values[1], path, line, "sprite origin Y"),
        };
    } else if (override_record.field == "tint") {
        require_values(4);
        sprite.tint = {
            color_channel(values[0], path, line),
            color_channel(values[1], path, line),
            color_channel(values[2], path, line),
            color_channel(values[3], path, line),
        };
    } else if (override_record.field == "layer") {
        require_values(1);
        sprite.layer = number<std::int32_t>(values[0], path, line, "sprite layer");
    } else if (override_record.field == "texture") {
        require_values(1);
        sprite.texture_id = values[0] == "-" ? "" : std::string{values[0]};
    } else {
        fail(path, line, "Prefab override field must be sprite_size, sprite_origin, tint, "
                         "layer, or texture.");
    }
}

[[nodiscard]] GroundAreaKind ground_kind(
    const std::string_view value,
    const std::filesystem::path& path,
    const std::size_t line
) {
    if (value == "solid") {
        return GroundAreaKind::solid;
    }
    if (value == "elevation") {
        return GroundAreaKind::elevation;
    }
    if (value == "trigger") {
        return GroundAreaKind::trigger;
    }
    fail(path, line, "Ground area kind must be solid, elevation, or trigger.");
}

[[nodiscard]] const AuthoredValue& required(
    const std::unordered_map<std::string, AuthoredValue>& settings,
    const std::string_view key,
    const std::filesystem::path& path
) {
    const auto found = settings.find(std::string{key});
    if (found == settings.end()) {
        fail(path, 0, "Missing required setting: " + std::string{key});
    }
    return found->second;
}

} // namespace

SceneDefinition SceneDefinition::load(const std::filesystem::path& scene_path) {
    const std::filesystem::path absolute_path =
        std::filesystem::absolute(scene_path).lexically_normal();
    std::ifstream stream{absolute_path};
    if (!stream) {
        fail(absolute_path, 0, "File could not be opened.");
    }

    std::unordered_map<std::string, AuthoredValue> settings;
    std::vector<AuthoredValue> texture_records;
    std::vector<AuthoredValue> aseprite_records;
    std::vector<AuthoredValue> ground_records;
    std::vector<AuthoredValue> physics_records;
    std::vector<AuthoredValue> prefab_records;
    std::vector<AuthoredValue> prefab_instance_records;
    std::vector<AuthoredValue> prefab_override_records;
    std::vector<AuthoredValue> entity_records;
    std::vector<AuthoredValue> animation_clip_records;
    std::vector<AuthoredValue> animation_frame_records;
    std::vector<AuthoredValue> animation_binding_records;
    const std::unordered_set<std::string> singleton_keys{
        "schema", "id", "world_space", "ground_plane", "elevation_axis",
        "walkable_bounds", "max_step_height", "camera", "physics",
        "ground_filter", "trigger_filter", "player_speed",
    };

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
            fail(absolute_path, line_number, "Expected key=value syntax.");
        }
        const std::string key{trim(cleaned.substr(0, separator))};
        AuthoredValue authored{std::string{trim(cleaned.substr(separator + 1))}, line_number};
        if (key.empty() || authored.value.empty()) {
            fail(absolute_path, line_number, "Keys and values cannot be empty.");
        }
        if (key == "texture") {
            texture_records.push_back(std::move(authored));
        } else if (key == "aseprite") {
            aseprite_records.push_back(std::move(authored));
        } else if (key == "ground_area") {
            ground_records.push_back(std::move(authored));
        } else if (key == "physics_box") {
            physics_records.push_back(std::move(authored));
        } else if (key == "prefab") {
            prefab_records.push_back(std::move(authored));
        } else if (key == "prefab_instance") {
            prefab_instance_records.push_back(std::move(authored));
        } else if (key == "prefab_override") {
            prefab_override_records.push_back(std::move(authored));
        } else if (key == "entity") {
            entity_records.push_back(std::move(authored));
        } else if (key == "animation_clip") {
            animation_clip_records.push_back(std::move(authored));
        } else if (key == "animation_frame") {
            animation_frame_records.push_back(std::move(authored));
        } else if (key == "animation_binding") {
            animation_binding_records.push_back(std::move(authored));
        } else if (!singleton_keys.contains(key)) {
            fail(absolute_path, line_number, "Unsupported setting: " + key);
        } else if (!settings.emplace(key, std::move(authored)).second) {
            fail(absolute_path, line_number, "Duplicate setting: " + key);
        }
    }

    SceneDefinition scene;
    scene.source_path_ = absolute_path;
    const AuthoredValue& schema = required(settings, "schema", absolute_path);
    scene.schema_version_ = number<std::uint32_t>(schema.value, absolute_path, schema.line, "schema");
    if (scene.schema_version_ != supported_schema_version) {
        fail(absolute_path, schema.line,
             "Unsupported schema version " + std::to_string(scene.schema_version_) + ".");
    }

    const AuthoredValue& id = required(settings, "id", absolute_path);
    scene.id_ = id.value;
    if (!valid_id(scene.id_)) {
        fail(absolute_path, id.line, "Scene id contains unsupported characters.");
    }
    const AuthoredValue& world_space = required(settings, "world_space", absolute_path);
    const AuthoredValue& ground_plane = required(settings, "ground_plane", absolute_path);
    const AuthoredValue& elevation_axis = required(settings, "elevation_axis", absolute_path);
    if (world_space.value != "x_y_z" || ground_plane.value != "x_z" ||
        elevation_axis.value != "y") {
        fail(absolute_path, world_space.line,
             "IC_2DE scenes require world_space=x_y_z, ground_plane=x_z, and elevation_axis=y.");
    }

    const AuthoredValue& bounds = required(settings, "walkable_bounds", absolute_path);
    const auto bounds_fields = fields(bounds.value);
    require_field_count(bounds_fields, 4, absolute_path, bounds.line, "walkable_bounds");
    scene.ground_.walkable_bounds = {
        number<float>(bounds_fields[0], absolute_path, bounds.line, "bounds X"),
        number<float>(bounds_fields[1], absolute_path, bounds.line, "bounds Z"),
        number<float>(bounds_fields[2], absolute_path, bounds.line, "bounds width"),
        number<float>(bounds_fields[3], absolute_path, bounds.line, "bounds depth"),
    };
    const AuthoredValue& max_step = required(settings, "max_step_height", absolute_path);
    scene.ground_.max_step_height =
        number<float>(max_step.value, absolute_path, max_step.line, "max step height");

    const AuthoredValue& camera = required(settings, "camera", absolute_path);
    const auto camera_fields = fields(camera.value);
    require_field_count(camera_fields, 4, absolute_path, camera.line, "camera");
    scene.camera_.yaw_degrees = number<float>(camera_fields[0], absolute_path, camera.line, "camera yaw");
    scene.camera_.pitch_degrees = number<float>(camera_fields[1], absolute_path, camera.line, "camera pitch");
    scene.camera_.pixels_per_world_unit =
        number<float>(camera_fields[2], absolute_path, camera.line, "camera scale");
    scene.camera_.zoom = number<float>(camera_fields[3], absolute_path, camera.line, "camera zoom");

    const AuthoredValue& physics = required(settings, "physics", absolute_path);
    const auto physics_fields = fields(physics.value);
    require_field_count(physics_fields, 6, absolute_path, physics.line, "physics");
    scene.simulation_.physics.pixels_per_metre =
        number<float>(physics_fields[0], absolute_path, physics.line, "pixels per metre");
    scene.simulation_.physics.substep_count =
        number<std::int32_t>(physics_fields[1], absolute_path, physics.line, "substep count");
    scene.simulation_.physics.gravity_pixels_per_second_squared = {
        number<float>(physics_fields[2], absolute_path, physics.line, "gravity X"),
        number<float>(physics_fields[3], absolute_path, physics.line, "gravity Z"),
    };
    scene.simulation_.physics.enable_sleep =
        boolean(physics_fields[4], absolute_path, physics.line, "enable sleep");
    scene.simulation_.world_boundary_thickness =
        number<float>(physics_fields[5], absolute_path, physics.line, "world boundary thickness");

    const auto parse_filter = [&absolute_path](const AuthoredValue& authored,
                                                std::uint64_t& category,
                                                std::uint64_t& mask) {
        const auto parsed_fields = fields(authored.value);
        require_field_count(parsed_fields, 2, absolute_path, authored.line, "collision filter");
        category = number<std::uint64_t>(
            parsed_fields[0], absolute_path, authored.line, "category bits");
        mask = number<std::uint64_t>(parsed_fields[1], absolute_path, authored.line, "mask bits");
        if (category == 0) {
            fail(absolute_path, authored.line, "Collision category bits cannot be zero.");
        }
    };
    parse_filter(required(settings, "ground_filter", absolute_path),
                 scene.simulation_.ground_category_bits, scene.simulation_.ground_mask_bits);
    parse_filter(required(settings, "trigger_filter", absolute_path),
                 scene.simulation_.trigger_category_bits, scene.simulation_.trigger_mask_bits);
    const AuthoredValue& speed = required(settings, "player_speed", absolute_path);
    scene.simulation_.player_speed = number<float>(speed.value, absolute_path, speed.line, "player speed");

    std::unordered_set<std::string> texture_ids;
    for (const AuthoredValue& record : texture_records) {
        const auto parsed_fields = fields(record.value);
        if (parsed_fields.size() < 2) {
            fail(absolute_path, record.line, "Texture records require an id and kind.");
        }
        SceneTextureDefinition texture;
        texture.id = parsed_fields[0];
        if (!valid_id(texture.id) || !texture_ids.insert(texture.id).second) {
            fail(absolute_path, record.line, "Texture ids must be valid and unique.");
        }
        if (parsed_fields[1] == "file") {
            require_field_count(parsed_fields, 4, absolute_path, record.line, "file texture");
            texture.kind = SceneTextureKind::file;
            texture.relative_path = parsed_fields[2];
            texture.sampling = texture_sampling(parsed_fields[3], absolute_path, record.line);
            if (!safe_relative_path(texture.relative_path)) {
                fail(absolute_path, record.line, "Texture files must use safe relative paths.");
            }
            const std::filesystem::path resolved =
                (absolute_path.parent_path() / texture.relative_path).lexically_normal();
            if (!std::filesystem::is_regular_file(resolved)) {
                fail(absolute_path, record.line, "Texture file is missing: " + resolved.string());
            }
        } else if (parsed_fields[1] == "checker") {
            require_field_count(parsed_fields, 14, absolute_path, record.line, "checker texture");
            texture.kind = SceneTextureKind::checker;
            texture.width = number<int>(parsed_fields[2], absolute_path, record.line, "texture width");
            texture.height = number<int>(parsed_fields[3], absolute_path, record.line, "texture height");
            texture.cell_size = number<int>(parsed_fields[4], absolute_path, record.line, "cell size");
            texture.first_color = {
                color_channel(parsed_fields[5], absolute_path, record.line),
                color_channel(parsed_fields[6], absolute_path, record.line),
                color_channel(parsed_fields[7], absolute_path, record.line),
                color_channel(parsed_fields[8], absolute_path, record.line),
            };
            texture.second_color = {
                color_channel(parsed_fields[9], absolute_path, record.line),
                color_channel(parsed_fields[10], absolute_path, record.line),
                color_channel(parsed_fields[11], absolute_path, record.line),
                color_channel(parsed_fields[12], absolute_path, record.line),
            };
            texture.sampling = texture_sampling(parsed_fields[13], absolute_path, record.line);
            if (texture.width <= 0 || texture.height <= 0 || texture.cell_size <= 0) {
                fail(absolute_path, record.line, "Checker dimensions must be positive.");
            }
        } else if (parsed_fields[1] == "radial") {
            require_field_count(parsed_fields, 13, absolute_path, record.line, "radial texture");
            texture.kind = SceneTextureKind::radial;
            texture.width = number<int>(parsed_fields[2], absolute_path, record.line, "texture width");
            texture.height = number<int>(parsed_fields[3], absolute_path, record.line, "texture height");
            texture.first_color = {
                color_channel(parsed_fields[4], absolute_path, record.line),
                color_channel(parsed_fields[5], absolute_path, record.line),
                color_channel(parsed_fields[6], absolute_path, record.line),
                color_channel(parsed_fields[7], absolute_path, record.line),
            };
            texture.second_color = {
                color_channel(parsed_fields[8], absolute_path, record.line),
                color_channel(parsed_fields[9], absolute_path, record.line),
                color_channel(parsed_fields[10], absolute_path, record.line),
                color_channel(parsed_fields[11], absolute_path, record.line),
            };
            texture.sampling = texture_sampling(parsed_fields[12], absolute_path, record.line);
            if (texture.width <= 0 || texture.height <= 0) {
                fail(absolute_path, record.line, "Radial dimensions must be positive.");
            }
        } else {
            fail(absolute_path, record.line, "Texture kind must be file, checker, or radial.");
        }
        scene.textures_.push_back(std::move(texture));
    }

    std::vector<ImportedAsepriteDefinition> imported_aseprite;
    imported_aseprite.reserve(aseprite_records.size());
    for (const AuthoredValue& record : aseprite_records) {
        const auto parsed_fields = fields(record.value);
        require_field_count(parsed_fields, 4, absolute_path, record.line, "aseprite");
        const std::string texture_id{parsed_fields[0]};
        if (!valid_id(texture_id) || !texture_ids.insert(texture_id).second) {
            fail(absolute_path, record.line,
                 "Aseprite texture ids must be valid and unique across all textures.");
        }
        const std::filesystem::path metadata_path{parsed_fields[1]};
        if (!safe_relative_path(metadata_path)) {
            fail(absolute_path, record.line,
                 "Aseprite metadata files must use safe relative paths.");
        }
        const SceneTextureSampling sampling =
            texture_sampling(parsed_fields[2], absolute_path, record.line);
        const std::uint32_t fixed_hz = number<std::uint32_t>(
            parsed_fields[3], absolute_path, record.line, "Aseprite fixed update rate");

        AsepriteImportResult imported;
        try {
            imported = import_aseprite_json(
                (absolute_path.parent_path() / metadata_path).lexically_normal(), fixed_hz);
        } catch (const std::exception& error) {
            fail(absolute_path, record.line,
                 "Aseprite import failed: " + std::string{error.what()});
        }
        const std::filesystem::path atlas_relative =
            imported.atlas_path.lexically_relative(absolute_path.parent_path());
        if (!safe_relative_path(atlas_relative) ||
            !std::filesystem::is_regular_file(imported.atlas_path)) {
            fail(absolute_path, record.line,
                 "Imported Aseprite atlas is missing or escapes the scene directory: " +
                     imported.atlas_path.string());
        }
        scene.textures_.push_back({
            .id = texture_id,
            .kind = SceneTextureKind::file,
            .sampling = sampling,
            .relative_path = atlas_relative,
        });
        imported_aseprite.push_back({
            .texture_id = texture_id,
            .line = record.line,
            .clips = std::move(imported.clips),
        });
    }

    for (const AuthoredValue& record : ground_records) {
        const auto parsed_fields = fields(record.value);
        require_field_count(parsed_fields, 7, absolute_path, record.line, "ground_area");
        scene.ground_.areas.push_back({
            .bounds = {
                number<float>(parsed_fields[1], absolute_path, record.line, "ground X"),
                number<float>(parsed_fields[2], absolute_path, record.line, "ground Z"),
                number<float>(parsed_fields[3], absolute_path, record.line, "ground width"),
                number<float>(parsed_fields[4], absolute_path, record.line, "ground depth"),
            },
            .kind = ground_kind(parsed_fields[0], absolute_path, record.line),
            .elevation = number<float>(parsed_fields[5], absolute_path, record.line, "ground elevation"),
            .tag = number<std::uint32_t>(parsed_fields[6], absolute_path, record.line, "ground tag"),
        });
    }

    std::unordered_map<std::string, std::size_t> body_indices;
    std::size_t player_count = 0;
    std::size_t primary_prop_count = 0;
    for (const AuthoredValue& record : physics_records) {
        const auto parsed_fields = fields(record.value);
        require_field_count(parsed_fields, 16, absolute_path, record.line, "physics_box");
        ScenePhysicsBodyDefinition body;
        body.id = parsed_fields[0];
        if (!valid_id(body.id) || body_indices.contains(body.id)) {
            fail(absolute_path, record.line, "Physics body ids must be valid and unique.");
        }
        body.role = physics_role(parsed_fields[1], absolute_path, record.line);
        body.box.motion = motion_type(parsed_fields[2], absolute_path, record.line);
        body.box.center = {
            number<float>(parsed_fields[3], absolute_path, record.line, "body X"),
            number<float>(parsed_fields[4], absolute_path, record.line, "body Z"),
        };
        body.box.half_extents = {
            number<float>(parsed_fields[5], absolute_path, record.line, "body half width"),
            number<float>(parsed_fields[6], absolute_path, record.line, "body half depth"),
        };
        body.box.category_bits =
            number<std::uint64_t>(parsed_fields[7], absolute_path, record.line, "body category bits");
        body.box.mask_bits =
            number<std::uint64_t>(parsed_fields[8], absolute_path, record.line, "body mask bits");
        body.box.tag = number<std::uint32_t>(parsed_fields[9], absolute_path, record.line, "body tag");
        body.box.sensor = boolean(parsed_fields[10], absolute_path, record.line, "body sensor");
        body.box.fixed_rotation =
            boolean(parsed_fields[11], absolute_path, record.line, "fixed rotation");
        body.box.linear_damping =
            number<float>(parsed_fields[12], absolute_path, record.line, "linear damping");
        body.box.angular_damping =
            number<float>(parsed_fields[13], absolute_path, record.line, "angular damping");
        body.box.density = number<float>(parsed_fields[14], absolute_path, record.line, "density");
        body.box.friction = number<float>(parsed_fields[15], absolute_path, record.line, "friction");
        body.box.gravity_scale = 0.0F;
        if (body.role == ScenePhysicsRole::player) {
            ++player_count;
            if (body.box.motion != PhysicsMotionType::kinematic_body) {
                fail(absolute_path, record.line, "The player body must be kinematic.");
            }
        } else if (body.role == ScenePhysicsRole::primary_prop) {
            ++primary_prop_count;
            if (body.box.motion != PhysicsMotionType::dynamic_body) {
                fail(absolute_path, record.line, "The primary prop body must be dynamic.");
            }
        }
        body_indices.emplace(body.id, scene.physics_bodies_.size());
        scene.physics_bodies_.push_back(std::move(body));
    }
    if (player_count != 1 || primary_prop_count != 1) {
        fail(absolute_path, 0, "A scene requires exactly one player and one primary_prop physics body.");
    }

    std::unordered_map<std::string, std::size_t> prefab_indices;
    std::unordered_set<std::uint64_t> identity_uuids;
    for (const AuthoredValue& record : prefab_records) {
        const auto parsed_fields = fields(record.value);
        require_field_count(parsed_fields, 13, absolute_path, record.line, "prefab");
        ScenePrefabDefinition prefab;
        prefab.id = parsed_fields[0];
        prefab.uuid = {number<std::uint64_t>(
            parsed_fields[1], absolute_path, record.line, "prefab UUID")};
        prefab.name = parsed_fields[2];
        if (!valid_id(prefab.id) || prefab_indices.contains(prefab.id) || !prefab.uuid ||
            prefab.name.empty() || !identity_uuids.insert(prefab.uuid.value).second) {
            fail(absolute_path, record.line,
                 "Prefab ids, UUIDs, and names must be valid, non-zero, unique, and non-empty.");
        }
        prefab.sprite =
            sprite_definition(parsed_fields, 3, absolute_path, record.line, texture_ids);
        prefab_indices.emplace(prefab.id, scene.prefabs_.size());
        scene.prefabs_.push_back(std::move(prefab));
    }

    std::unordered_map<std::string, std::vector<PrefabOverride>> prefab_overrides;
    for (const AuthoredValue& record : prefab_override_records) {
        const auto parsed_fields = fields(record.value);
        require_field_count(parsed_fields, 3, absolute_path, record.line, "prefab_override");
        std::vector<PrefabOverride>& overrides = prefab_overrides[std::string{parsed_fields[0]}];
        std::string field_name{parsed_fields[1]};
        if (std::ranges::any_of(overrides, [&field_name](const PrefabOverride& existing) {
                return existing.field == field_name;
            })) {
            fail(absolute_path, record.line,
                 "Prefab instances may override each field at most once: " + field_name);
        }
        overrides.push_back({
            .field = std::move(field_name),
            .value = std::string{parsed_fields[2]},
            .line = record.line,
        });
    }

    // Authored entities and prefab instances share one placement order so that
    // authored source order remains the order gameplay and tools observe.
    struct PlacementRecord {
        const AuthoredValue* record{nullptr};
        bool from_prefab{false};
    };
    std::vector<PlacementRecord> placements;
    placements.reserve(entity_records.size() + prefab_instance_records.size());
    for (const AuthoredValue& record : entity_records) {
        placements.push_back({&record, false});
    }
    for (const AuthoredValue& record : prefab_instance_records) {
        placements.push_back({&record, true});
    }
    std::ranges::sort(placements, {}, [](const PlacementRecord& placement) {
        return placement.record->line;
    });

    std::unordered_map<std::string, std::size_t> entity_indices;
    std::unordered_set<std::string> referenced_bodies;
    std::unordered_set<std::string> instance_ids;
    for (const PlacementRecord& placement : placements) {
        const AuthoredValue& record = *placement.record;
        const auto parsed_fields = fields(record.value);
        SceneEntityDefinition entity;
        if (placement.from_prefab) {
            require_field_count(parsed_fields, 8, absolute_path, record.line, "prefab_instance");
            entity.id = parsed_fields[0];
            entity.uuid = {number<std::uint64_t>(
                parsed_fields[1], absolute_path, record.line, "prefab instance UUID")};
            entity.prefab_id = parsed_fields[2];
            entity.name = parsed_fields[3];
            entity.physics_binding = parsed_fields[4] == "-" ? "" : std::string{parsed_fields[4]};
            entity.position = {
                number<float>(parsed_fields[5], absolute_path, record.line, "entity X"),
                number<float>(parsed_fields[6], absolute_path, record.line, "entity Y"),
                number<float>(parsed_fields[7], absolute_path, record.line, "entity Z"),
            };
            const auto prefab = prefab_indices.find(entity.prefab_id);
            if (prefab == prefab_indices.end()) {
                fail(absolute_path, record.line,
                     "Prefab instance references an unknown prefab: " + entity.prefab_id);
            }
            entity.sprite = scene.prefabs_[prefab->second].sprite;
            const auto overrides = prefab_overrides.find(entity.id);
            if (overrides != prefab_overrides.end()) {
                for (const PrefabOverride& override_record : overrides->second) {
                    apply_prefab_override(entity.sprite, override_record, absolute_path);
                }
                validate_sprite(entity.sprite, absolute_path, record.line, texture_ids);
            }
            instance_ids.insert(entity.id);
        } else {
            require_field_count(parsed_fields, 17, absolute_path, record.line, "entity");
            entity.id = parsed_fields[0];
            entity.uuid = {number<std::uint64_t>(
                parsed_fields[1], absolute_path, record.line, "entity UUID")};
            entity.name = parsed_fields[2];
            entity.physics_binding = parsed_fields[3] == "-" ? "" : std::string{parsed_fields[3]};
            entity.position = {
                number<float>(parsed_fields[4], absolute_path, record.line, "entity X"),
                number<float>(parsed_fields[5], absolute_path, record.line, "entity Y"),
                number<float>(parsed_fields[6], absolute_path, record.line, "entity Z"),
            };
            entity.sprite =
                sprite_definition(parsed_fields, 7, absolute_path, record.line, texture_ids);
        }
        if (!valid_id(entity.id) || !entity.uuid || entity.name.empty() ||
            entity_indices.contains(entity.id) ||
            !identity_uuids.insert(entity.uuid.value).second) {
            fail(absolute_path, record.line,
                 "Entity ids and UUIDs must be non-zero and unique and names cannot be empty.");
        }
        if (!entity.physics_binding.empty()) {
            const auto body = body_indices.find(entity.physics_binding);
            if (body == body_indices.end()) {
                fail(absolute_path, record.line, "Entity references an unknown physics body: " +
                                                     entity.physics_binding);
            }
            const Vec2 body_center = scene.physics_bodies_[body->second].box.center;
            if (std::abs(entity.position.x - body_center.x) > 0.001F ||
                std::abs(entity.position.z - body_center.y) > 0.001F) {
                fail(absolute_path, record.line,
                     "Physics-bound entities must start at their body's X/Z center.");
            }
            referenced_bodies.insert(entity.physics_binding);
        }
        entity_indices.emplace(entity.id, scene.entities_.size());
        scene.entities_.push_back(std::move(entity));
    }
    for (const auto& [instance_id, overrides] : prefab_overrides) {
        if (!instance_ids.contains(instance_id)) {
            fail(absolute_path, overrides.front().line,
                 "Prefab override references an unknown prefab instance: " + instance_id);
        }
    }
    for (const ScenePhysicsBodyDefinition& body : scene.physics_bodies_) {
        if ((body.role == ScenePhysicsRole::player || body.role == ScenePhysicsRole::primary_prop) &&
            !referenced_bodies.contains(body.id)) {
            fail(absolute_path, 0, "Player and primary_prop bodies require at least one bound entity.");
        }
    }

    std::unordered_map<std::string, std::size_t> animation_clip_indices;
    std::unordered_map<std::string, std::size_t> animation_clip_lines;
    for (ImportedAsepriteDefinition& imported : imported_aseprite) {
        for (AnimationClip& clip : imported.clips) {
            if (!valid_id(clip.id) || animation_clip_indices.contains(clip.id)) {
                fail(absolute_path, imported.line,
                     "Imported animation clip ids must be valid and unique.");
            }
            animation_clip_indices.emplace(clip.id, scene.animation_clips_.size());
            animation_clip_lines.emplace(clip.id, imported.line);
            scene.animation_clips_.push_back({
                .clip = std::move(clip),
                .texture_id = imported.texture_id,
            });
        }
    }
    for (const AuthoredValue& record : animation_clip_records) {
        const auto parsed_fields = fields(record.value);
        require_field_count(parsed_fields, 3, absolute_path, record.line, "animation_clip");
        SceneAnimationClipDefinition clip_definition{
            .clip = {
                .id = std::string{parsed_fields[0]},
                .loop_mode = animation_loop_mode(parsed_fields[2], absolute_path, record.line),
            },
            .texture_id = std::string{parsed_fields[1]},
        };
        if (!valid_id(clip_definition.clip.id) ||
            animation_clip_indices.contains(clip_definition.clip.id)) {
            fail(absolute_path, record.line, "Animation clip ids must be valid and unique.");
        }
        if (!texture_ids.contains(clip_definition.texture_id)) {
            fail(absolute_path, record.line,
                 "Animation clip references an unknown texture: " + clip_definition.texture_id);
        }
        animation_clip_indices.emplace(
            clip_definition.clip.id, scene.animation_clips_.size());
        animation_clip_lines.emplace(clip_definition.clip.id, record.line);
        scene.animation_clips_.push_back(std::move(clip_definition));
    }

    for (const AuthoredValue& record : animation_frame_records) {
        const auto parsed_fields = fields(record.value);
        require_field_count(parsed_fields, 7, absolute_path, record.line, "animation_frame");
        const auto clip = animation_clip_indices.find(std::string{parsed_fields[0]});
        if (clip == animation_clip_indices.end()) {
            fail(absolute_path, record.line,
                 "Animation frame references an unknown clip: " + std::string{parsed_fields[0]});
        }
        scene.animation_clips_[clip->second].clip.frames.push_back({
            .source = {
                number<float>(parsed_fields[1], absolute_path, record.line, "frame X"),
                number<float>(parsed_fields[2], absolute_path, record.line, "frame Y"),
                number<float>(parsed_fields[3], absolute_path, record.line, "frame width"),
                number<float>(parsed_fields[4], absolute_path, record.line, "frame height"),
            },
            .duration_ticks = number<std::uint32_t>(
                parsed_fields[5], absolute_path, record.line, "frame duration ticks"),
            .events = animation_events(parsed_fields[6], absolute_path, record.line),
        });
    }

    for (const SceneAnimationClipDefinition& clip : scene.animation_clips_) {
        try {
            static_cast<void>(AnimationPlayer{{clip.clip}, clip.clip.id});
        } catch (const std::exception& error) {
            fail(absolute_path, animation_clip_lines.at(clip.clip.id),
                 "Animation clip validation failed: " + std::string{error.what()});
        }
    }

    std::unordered_map<std::string, std::size_t> animation_binding_indices;
    std::unordered_set<std::string> animation_initial_entities;
    for (const AuthoredValue& record : animation_binding_records) {
        const auto parsed_fields = fields(record.value);
        require_field_count(parsed_fields, 4, absolute_path, record.line, "animation_binding");
        const std::string entity_id{parsed_fields[0]};
        const auto entity = entity_indices.find(entity_id);
        if (entity == entity_indices.end()) {
            fail(absolute_path, record.line,
                 "Animation binding references an unknown entity: " + entity_id);
        }
        if (scene.entities_[entity->second].physics_binding.empty()) {
            fail(absolute_path, record.line,
                 "Locomotion animation bindings require a physics-bound entity.");
        }
        const std::string clip_id{parsed_fields[2]};
        if (!animation_clip_indices.contains(clip_id)) {
            fail(absolute_path, record.line,
                 "Animation binding references an unknown clip: " + clip_id);
        }
        const LocomotionState state =
            locomotion_state(parsed_fields[1], absolute_path, record.line);
        const bool initial = boolean(parsed_fields[3], absolute_path, record.line, "initial state");
        auto binding = animation_binding_indices.find(entity_id);
        if (binding == animation_binding_indices.end()) {
            binding = animation_binding_indices.emplace(
                entity_id, scene.animation_bindings_.size()).first;
            scene.animation_bindings_.push_back({.entity_id = entity_id});
        }
        SceneAnimationBindingDefinition& definition = scene.animation_bindings_[binding->second];
        std::string& state_clip = definition.state_clips[static_cast<std::size_t>(state)];
        if (!state_clip.empty()) {
            fail(absolute_path, record.line,
                 "Animation entity states must be unique within one binding.");
        }
        state_clip = clip_id;
        if (initial) {
            if (!animation_initial_entities.insert(entity_id).second) {
                fail(absolute_path, record.line,
                     "Animation bindings require exactly one initial state per entity.");
            }
            definition.initial_state = state;
        }
    }
    for (const SceneAnimationBindingDefinition& binding : scene.animation_bindings_) {
        if (!animation_initial_entities.contains(binding.entity_id)) {
            fail(absolute_path, 0,
                 "Animation binding has no initial state: " + binding.entity_id);
        }
        if (std::ranges::any_of(binding.state_clips, [](const std::string& clip) {
                return clip.empty();
            })) {
            fail(absolute_path, 0,
                 "Locomotion animation bindings require all sixteen idle/move states: " +
                     binding.entity_id);
        }
    }

    if (scene.simulation_.world_boundary_thickness <= 0.0F ||
        scene.simulation_.player_speed <= 0.0F) {
        fail(absolute_path, 0, "Boundary thickness and player speed must be positive.");
    }
    try {
        const GroundMap validated_ground{scene.ground_};
        PhysicsWorld validated_physics{scene.simulation_.physics};
        for (const ScenePhysicsBodyDefinition& body : scene.physics_bodies_) {
            static_cast<void>(validated_physics.create_box(body.box));
        }
        const auto player = std::ranges::find(
            scene.physics_bodies_, ScenePhysicsRole::player, &ScenePhysicsBodyDefinition::role);
        scene.camera_.focus = {
            player->box.center.x,
            validated_ground.elevation_at(player->box.center),
            player->box.center.y,
        };
        if (!valid(scene.camera_)) {
            fail(absolute_path, camera.line, "Camera values are invalid.");
        }
    } catch (const std::exception& error) {
        fail(absolute_path, 0, std::string{"Runtime validation failed: "} + error.what());
    }

    return scene;
}

std::uint32_t SceneDefinition::schema_version() const noexcept { return schema_version_; }
const std::string& SceneDefinition::id() const noexcept { return id_; }
const std::filesystem::path& SceneDefinition::source_path() const noexcept { return source_path_; }

std::filesystem::path SceneDefinition::resolve_asset(
    const std::filesystem::path& relative_path
) const {
    if (!safe_relative_path(relative_path)) {
        throw std::invalid_argument{"Scene asset paths must be safe and relative."};
    }
    return (source_path_.parent_path() / relative_path).lexically_normal();
}

const GroundMapDefinition& SceneDefinition::ground() const noexcept { return ground_; }
const SceneSimulationDefinition& SceneDefinition::simulation() const noexcept { return simulation_; }
const Camera25DState& SceneDefinition::camera() const noexcept { return camera_; }
const std::vector<SceneTextureDefinition>& SceneDefinition::textures() const noexcept { return textures_; }
const std::vector<ScenePhysicsBodyDefinition>& SceneDefinition::physics_bodies() const noexcept {
    return physics_bodies_;
}
const std::vector<ScenePrefabDefinition>& SceneDefinition::prefabs() const noexcept { return prefabs_; }
const std::vector<SceneEntityDefinition>& SceneDefinition::entities() const noexcept { return entities_; }
const std::vector<SceneAnimationClipDefinition>& SceneDefinition::animation_clips() const noexcept {
    return animation_clips_;
}
const std::vector<SceneAnimationBindingDefinition>& SceneDefinition::animation_bindings() const noexcept {
    return animation_bindings_;
}

} // namespace ic2d
