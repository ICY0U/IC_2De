#include "ic2d/scene.hpp"
#include "ic2d/scene_document.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void write_file(const std::filesystem::path& path, const std::string_view contents) {
    std::ofstream stream{path};
    stream << contents;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::string valid_scene(const std::string_view extra = {}) {
    return std::string{
               "schema=10\n"
               "id=fixture\n"
               "world_space=x_y_z\n"
               "ground_plane=x_z\n"
               "elevation_axis=y\n"
               "walkable_bounds=-100|-80|200|160\n"
               "max_step_height=16\n"
               "camera=-18|50|1|1\n"
               "physics=32|4|0|0|false|16\n"
               "ground_filter=1|6\n"
               "trigger_filter=8|6\n"
               "player_speed=130\n"
               "texture=crate|checker|8|8|2|10|20|30|255|40|50|60|255|pixel\n"
               "ground_area=solid|20|20|10|10|0|0\n"
               "ground_area=trigger|40|40|10|10|0|7\n"
               "physics_box=player|player|kinematic|-20|-20|4|3|2|13|10|false|true|0|0|1|0.6\n"
               "physics_box=prop|primary_prop|dynamic|0|0|5|5|4|11|20|false|true|1|1|1|0.7\n"
               "entity=player|3001|Player|player|-20|0|-20|16|24|0.5|1|255|220|100|255|0|-\n"
               "entity=prop|3002|Prop|prop|0|0|0|16|16|0.5|1|255|255|255|255|0|crate\n"
               "animation_clip=player-idle|crate|ping_pong\n"
               "animation_clip=player-move|crate|loop\n"
               "animation_frame=player-idle|0|0|4|8|12|-\n"
               "animation_frame=player-idle|4|0|4|8|12|-\n"
               "animation_frame=player-move|0|0|4|8|4|-\n"
               "animation_frame=player-move|4|0|4|8|4|footstep\n"
               "animation_binding=player|idle_south|player-idle|true\n"
               "animation_binding=player|idle_southwest|player-idle|false\n"
               "animation_binding=player|idle_northwest|player-idle|false\n"
               "animation_binding=player|idle_north|player-idle|false\n"
               "animation_binding=player|idle_northeast|player-idle|false\n"
               "animation_binding=player|idle_west|player-idle|false\n"
               "animation_binding=player|idle_east|player-idle|false\n"
               "animation_binding=player|idle_southeast|player-idle|false\n"
               "animation_binding=player|move_south|player-move|false\n"
               "animation_binding=player|move_southwest|player-move|false\n"
               "animation_binding=player|move_northwest|player-move|false\n"
               "animation_binding=player|move_north|player-move|false\n"
               "animation_binding=player|move_northeast|player-move|false\n"
               "animation_binding=player|move_west|player-move|false\n"
               "animation_binding=player|move_east|player-move|false\n"
               "animation_binding=player|move_southeast|player-move|false\n"} +
           std::string{extra};
}

void test_loads_complete_scene(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "valid.scene";
    write_file(path, valid_scene());
    const ic2d::SceneDefinition scene = ic2d::SceneDefinition::load(path);
    expect(scene.schema_version() == 10, "Scene schema version must be retained.");
    expect(scene.id() == "fixture", "Scene id must be retained.");
    expect(scene.ground().areas.size() == 2, "Ground records must be loaded.");
    expect(scene.physics_bodies().size() == 2, "Physics body records must be loaded.");
    expect(scene.entities().size() == 2, "Entity records must be loaded.");
    expect(scene.entities().size() == 2 && scene.entities()[0].uuid.value == 3001 &&
               scene.entities()[1].uuid.value == 3002,
           "Persistent entity UUIDs must survive authored-scene loading.");
    expect(scene.textures().size() == 1, "Texture records must be loaded.");
    expect(scene.animation_clips().size() == 2, "Animation clips must be loaded.");
    expect(scene.animation_bindings().size() == 1,
           "Locomotion state bindings must be grouped by entity.");
    expect(scene.auto_animations().empty(),
           "Scenes without automatic animation records must retain an empty binding set.");
    expect(scene.camera().focus.x == -20.0F && scene.camera().focus.z == -20.0F,
           "Initial camera focus must be derived from the authored player.");
}

void test_loads_automatic_animation_with_phase_offset(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "automatic-animation.scene";
    write_file(path, valid_scene("animation_auto=prop|player-idle|7\n"));

    const ic2d::SceneDefinition scene = ic2d::SceneDefinition::load(path);
    expect(scene.auto_animations().size() == 1 &&
               scene.auto_animations().front().entity_id == "prop" &&
               scene.auto_animations().front().clip_id == "player-idle" &&
               scene.auto_animations().front().initial_tick_offset == 7,
           "Automatic animation bindings must retain clip and deterministic phase offset.");
}

void test_loads_second_authored_fixture() {
    const std::filesystem::path path =
        std::filesystem::path{IC2DE_TEST_FIXTURE_DIRECTORY} / "scenes" /
        "compact_encounter.scene";
    const ic2d::SceneDefinition scene = ic2d::SceneDefinition::load(path);
    expect(scene.id() == "compact_encounter", "The second fixture id must be retained.");
    expect(scene.physics_bodies().size() == 3,
           "The second fixture must load player, prop, and enemy bodies.");
    bool found_enemy = false;
    for (const ic2d::ScenePhysicsBodyDefinition& body : scene.physics_bodies()) {
        found_enemy = found_enemy || body.role == ic2d::ScenePhysicsRole::enemy;
    }
    expect(found_enemy, "The reusable scene format must retain an authored enemy role.");
    expect(scene.entities().size() == 5,
           "The second fixture must retain its independent entity layout.");
    expect(scene.animation_clips().size() == 4 && scene.animation_bindings().size() == 2,
           "The second fixture must retain reusable player and enemy animation data.");
}

void test_loads_aseprite_runtime_content() {
    const std::filesystem::path path =
        std::filesystem::path{IC2DE_RUNTIME_ASSET_DIRECTORY} / "test_area.scene";
    const ic2d::SceneDefinition scene = ic2d::SceneDefinition::load(path);
    expect(scene.schema_version() == 10,
           "The runtime scene must use the authored attacker role and eight-way locomotion.");
    const bool found_attacker = std::ranges::any_of(
        scene.physics_bodies(), [](const ic2d::ScenePhysicsBodyDefinition& body) {
            return body.role == ic2d::ScenePhysicsRole::attacker &&
                   body.box.motion == ic2d::PhysicsMotionType::kinematic_body;
        });
    expect(found_attacker,
           "Schema 10 runtime content must retain one kinematic authored attacker.");
    expect(scene.prefabs().size() == 2,
           "The runtime scene must declare its reusable tree and shadow prefabs.");
    const ic2d::SceneTextureDefinition* player_texture = nullptr;
    for (const ic2d::SceneTextureDefinition& texture : scene.textures()) {
        if (texture.id == "player-v2-walk-south") {
            player_texture = &texture;
            break;
        }
    }
    expect(scene.textures().size() == 15 && player_texture != nullptr &&
               player_texture->kind == ic2d::SceneTextureKind::file &&
               player_texture->relative_path == "player-v2-walk-south.png",
           "The Aseprite records must provide the replacement player texture set.");
    expect(scene.animation_clips().size() == 41,
           "The runtime scene must combine V2 player, dodge, NPC, and ambient clips.");
    expect(scene.animation_clips().front().clip.id == "player-v2-idle-south" &&
               scene.animation_clips()[1].clip.id == "player-v2-move-south" &&
               scene.animation_clips()[1].clip.frames.size() == 8,
           "Imported tag order and inclusive ranges must reach scene-owned clips.");
    expect(scene.animation_clips()[10].clip.id == "player-v2-idle-southwest" &&
               scene.animation_clips()[11].clip.id == "player-v2-move-southwest" &&
               scene.animation_clips()[11].clip.frames.size() == 8,
           "The diagonal atlas must provide independent idle and move clips.");
    expect(scene.auto_animations().size() == 2 &&
               scene.auto_animations()[0].clip_id == "tree-sway" &&
               scene.auto_animations()[1].initial_tick_offset != 0,
           "Tree instances must use deterministic, phase-offset ambient animation.");
    expect(scene.animation_clips()[25].clip.id == "npc-patchwork-idle-south" &&
               scene.animation_clips()[26].clip.id == "npc-patchwork-move-south" &&
               scene.animation_clips()[26].clip.frames.size() == 8,
           "The second stitched character must remain available through the NPC bindings.");

    const ic2d::SceneAnimationClipDefinition* player_move_west = nullptr;
    const ic2d::SceneAnimationClipDefinition* player_idle_east = nullptr;
    const ic2d::SceneAnimationClipDefinition* player_dodge_west = nullptr;
    const ic2d::SceneAnimationClipDefinition* tree_sway = nullptr;
    for (const ic2d::SceneAnimationClipDefinition& clip : scene.animation_clips()) {
        if (clip.clip.id == "player-v2-move-west") {
            player_move_west = &clip;
        } else if (clip.clip.id == "player-v2-idle-east") {
            player_idle_east = &clip;
        } else if (clip.clip.id == "player-v2-dodge-west") {
            player_dodge_west = &clip;
        } else if (clip.clip.id == "tree-sway") {
            tree_sway = &clip;
        }
    }
    expect(player_move_west != nullptr &&
               player_move_west->clip.frames.front().source.x <
                   player_move_west->clip.frames.back().source.x &&
               std::ranges::all_of(player_move_west->clip.frames,
                                   [](const ic2d::AnimationFrame& frame) {
                                       return frame.flip_x;
                                   }),
           "The west gait must play forward while horizontally presenting the east-facing source art.");
    expect(player_idle_east != nullptr &&
               std::ranges::all_of(player_idle_east->clip.frames,
                                   [](const ic2d::AnimationFrame& frame) {
                                       return !frame.flip_x;
                                   }),
           "The V2 east idle must use the authored east-facing source art.");
    expect(player_move_west != nullptr &&
               std::ranges::all_of(player_move_west->clip.frames, [](const ic2d::AnimationFrame& frame) {
                   return frame.duration_ticks >= 5 && frame.duration_ticks <= 7;
               }),
           "Player V2 walk timing must preserve the authored 48-tick gait.");
    std::uint32_t dodge_ticks = 0;
    if (player_dodge_west != nullptr) {
        for (const ic2d::AnimationFrame& frame : player_dodge_west->clip.frames) {
            dodge_ticks += frame.duration_ticks;
        }
    }
    expect(player_dodge_west != nullptr && player_dodge_west->clip.frames.size() == 8 &&
               dodge_ticks == 12 &&
               std::ranges::all_of(player_dodge_west->clip.frames,
                                   [](const ic2d::AnimationFrame& frame) {
                                       return frame.flip_x;
                                   }),
           "The mirrored west dodge must show all eight poses across exactly twelve ticks.");
    expect(tree_sway != nullptr &&
               std::ranges::all_of(tree_sway->clip.frames, [](const ic2d::AnimationFrame& frame) {
                   return frame.duration_ticks >= 12;
               }),
           "Tree sway must remain slower than its original rapid loop.");

    const auto tree_prefab = std::ranges::find(scene.prefabs(), "tree", &ic2d::ScenePrefabDefinition::id);
    expect(tree_prefab != scene.prefabs().end() &&
               tree_prefab->sprite.normalized_origin.y <= 0.96F,
           "Tree sprites must sink their visible roots into the authored ground contact point.");

    const ic2d::SceneEntityDefinition* player_shadow = nullptr;
    for (const ic2d::SceneEntityDefinition& entity : scene.entities()) {
        if (entity.id == "player-shadow") {
            player_shadow = &entity;
            break;
        }
    }
    expect(player_shadow != nullptr && player_shadow->sprite.texture_id == "soft-shadow",
           "The player shadow must use the soft radial texture instead of an untextured quad.");
    const ic2d::SceneTextureDefinition* shadow_texture = nullptr;
    for (const ic2d::SceneTextureDefinition& texture : scene.textures()) {
        if (texture.id == "soft-shadow") {
            shadow_texture = &texture;
            break;
        }
    }
    expect(shadow_texture != nullptr && shadow_texture->kind == ic2d::SceneTextureKind::radial,
           "The player shadow texture must be a generated radial alpha gradient.");
}

void test_rejects_incomplete_locomotion_binding(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "incomplete-animation.scene";
    std::string contents = valid_scene();
    const std::string missing =
        "animation_binding=player|move_east|player-move|false\n";
    contents.erase(contents.find(missing), missing.size());
    write_file(path, contents);
    bool rejected = false;
    try {
        static_cast<void>(ic2d::SceneDefinition::load(path));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()}.find("all sixteen") !=
                   std::string_view::npos;
    }
    expect(rejected, "Locomotion bindings must provide all idle and move directions.");
}

void test_rejects_unknown_binding(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "unknown-binding.scene";
    std::string contents = valid_scene();
    const std::string expected = "entity=prop|3002|Prop|prop|";
    contents.replace(contents.find(expected), expected.size(),
                     "entity=prop|3002|Prop|missing|");
    write_file(path, contents);
    bool rejected = false;
    try {
        static_cast<void>(ic2d::SceneDefinition::load(path));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()}.find("unknown physics body") != std::string_view::npos;
    }
    expect(rejected, "Unknown physics bindings must fail with a field diagnostic.");
}

void test_rejects_duplicate_ids(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "duplicate.scene";
    write_file(path, valid_scene(
        "entity=player|3999|Duplicate|player|-20|0|-20|16|24|0.5|1|255|255|255|255|0|-\n"));
    bool rejected = false;
    try {
        static_cast<void>(ic2d::SceneDefinition::load(path));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()}.find("unique") != std::string_view::npos;
    }
    expect(rejected, "Duplicate authored ids must fail before runtime construction.");
}

void test_rejects_duplicate_entity_uuids(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "duplicate-uuid.scene";
    write_file(path, valid_scene(
        "entity=duplicate|3001|Duplicate UUID|player|-20|0|-20|16|24|0.5|1|255|255|255|255|0|-\n"));
    bool rejected = false;
    try {
        static_cast<void>(ic2d::SceneDefinition::load(path));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()}.find("UUIDs") != std::string_view::npos;
    }
    expect(rejected, "Duplicate persistent UUIDs must fail before runtime construction.");
}

void test_rejects_zero_entity_uuid(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "zero-uuid.scene";
    std::string contents = valid_scene();
    const std::string expected = "entity=player|3001|";
    contents.replace(contents.find(expected), expected.size(), "entity=player|0|");
    write_file(path, contents);
    bool rejected = false;
    try {
        static_cast<void>(ic2d::SceneDefinition::load(path));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()}.find("non-zero") != std::string_view::npos;
    }
    expect(rejected, "Zero persistent UUIDs must fail before runtime construction.");
}

void test_rejects_unsafe_asset_path(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "unsafe.scene";
    write_file(path, valid_scene("texture=escape|file|../escape.png|pixel\n"));
    bool rejected = false;
    try {
        static_cast<void>(ic2d::SceneDefinition::load(path));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()}.find("safe relative paths") != std::string_view::npos;
    }
    expect(rejected, "Scene assets must not escape the scene content directory.");
}

void test_rejects_unsupported_schema(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "old.scene";
    std::string contents = valid_scene();
    contents.replace(contents.find("schema=10"), 9, "schema=5");
    write_file(path, contents);
    bool rejected = false;
    try {
        static_cast<void>(ic2d::SceneDefinition::load(path));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()}.find("Unsupported schema") != std::string_view::npos;
    }
    expect(rejected, "Unsupported scene schemas must fail before runtime construction.");
}

void test_scene_document_round_trip_edits_by_uuid(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "editable.scene";
    write_file(path, std::string{"# editor-note\n"} + valid_scene(
        "entity=marker|3003|Marker|-|10|0|10|8|8|0.5|1|255|255|255|255|1|-\n"));

    ic2d::SceneDocument document = ic2d::SceneDocument::open(path);
    const auto before = document.entities();
    expect(document.schema_version() == 10 && before.size() == 3,
           "SceneDocument must expose validated schema and entity inspection data.");
    expect(document.rename_entity({3002}, "Renamed prop"),
           "SceneDocument must rename an entity through its stable UUID.");
    expect(document.set_unbound_entity_position({3003}, {12.5F, 2.0F, -4.25F}),
           "SceneDocument must move an unbound entity through its stable UUID.");
    expect(!document.rename_entity({999999}, "Missing"),
           "SceneDocument must report a missing UUID without mutating another entity.");

    bool bound_move_rejected = false;
    try {
        static_cast<void>(document.set_unbound_entity_position({3001}, {}));
    } catch (const std::invalid_argument&) {
        bound_move_rejected = true;
    }
    expect(bound_move_rejected,
           "SceneDocument must not bypass physics ownership when moving a bound entity.");

    const ic2d::SceneDefinition runtime_copy = document.runtime_copy();
    const auto runtime_renamed = std::ranges::find(
        runtime_copy.entities(), ic2d::EntityUuid{3002}, &ic2d::SceneEntityDefinition::uuid);
    expect(runtime_renamed != runtime_copy.entities().end() &&
               runtime_renamed->name == "Renamed prop",
           "An unsaved runtime copy must contain validated in-memory document edits.");
    expect(read_file(path).find("Renamed prop") == std::string::npos,
           "Creating a runtime copy must not overwrite the authored source file.");
    expect(!std::filesystem::exists(path.string() + ".ic2de.runtime-copy.tmp"),
           "Runtime-copy materialization must clean its temporary scene file.");

    document.save_atomic(path);
    const ic2d::SceneDefinition saved = ic2d::SceneDefinition::load(path);
    const auto renamed = std::ranges::find(saved.entities(), ic2d::EntityUuid{3002},
                                           &ic2d::SceneEntityDefinition::uuid);
    const auto marker = std::ranges::find(saved.entities(), ic2d::EntityUuid{3003},
                                          &ic2d::SceneEntityDefinition::uuid);
    expect(renamed != saved.entities().end() && renamed->name == "Renamed prop",
           "Atomic scene save must retain the UUID-addressed name edit.");
    expect(marker != saved.entities().end() && std::abs(marker->position.x - 12.5F) < 0.001F &&
               std::abs(marker->position.y - 2.0F) < 0.001F &&
               std::abs(marker->position.z + 4.25F) < 0.001F,
           "Atomic scene save must round-trip finite XYZ position edits.");
    expect(read_file(path).find("# editor-note") != std::string::npos,
           "SceneDocument must preserve comments and untouched authored records.");
}

void test_scene_document_failed_save_preserves_destination(const std::filesystem::path& root) {
    const std::filesystem::path source =
        std::filesystem::path{IC2DE_RUNTIME_ASSET_DIRECTORY} / "test_area.scene";
    const std::filesystem::path destination = root / "protected.scene";
    write_file(destination, "keep-existing-file\n");
    const std::string original_destination = read_file(destination);
    const ic2d::SceneDocument document = ic2d::SceneDocument::open(source);

    bool rejected = false;
    try {
        document.save_atomic(destination);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected,
           "Save-as must reject a destination whose relative asset dependencies are absent.");
    expect(read_file(destination) == original_destination,
           "Failed scene validation must leave the previous destination untouched.");
    expect(!std::filesystem::exists(destination.string() + ".ic2de.tmp"),
           "Failed scene saves must clean their candidate file.");
}

void test_scene_document_migrates_schema_five_deterministically(
    const std::filesystem::path& root
) {
    const std::filesystem::path path = root / "schema-five.scene";
    std::string legacy = valid_scene();
    legacy.replace(legacy.find("schema=10"), 9, "schema=5");
    const std::string player_current = "entity=player|3001|";
    legacy.replace(legacy.find(player_current), player_current.size(), "entity=player|");
    const std::string prop_current = "entity=prop|3002|";
    legacy.replace(legacy.find(prop_current), prop_current.size(), "entity=prop|");
    write_file(path, legacy);

    const ic2d::SceneDocument migrated = ic2d::SceneDocument::migrate_to_current(path);
    const auto entities = migrated.entities();
    expect(migrated.schema_version() == 10 && entities.size() == 2,
           "Explicit migration must produce a current, inspectable scene document.");
    expect(entities.size() == 2 && entities[0].uuid && entities[1].uuid &&
               entities[0].uuid != entities[1].uuid,
           "Schema migration must assign non-zero unique persistent UUIDs.");
    const std::string first_result = read_file(path);
    static_cast<void>(ic2d::SceneDocument::migrate_to_current(path));
    expect(read_file(path) == first_result,
           "Repeating migration on a current file must be deterministic and idempotent.");
    static_cast<void>(ic2d::SceneDefinition::load(path));
}

[[nodiscard]] std::string prefab_scene() {
    return valid_scene(
        "prefab=marker|3100|Marker prefab|8|8|0.5|1|255|255|255|255|1|crate\n"
        "prefab_instance=marker-b|3102|marker|Marker B|-|20|0|20\n"
        "prefab_override=marker-b|sprite_size|12,6\n"
        "prefab_override=marker-b|tint|10,20,30,255\n"
        "prefab_override=marker-b|texture|-\n");
}

void test_expands_prefab_instances_in_authored_order(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "prefabs.scene";
    std::string contents = prefab_scene();
    contents.insert(contents.find("entity=prop|3002|"),
                    "prefab_instance=marker-a|3101|marker|Marker A|-|10|0|10\n");
    write_file(path, contents);

    const ic2d::SceneDefinition scene = ic2d::SceneDefinition::load(path);
    expect(scene.prefabs().size() == 1 && scene.prefabs().front().id == "marker" &&
               scene.prefabs().front().uuid.value == 3100,
           "Prefab definitions must load with their own persistent identity.");
    expect(scene.entities().size() == 4,
           "Prefab instances must become entities alongside authored entities.");
    const std::vector<std::string> order{"player", "marker-a", "prop", "marker-b"};
    bool ordered = scene.entities().size() == order.size();
    for (std::size_t index = 0; ordered && index < order.size(); ++index) {
        ordered = scene.entities()[index].id == order[index];
    }
    expect(ordered, "Entities and prefab instances must retain authored source order.");

    const auto instance_a = std::ranges::find(scene.entities(), "marker-a",
                                              &ic2d::SceneEntityDefinition::id);
    const auto instance_b = std::ranges::find(scene.entities(), "marker-b",
                                              &ic2d::SceneEntityDefinition::id);
    expect(instance_a != scene.entities().end() && instance_a->prefab_id == "marker" &&
               instance_a->uuid.value == 3101 && instance_a->sprite.size.x == 8.0F &&
               instance_a->sprite.size.y == 8.0F && instance_a->sprite.layer == 1 &&
               instance_a->sprite.texture_id == "crate",
           "An instance without overrides must copy every prefab sprite field.");
    expect(instance_b != scene.entities().end() && instance_b->sprite.size.x == 12.0F &&
               instance_b->sprite.size.y == 6.0F && instance_b->sprite.tint.red == 10 &&
               instance_b->sprite.tint.alpha == 255 && instance_b->sprite.texture_id.empty() &&
               instance_b->sprite.layer == 1,
           "Overrides must replace only the fields they name.");
}

void test_rejects_unknown_prefab_reference(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "unknown-prefab.scene";
    write_file(path, valid_scene(
        "prefab_instance=marker-a|3101|missing|Marker A|-|10|0|10\n"));
    bool rejected = false;
    try {
        static_cast<void>(ic2d::SceneDefinition::load(path));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()}.find("unknown prefab") != std::string_view::npos;
    }
    expect(rejected, "Prefab instances must reference a declared prefab.");
}

void test_rejects_prefab_identity_collision(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "prefab-collision.scene";
    write_file(path, valid_scene(
        "prefab=marker|3001|Marker prefab|8|8|0.5|1|255|255|255|255|1|crate\n"));
    bool rejected = false;
    try {
        static_cast<void>(ic2d::SceneDefinition::load(path));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()}.find("unique") != std::string_view::npos;
    }
    expect(rejected, "Prefabs and entities must share one scene-unique identity space.");
}

void test_rejects_unknown_prefab_override_target(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "orphan-override.scene";
    write_file(path, valid_scene("prefab_override=missing-instance|layer|3\n"));
    bool rejected = false;
    try {
        static_cast<void>(ic2d::SceneDefinition::load(path));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()}.find("unknown prefab instance") !=
                   std::string_view::npos;
    }
    expect(rejected, "Overrides must name a prefab instance that exists.");
}

void test_rejects_repeated_prefab_override_field(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "repeated-override.scene";
    write_file(path, prefab_scene() + "prefab_override=marker-b|layer|3\n" +
                         "prefab_override=marker-b|layer|4\n");
    bool rejected = false;
    try {
        static_cast<void>(ic2d::SceneDefinition::load(path));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()}.find("at most once") != std::string_view::npos;
    }
    expect(rejected, "One instance must not override the same field twice.");
}

void test_scene_document_migrates_schema_six_to_ten(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "schema-six.scene";
    std::string legacy = valid_scene();
    legacy.replace(legacy.find("schema=10"), 9, "schema=6");
    write_file(path, legacy);

    const ic2d::SceneDocument migrated = ic2d::SceneDocument::migrate_to_current(path);
    expect(migrated.schema_version() == 10 && migrated.entities().size() == 2 &&
               migrated.prefabs().empty(),
           "Schema 6 content must migrate to schema 10 without inventing prefab records.");
    expect(migrated.entities().front().uuid.value == 3001,
           "Schema 6 migration must retain persistent identities that already exist.");
    const std::string first_result = read_file(path);
    static_cast<void>(ic2d::SceneDocument::migrate_to_current(path));
    expect(read_file(path) == first_result,
           "Repeating the schema 6 migration must be idempotent.");
}

void test_rejects_incomplete_dodge_binding(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "incomplete-dodge-animation.scene";
    std::string contents = valid_scene(
        "animation_binding=player|dodge_south|player-move|false\n");
    write_file(path, contents);
    bool rejected = false;
    try {
        static_cast<void>(ic2d::SceneDefinition::load(path));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()}.find("all eight compass") !=
                   std::string_view::npos;
    }
    expect(rejected, "A player that opts into dodge art must bind every direction.");
}

void test_scene_document_migrates_schema_seven_to_ten(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "schema-seven.scene";
    std::string legacy = valid_scene();
    legacy.replace(legacy.find("schema=10"), 9, "schema=7");
    write_file(path, legacy);

    const ic2d::SceneDocument migrated = ic2d::SceneDocument::migrate_to_current(path);
    const ic2d::SceneDefinition runtime_copy = migrated.runtime_copy();
    expect(migrated.schema_version() == 10 && migrated.entities().size() == 2 &&
               runtime_copy.auto_animations().empty(),
           "Schema 7 content must migrate to schema 10 without inventing animation records.");
    const std::string first_result = read_file(path);
    static_cast<void>(ic2d::SceneDocument::migrate_to_current(path));
    expect(read_file(path) == first_result,
           "Repeating the schema 7 migration must be idempotent.");
}

void test_scene_document_migrates_schema_eight_to_ten(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "schema-eight.scene";
    std::string legacy = valid_scene();
    legacy.replace(legacy.find("schema=10"), 9, "schema=8");
    write_file(path, legacy);

    const ic2d::SceneDocument migrated = ic2d::SceneDocument::migrate_to_current(path);
    expect(migrated.schema_version() == 10 && migrated.entities().size() == 2,
           "Schema 8 content must migrate to schema 10 without inventing attacker records.");
    const std::string first_result = read_file(path);
    static_cast<void>(ic2d::SceneDocument::migrate_to_current(path));
    expect(read_file(path) == first_result,
           "Repeating the schema 8 migration must be idempotent.");
}

void test_scene_document_migrates_schema_nine_to_ten(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "schema-nine.scene";
    std::string legacy = valid_scene();
    legacy.replace(legacy.find("schema=10"), 9, "schema=9");
    write_file(path, legacy);

    const ic2d::SceneDocument migrated = ic2d::SceneDocument::migrate_to_current(path);
    expect(migrated.schema_version() == 10 && migrated.entities().size() == 2,
           "Schema 9 content must migrate to schema 10 without inventing depth spans.");
    const std::string first_result = read_file(path);
    static_cast<void>(ic2d::SceneDocument::migrate_to_current(path));
    expect(read_file(path) == first_result,
           "Repeating the schema 9 migration must be idempotent.");
}

} // namespace

int run_scene_editor_tests();

int main() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "ic2de-scene-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    std::cerr << "[DEBUG-scene-hang] load fixtures\n";
    test_loads_complete_scene(root);
    test_loads_automatic_animation_with_phase_offset(root);
    test_loads_second_authored_fixture();
    std::cerr << "[DEBUG-scene-hang] load runtime content\n";
    test_loads_aseprite_runtime_content();
    std::cerr << "[DEBUG-scene-hang] rejection cases\n";
    test_rejects_incomplete_locomotion_binding(root);
    test_rejects_incomplete_dodge_binding(root);
    test_rejects_unknown_binding(root);
    test_rejects_duplicate_ids(root);
    test_rejects_duplicate_entity_uuids(root);
    test_rejects_zero_entity_uuid(root);
    test_rejects_unsafe_asset_path(root);
    test_rejects_unsupported_schema(root);
    std::cerr << "[DEBUG-scene-hang] document and migration cases\n";
    test_scene_document_round_trip_edits_by_uuid(root);
    test_scene_document_failed_save_preserves_destination(root);
    test_scene_document_migrates_schema_five_deterministically(root);
    test_scene_document_migrates_schema_six_to_ten(root);
    test_scene_document_migrates_schema_seven_to_ten(root);
    test_scene_document_migrates_schema_eight_to_ten(root);
    test_scene_document_migrates_schema_nine_to_ten(root);
    test_expands_prefab_instances_in_authored_order(root);
    test_rejects_unknown_prefab_reference(root);
    test_rejects_prefab_identity_collision(root);
    test_rejects_unknown_prefab_override_target(root);
    test_rejects_repeated_prefab_override_field(root);
    std::cerr << "[DEBUG-scene-hang] scene editor cases\n";
    failures += run_scene_editor_tests();
    std::cerr << "[DEBUG-scene-hang] complete\n";

    std::filesystem::remove_all(root);
    if (failures == 0) {
        std::cout << "Scene tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
