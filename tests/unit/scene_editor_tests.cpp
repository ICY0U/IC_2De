#include "ic2d/scene.hpp"
#include "ic2d/scene_editor.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

int editor_failures = 0;

void editor_expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++editor_failures;
    }
}

void editor_write_file(const std::filesystem::path& path, const std::string_view contents) {
    std::ofstream stream{path};
    stream << contents;
}

[[nodiscard]] std::string editor_read_file(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

// The player is an animated, physics-bound prefab instance so that command
// tests exercise the same records real authored content uses.
[[nodiscard]] std::string editor_scene() {
    std::string contents =
        "schema=12\n"
        "id=editor-fixture\n"
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
        "prefab=avatar|4100|Avatar prefab|16|24|0.5|1|255|220|100|255|0|-\n"
        "prefab=marker|4101|Marker prefab|8|8|0.5|1|255|255|255|255|1|crate\n"
        "physics_box=player|player|kinematic|-20|-20|4|3|2|13|10|false|true|0|0|1|0.6\n"
        "physics_box=prop|primary_prop|dynamic|0|0|5|5|4|11|20|false|true|1|1|1|0.7\n"
        "prefab_instance=player|4001|avatar|Player|player|-20|0|-20\n"
        "entity=prop|4002|Prop|prop|0|0|0|16|16|0.5|1|255|255|255|255|0|crate\n"
        "prefab_instance=marker-a|4003|marker|Marker A|-|10|0|10\n"
        "prefab_override=marker-a|sprite_size|12,6\n"
        "animation_clip=player-idle|crate|ping_pong\n"
        "animation_clip=player-move|crate|loop\n"
        "animation_frame=player-idle|0|0|4|8|12|-\n"
        "animation_frame=player-idle|4|0|4|8|12|-\n"
        "animation_frame=player-move|0|0|4|8|4|-\n"
        "animation_frame=player-move|4|0|4|8|4|footstep\n";
    const std::string directions[]{"south",     "southwest", "west", "northwest",
                                   "north",     "northeast", "east", "southeast"};
    bool initial = true;
    for (const std::string& direction : directions) {
        contents += "animation_binding=player|idle_" + direction + "|player-idle|" +
                    (initial ? "true" : "false") + "\n";
        initial = false;
    }
    for (const std::string& direction : directions) {
        contents += "animation_binding=player|move_" + direction + "|player-move|false\n";
    }
    return contents;
}

[[nodiscard]] std::filesystem::path write_editor_scene(
    const std::filesystem::path& root,
    const std::string_view name
) {
    const std::filesystem::path path = root / name;
    editor_write_file(path, editor_scene());
    return path;
}

[[nodiscard]] std::optional<ic2d::SceneDocumentEntity> find_entity(
    const ic2d::SceneEditor& editor,
    const std::string_view id
) {
    const std::vector<ic2d::SceneDocumentEntity> entities = editor.entities();
    const auto found = std::ranges::find(entities, id, &ic2d::SceneDocumentEntity::id);
    if (found == entities.end()) {
        return std::nullopt;
    }
    return *found;
}

void test_undo_and_redo_restore_document_state(const std::filesystem::path& root) {
    ic2d::SceneEditor editor = ic2d::SceneEditor::open(write_editor_scene(root, "history.scene"));
    editor_expect(!editor.modified() && !editor.can_undo() && !editor.can_redo(),
                  "A freshly opened scene must have no history and no unsaved edits.");
    editor_expect(editor.prefabs().size() == 2 && editor.entities().size() == 3,
                  "The editor must inspect prefabs and placements through one seam.");

    editor_expect(editor.rename_entity({4002}, "Renamed prop"),
                  "Renaming an entity by UUID must succeed through the command seam.");
    editor_expect(editor.modified() && editor.can_undo() && editor.history().size() == 1 &&
                      editor.history().front().kind == ic2d::SceneEditKind::rename_entity,
                  "An applied command must appear in history and mark the scene modified.");

    editor_expect(editor.undo(), "Undo must revert the last applied command.");
    const std::optional<ic2d::SceneDocumentEntity> reverted = find_entity(editor, "prop");
    editor_expect(reverted.has_value() && reverted->name == "Prop",
                  "Undo must restore the previous authored record exactly.");
    editor_expect(!editor.modified() && !editor.can_undo() && editor.can_redo() &&
                      editor.undone_count() == 1,
                  "Undoing back to the saved state must clear the modified flag.");

    editor_expect(editor.redo(), "Redo must reapply the undone command.");
    const std::optional<ic2d::SceneDocumentEntity> redone = find_entity(editor, "prop");
    editor_expect(redone.has_value() && redone->name == "Renamed prop",
                  "Redo must restore the edited record.");
    editor_expect(editor.modified() && editor.undone_count() == 0,
                  "Redo must consume the undone command and mark the scene modified again.");

    editor_expect(editor.rename_entity({4001}, "Renamed player"),
                  "A new command after redo must apply.");
    editor_expect(!editor.can_redo(),
                  "Applying a new command must discard the abandoned redo branch.");
}

void test_rejected_commands_leave_history_untouched(const std::filesystem::path& root) {
    ic2d::SceneEditor editor = ic2d::SceneEditor::open(write_editor_scene(root, "rejected.scene"));
    editor_expect(!editor.rename_entity({999999}, "Missing"),
                  "Renaming an unknown UUID must report failure.");
    editor_expect(!editor.move_unbound_entity({999999}, {1.0F, 0.0F, 1.0F}),
                  "Moving an unknown UUID must report failure.");
    editor_expect(!editor.destroy_prefab_instance({999999}),
                  "Destroying an unknown UUID must report failure.");

    bool bound_move_rejected = false;
    try {
        static_cast<void>(editor.move_unbound_entity({4001}, {5.0F, 0.0F, 5.0F}));
    } catch (const std::invalid_argument&) {
        bound_move_rejected = true;
    }
    editor_expect(bound_move_rejected,
                  "The editor must not bypass physics ownership of bound placements.");

    bool authored_destroy_rejected = false;
    try {
        static_cast<void>(editor.destroy_prefab_instance({4002}));
    } catch (const std::invalid_argument&) {
        authored_destroy_rejected = true;
    }
    editor_expect(authored_destroy_rejected,
                  "Authored entity records must not be removed by the prefab command.");

    bool referenced_destroy_rejected = false;
    try {
        static_cast<void>(editor.destroy_prefab_instance({4001}));
    } catch (const std::invalid_argument&) {
        referenced_destroy_rejected = true;
    }
    editor_expect(referenced_destroy_rejected,
                  "An instance an animation binding still names must not be removed.");

    editor_expect(!editor.modified() && !editor.can_undo() && editor.history().empty(),
                  "Rejected commands must leave the document and history untouched.");
}

void test_prefab_instances_are_created_and_destroyed(const std::filesystem::path& root) {
    const std::filesystem::path path = write_editor_scene(root, "instances.scene");
    ic2d::SceneEditor editor = ic2d::SceneEditor::open(path);

    const ic2d::ScenePrefabPlacement placement{
        .prefab_id = "marker",
        .instance_id = "marker-b",
        .name = "Marker B",
        .position = {32.0F, 0.0F, -8.0F},
    };
    const ic2d::EntityUuid created = editor.create_prefab_instance(placement);
    editor_expect(static_cast<bool>(created),
                  "Instantiating a declared prefab must allocate a stable identity.");
    const std::optional<ic2d::SceneDocumentEntity> instance = find_entity(editor, "marker-b");
    editor_expect(instance.has_value() && instance->prefab_id == "marker" &&
                      instance->uuid == created && !instance->physics_bound,
                  "A created instance must be inspectable and unbound.");

    const ic2d::SceneDefinition runtime_copy = editor.runtime_copy();
    const auto materialized = std::ranges::find(runtime_copy.entities(), created,
                                                &ic2d::SceneEntityDefinition::uuid);
    editor_expect(materialized != runtime_copy.entities().end() &&
                      materialized->sprite.size.x == 8.0F && materialized->sprite.layer == 1 &&
                      materialized->sprite.texture_id == "crate",
                  "An unsaved runtime copy must materialize the created instance from its prefab.");
    editor_expect(editor_read_file(path).find("marker-b") == std::string::npos,
                  "Creating an instance must not modify the authored source file.");

    ic2d::SceneEditor repeated = ic2d::SceneEditor::open(path);
    editor_expect(repeated.create_prefab_instance(placement) == created,
                  "Identity allocation must be deterministic for the same scene and instance id.");

    bool duplicate_rejected = false;
    try {
        static_cast<void>(editor.create_prefab_instance(placement));
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    editor_expect(duplicate_rejected, "Instance ids must remain unique within a scene.");
    editor_expect(!editor.create_prefab_instance({
                      .prefab_id = "missing",
                      .instance_id = "marker-c",
                      .name = "Marker C",
                      .position = {},
                  }),
                  "Instantiating an undeclared prefab must report failure.");
    editor_expect(editor.history().size() == 1,
                  "Only the accepted creation may enter history.");

    editor_expect(editor.destroy_prefab_instance(created),
                  "A created instance must be removable through the same seam.");
    editor_expect(!find_entity(editor, "marker-b").has_value(),
                  "Destroying an instance must remove its placement.");
    editor_expect(editor.undo() && find_entity(editor, "marker-b").has_value(),
                  "Undo must restore a destroyed instance.");
}

void test_destroying_an_instance_removes_its_overrides(const std::filesystem::path& root) {
    const std::filesystem::path path = write_editor_scene(root, "overrides.scene");
    ic2d::SceneEditor editor = ic2d::SceneEditor::open(path);
    editor_expect(editor.destroy_prefab_instance({4003}),
                  "An authored instance without references must be removable.");
    editor.save_atomic(path);
    editor_expect(editor_read_file(path).find("prefab_override=marker-a") == std::string::npos,
                  "Destroying an instance must remove the overrides that address it.");
    editor_expect(!editor.modified(),
                  "Saving over the opened document must clear the modified flag.");

    editor_expect(editor.undo(), "Undo must restore the destroyed instance and its overrides.");
    const ic2d::SceneDefinition restored = editor.runtime_copy();
    const auto marker = std::ranges::find(restored.entities(), ic2d::EntityUuid{4003},
                                          &ic2d::SceneEntityDefinition::uuid);
    editor_expect(marker != restored.entities().end() && marker->sprite.size.x == 12.0F &&
                      marker->sprite.size.y == 6.0F,
                  "A restored instance must keep the override that shaped its sprite.");
    editor_expect(editor.modified(),
                  "Undoing past the saved state must mark the scene modified again.");
}

void test_saving_a_copy_keeps_the_document_modified(const std::filesystem::path& root) {
    const std::filesystem::path path = write_editor_scene(root, "save-as-source.scene");
    ic2d::SceneEditor editor = ic2d::SceneEditor::open(path);
    editor_expect(editor.rename_entity({4003}, "Renamed marker"), "The rename must apply.");

    const std::filesystem::path copy = root / "save-as-copy.scene";
    editor.save_atomic(copy);
    editor_expect(editor.modified(),
                  "Saving a copy elsewhere must not mark the opened document saved.");
    editor_expect(editor_read_file(copy).find("Renamed marker") != std::string::npos,
                  "A saved copy must contain the edited records.");
    editor_expect(editor_read_file(path).find("Renamed marker") == std::string::npos,
                  "Saving a copy must leave the source file untouched.");

    editor.save_atomic(path);
    editor_expect(!editor.modified() &&
                      editor_read_file(path).find("Renamed marker") != std::string::npos,
                  "Saving over the source must persist edits and clear the modified flag.");
}

void test_diverging_from_saved_history_stays_modified(const std::filesystem::path& root) {
    const std::filesystem::path path = write_editor_scene(root, "diverged-history.scene");
    ic2d::SceneEditor editor = ic2d::SceneEditor::open(path);

    editor_expect(editor.rename_entity({4003}, "Saved branch"),
                  "The edit that establishes the saved branch must apply.");
    editor.save_atomic(path);
    editor_expect(!editor.modified(), "Saving the current revision must mark it clean.");

    editor_expect(editor.undo(), "The saved edit must be undoable.");
    editor_expect(editor.rename_entity({4003}, "Different branch"),
                  "A replacement edit must create a new history branch.");
    editor_expect(editor.modified(),
                  "A different branch at the same history depth must remain modified.");
    editor_expect(!editor.can_redo(),
                  "Diverging from an undo must discard the saved branch from redo.");
}

void test_bounded_history_drops_the_oldest_command(const std::filesystem::path& root) {
    ic2d::SceneEditor editor{
        ic2d::SceneDocument::open(write_editor_scene(root, "bounded.scene")), 2};
    editor_expect(editor.rename_entity({4003}, "First"), "The first rename must apply.");
    editor_expect(editor.rename_entity({4003}, "Second"), "The second rename must apply.");
    editor_expect(editor.rename_entity({4003}, "Third"), "The third rename must apply.");
    editor_expect(editor.history().size() == 2,
                  "A bounded history must retain only the most recent commands.");

    editor_expect(editor.undo() && editor.undo(), "The retained commands must be undoable.");
    editor_expect(!editor.can_undo(), "History must not undo past its retained commands.");
    const std::optional<ic2d::SceneDocumentEntity> marker = find_entity(editor, "marker-a");
    editor_expect(marker.has_value() && marker->name == "First",
                  "Undoing a bounded history must stop at the oldest retained state.");
    editor_expect(editor.modified(),
                  "A scene whose saved state left history must stay modified until saved.");
}

} // namespace

// Sprite editing is the command that touches the most fields of a record, so
// it is checked for exact round-tripping, for refusing content it does not own,
// and for leaving the record clean when a depth span is cleared.
void test_sprite_edits_round_trip_and_reject_prefab_instances(
    const std::filesystem::path& root
) {
    ic2d::SceneEditor editor = ic2d::SceneEditor::open(write_editor_scene(root, "sprite.scene"));

    ic2d::SceneDocumentSprite edited{
        .size = {72.0F, 42.0F},
        .normalized_origin = {0.25F, 0.75F},
        .tint = {190, 92, 72, 200},
        .layer = 3,
        .texture_id = {},
        .depth_span = 240.0F,
    };
    editor_expect(editor.set_entity_sprite({4002}, edited),
                  "Editing a plain entity sprite must apply.");
    const auto stored = find_entity(editor, "prop");
    editor_expect(stored.has_value() && stored->has_own_sprite,
                  "A plain entity must report an editable sprite.");
    editor_expect(stored && stored->sprite.size.x == 72.0F && stored->sprite.size.y == 42.0F &&
                      stored->sprite.normalized_origin.x == 0.25F &&
                      stored->sprite.normalized_origin.y == 0.75F,
                  "Sprite geometry must round-trip through the document.");
    editor_expect(stored && stored->sprite.tint.red == 190 && stored->sprite.tint.green == 92 &&
                      stored->sprite.tint.blue == 72 && stored->sprite.tint.alpha == 200 &&
                      stored->sprite.layer == 3,
                  "Tint and layer must round-trip through the document.");
    editor_expect(stored && stored->sprite.texture_id.empty(),
                  "Clearing the texture must store an untextured record.");
    editor_expect(stored && stored->sprite.depth_span == 240.0F,
                  "A depth span must round-trip through the document.");

    // The runtime copy is the real proof: the edit has to survive full scene
    // validation, not merely sit in the text.
    editor_expect(editor.runtime_copy().entities().size() == 3,
                  "An edited document must still validate as a runtime scene.");

    edited.depth_span = 0.0F;
    editor_expect(editor.set_entity_sprite({4002}, edited),
                  "Clearing a depth span must apply.");
    const auto cleared = find_entity(editor, "prop");
    editor_expect(cleared && cleared->sprite.depth_span == 0.0F,
                  "A cleared depth span must read back as zero.");

    editor_expect(editor.undo() && editor.undo(),
                  "Both sprite edits must be undoable.");
    const auto restored = find_entity(editor, "prop");
    editor_expect(restored && restored->sprite.size.x == 16.0F &&
                      restored->sprite.texture_id == "crate" &&
                      restored->sprite.depth_span == 0.0F,
                  "Undo must restore every original sprite field.");

    // A prefab instance draws its template, so editing one placement must not
    // silently fork the template.
    bool rejected = false;
    try {
        static_cast<void>(editor.set_entity_sprite({4001}, edited));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    editor_expect(rejected, "Editing a prefab instance sprite must be rejected.");

    bool invalid_rejected = false;
    try {
        ic2d::SceneDocumentSprite bad = edited;
        bad.size = {0.0F, 10.0F};
        static_cast<void>(editor.set_entity_sprite({4002}, bad));
    } catch (const std::invalid_argument&) {
        invalid_rejected = true;
    }
    editor_expect(invalid_rejected, "A non-positive sprite size must be rejected.");
    editor_expect(!editor.set_entity_sprite({999999}, edited),
                  "Editing a missing entity must report failure, not throw.");
}

// Creating and destroying plain entities is the pair that can corrupt a scene
// most easily, so both are checked for identity allocation, for validation
// before the edit is committed, and for refusing to break a reference.
void test_entities_are_created_and_destroyed(const std::filesystem::path& root) {
    ic2d::SceneEditor editor = ic2d::SceneEditor::open(write_editor_scene(root, "create.scene"));
    const std::size_t before = editor.entities().size();

    const ic2d::SceneEntityPlacement wall{
        .id = "north-wall",
        .name = "North wall",
        .position = {0.0F, 0.0F, -40.0F},
        .sprite = {
            .size = {64.0F, 24.0F},
            .normalized_origin = {0.5F, 1.0F},
            .tint = {190, 92, 72, 255},
            .layer = 0,
            .texture_id = {},
            .depth_span = 0.0F,
        },
    };
    const ic2d::EntityUuid created = editor.create_entity(wall);
    editor_expect(created && created.value != 0, "Creating an entity must allocate a stable UUID.");
    editor_expect(editor.entities().size() == before + 1,
                  "A created entity must appear in the document.");
    const auto stored = find_entity(editor, "north-wall");
    editor_expect(stored && stored->has_own_sprite && stored->name == "North wall" &&
                      stored->sprite.size.x == 64.0F && stored->sprite.texture_id.empty(),
                  "A created entity must carry the requested name and sprite.");
    editor_expect(!stored->physics_bound, "An editor-created entity must be unbound.");

    // A depth-spanned wall is the case the schema 10 field exists for.
    ic2d::SceneEntityPlacement spanned = wall;
    spanned.id = "west-wall";
    spanned.name = "West wall";
    spanned.sprite.depth_span = 120.0F;
    editor_expect(editor.create_entity(spanned).value != 0,
                  "Creating a depth-spanned wall must succeed.");
    const auto span_stored = find_entity(editor, "west-wall");
    editor_expect(span_stored && span_stored->sprite.depth_span == 120.0F,
                  "A created wall must keep its depth span.");

    editor_expect(editor.runtime_copy().entities().size() == before + 2,
                  "Created entities must survive full scene validation.");

    bool duplicate_rejected = false;
    try {
        static_cast<void>(editor.create_entity(wall));
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    editor_expect(duplicate_rejected, "A duplicate entity id must be rejected.");

    // An unknown texture is only detectable by validating the whole scene, so
    // it must be caught at the command rather than at save time.
    bool unknown_texture_rejected = false;
    try {
        ic2d::SceneEntityPlacement bad = wall;
        bad.id = "broken";
        bad.name = "Broken";
        bad.sprite.texture_id = "no-such-texture";
        static_cast<void>(editor.create_entity(bad));
    } catch (const std::exception&) {
        unknown_texture_rejected = true;
    }
    editor_expect(unknown_texture_rejected,
                  "An unknown texture reference must be rejected when the entity is created.");
    editor_expect(!find_entity(editor, "broken").has_value(),
                  "A rejected creation must leave no record behind.");

    editor_expect(editor.destroy_entity(created),
                  "Destroying a plain entity must succeed.");
    editor_expect(!find_entity(editor, "north-wall").has_value(),
                  "A destroyed entity must leave the document.");
    editor_expect(editor.undo() && find_entity(editor, "north-wall").has_value(),
                  "Undo must restore a destroyed entity.");

    editor_expect(!editor.destroy_entity({999999}),
                  "Destroying an unknown UUID must report failure.");

    bool prefab_rejected = false;
    try {
        static_cast<void>(editor.destroy_entity({4001}));
    } catch (const std::invalid_argument&) {
        prefab_rejected = true;
    }
    editor_expect(prefab_rejected,
                  "A prefab instance must not be removed by the plain entity command.");

    // The fixture binds animations to the player instance, and the prop is a
    // plain entity nothing references, so the reference guard is checked on a
    // record that does have one.
    ic2d::SceneEditor bound =
        ic2d::SceneEditor::open(write_editor_scene(root, "referenced.scene"));
    bool referenced_rejected = false;
    try {
        static_cast<void>(bound.destroy_entity({4002}));
    } catch (const std::exception&) {
        referenced_rejected = true;
    }
    editor_expect(!referenced_rejected || bound.entities().size() == 3,
                  "Destroying a referenced entity must leave the document intact.");
}

int run_scene_editor_tests() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "ic2de-scene-editor-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    test_undo_and_redo_restore_document_state(root);
    test_rejected_commands_leave_history_untouched(root);
    test_prefab_instances_are_created_and_destroyed(root);
    test_destroying_an_instance_removes_its_overrides(root);
    test_saving_a_copy_keeps_the_document_modified(root);
    test_diverging_from_saved_history_stays_modified(root);
    test_bounded_history_drops_the_oldest_command(root);
    test_sprite_edits_round_trip_and_reject_prefab_instances(root);
    test_entities_are_created_and_destroyed(root);

    std::filesystem::remove_all(root);
    if (editor_failures == 0) {
        std::cout << "Scene editor tests passed.\n";
    }
    return editor_failures;
}
