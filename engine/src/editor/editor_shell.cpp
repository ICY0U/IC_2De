#include "ic2d/editor.hpp"

#include "ic2d/core/log.hpp"
#include "editor/imgui_raylib_backend.hpp"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <imgui.h>
#include <imgui_internal.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace ic2d {
namespace {

constexpr std::size_t text_field_capacity = 96;
using TextField = std::array<char, text_field_capacity>;

void assign(TextField& field, const std::string& value) {
    const std::size_t length = std::min(value.size(), text_field_capacity - 1);
    std::memcpy(field.data(), value.data(), length);
    field[length] = '\0';
}

[[nodiscard]] std::string entity_label(const SceneDocumentEntity& entity) {
    std::string label = entity.name;
    if (!entity.prefab_id.empty()) {
        label += "  [" + entity.prefab_id + "]";
    }
    if (entity.physics_bound) {
        label += "  (body)";
    }
    return label;
}

void push_entity_id(const EntityUuid uuid) {
    ImGui::PushID(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(uuid.value)));
}

} // namespace

struct EditorShell::Impl {
    ImGuiRaylibBackend backend;
    bool visible{false};
    bool layout_built{false};

    EntityUuid selection{};
    EntityUuid buffered_selection{};
    TextField name_field{};
    std::array<float, 3> position_field{};
    bool name_field_active{false};
    bool position_field_active{false};

    TextField new_instance_id{};
    TextField new_instance_name{};
    std::array<float, 3> new_instance_position{};
    int selected_prefab{0};

    std::string status{"Editor ready."};

    void set_status(std::string message) { status = std::move(message); }

    void build_layout(ImGuiID dockspace_id) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

        ImGuiID centre = dockspace_id;
        const ImGuiID left =
            ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.20F, nullptr, &centre);
        const ImGuiID right =
            ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.28F, nullptr, &centre);
        const ImGuiID bottom =
            ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.30F, nullptr, &centre);

        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Statistics", bottom);
        ImGui::DockBuilderDockWindow("Debug channels", bottom);
        ImGui::DockBuilderDockWindow("History", bottom);
        ImGui::DockBuilderDockWindow("Viewport", centre);
        ImGui::DockBuilderFinish(dockspace_id);
    }

    void refresh_selection_fields(const std::vector<SceneDocumentEntity>& entities) {
        const auto found = std::ranges::find(entities, selection, &SceneDocumentEntity::uuid);
        if (found == entities.end()) {
            return;
        }
        if (buffered_selection != selection) {
            buffered_selection = selection;
            name_field_active = false;
            position_field_active = false;
        }
        if (!name_field_active) {
            assign(name_field, found->name);
        }
        if (!position_field_active) {
            position_field = {found->position.x, found->position.y, found->position.z};
        }
    }

    void draw_menu_bar(SceneEditor& editor, DebugVisuals& visuals, EditorActions& actions) {
        if (!ImGui::BeginMainMenuBar()) {
            return;
        }
        if (ImGui::BeginMenu("Scene")) {
            if (ImGui::MenuItem("Save", nullptr, false, editor.modified())) {
                save(editor);
            }
            if (ImGui::MenuItem("Apply to running scene")) {
                actions.apply_document_to_running_scene = true;
            }
            if (ImGui::MenuItem("Reset running scene")) {
                actions.reset_running_scene = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, editor.can_undo())) {
                static_cast<void>(editor.undo());
                set_status("Undid the last command.");
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, editor.can_redo())) {
                static_cast<void>(editor.redo());
                set_status("Redid the last undone command.");
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            bool debug_enabled = visuals.enabled();
            if (ImGui::MenuItem("Debug visuals", "F1", &debug_enabled)) {
                visuals.set_enabled(debug_enabled);
            }
            if (ImGui::MenuItem("Hide editor", "F2")) {
                visible = false;
            }
            ImGui::EndMenu();
        }
        ImGui::Text("   %s", status.c_str());
        ImGui::EndMainMenuBar();
    }

    void save(SceneEditor& editor) {
        try {
            editor.save_atomic(editor.document().source_path());
            set_status("Saved " + editor.document().source_path().filename().string() + ".");
        } catch (const std::exception& error) {
            set_status(std::string{"Save rejected: "} + error.what());
        }
    }

    void draw_hierarchy(const std::vector<SceneDocumentEntity>& entities) {
        if (!ImGui::Begin("Hierarchy")) {
            ImGui::End();
            return;
        }
        ImGui::TextDisabled("%zu placements", entities.size());
        ImGui::Separator();
        for (const SceneDocumentEntity& entity : entities) {
            push_entity_id(entity.uuid);
            if (ImGui::Selectable(entity_label(entity).c_str(), selection == entity.uuid)) {
                selection = entity.uuid;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("id %s\nuuid %llu", entity.id.c_str(),
                                  static_cast<unsigned long long>(entity.uuid.value));
            }
            ImGui::PopID();
        }
        ImGui::End();
    }

    void draw_inspector(
        SceneEditor& editor,
        const std::vector<SceneDocumentEntity>& entities
    ) {
        if (!ImGui::Begin("Inspector")) {
            ImGui::End();
            return;
        }
        const auto found = std::ranges::find(entities, selection, &SceneDocumentEntity::uuid);
        if (found == entities.end()) {
            ImGui::TextWrapped("Select a placement in the Hierarchy panel.");
            draw_prefab_creation(editor);
            ImGui::End();
            return;
        }

        ImGui::Text("id   %s", found->id.c_str());
        ImGui::Text("uuid %llu", static_cast<unsigned long long>(found->uuid.value));
        if (found->prefab_id.empty()) {
            ImGui::TextDisabled("authored entity");
        } else {
            ImGui::Text("prefab %s", found->prefab_id.c_str());
        }
        ImGui::Separator();

        ImGui::InputText("Name", name_field.data(), name_field.size());
        name_field_active = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            apply_rename(editor, found->uuid);
        }

        ImGui::BeginDisabled(found->physics_bound);
        ImGui::DragFloat3("Position", position_field.data(), 0.5F);
        position_field_active = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            apply_move(editor, found->uuid);
        }
        ImGui::EndDisabled();
        if (found->physics_bound) {
            ImGui::TextDisabled("X/Z owned by the bound physics body.");
        }

        if (!found->prefab_id.empty()) {
            ImGui::Separator();
            if (ImGui::Button("Destroy instance")) {
                destroy_instance(editor, found->uuid);
            }
        }

        draw_prefab_creation(editor);
        ImGui::End();
    }

    void apply_rename(SceneEditor& editor, const EntityUuid uuid) {
        try {
            if (editor.rename_entity(uuid, name_field.data())) {
                set_status("Renamed placement.");
            } else {
                set_status("Rename target no longer exists.");
            }
        } catch (const std::exception& error) {
            set_status(std::string{"Rename rejected: "} + error.what());
        }
        name_field_active = false;
    }

    void apply_move(SceneEditor& editor, const EntityUuid uuid) {
        try {
            const Vec3 position{position_field[0], position_field[1], position_field[2]};
            if (editor.move_unbound_entity(uuid, position)) {
                set_status("Moved placement.");
            } else {
                set_status("Move target no longer exists.");
            }
        } catch (const std::exception& error) {
            set_status(std::string{"Move rejected: "} + error.what());
        }
        position_field_active = false;
    }

    void destroy_instance(SceneEditor& editor, const EntityUuid uuid) {
        try {
            if (editor.destroy_prefab_instance(uuid)) {
                selection = {};
                set_status("Destroyed prefab instance.");
            } else {
                set_status("Prefab instance no longer exists.");
            }
        } catch (const std::exception& error) {
            set_status(std::string{"Destroy rejected: "} + error.what());
        }
    }

    void draw_prefab_creation(SceneEditor& editor) {
        ImGui::Separator();
        if (!ImGui::CollapsingHeader("Instantiate prefab")) {
            return;
        }
        const std::vector<SceneDocumentPrefab> prefabs = editor.prefabs();
        if (prefabs.empty()) {
            ImGui::TextDisabled("This scene declares no prefabs.");
            return;
        }
        selected_prefab =
            std::clamp(selected_prefab, 0, static_cast<int>(prefabs.size()) - 1);
        if (ImGui::BeginCombo("Prefab",
                              prefabs[static_cast<std::size_t>(selected_prefab)].id.c_str())) {
            for (std::size_t index = 0; index < prefabs.size(); ++index) {
                const bool selected = static_cast<int>(index) == selected_prefab;
                if (ImGui::Selectable(prefabs[index].id.c_str(), selected)) {
                    selected_prefab = static_cast<int>(index);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::InputText("Instance id", new_instance_id.data(), new_instance_id.size());
        ImGui::InputText("Display name", new_instance_name.data(), new_instance_name.size());
        ImGui::DragFloat3("At", new_instance_position.data(), 0.5F);
        if (!ImGui::Button("Create instance")) {
            return;
        }
        try {
            const ScenePrefabPlacement placement{
                .prefab_id = prefabs[static_cast<std::size_t>(selected_prefab)].id,
                .instance_id = new_instance_id.data(),
                .name = new_instance_name.data(),
                .position = {new_instance_position[0], new_instance_position[1],
                             new_instance_position[2]},
            };
            const EntityUuid created = editor.create_prefab_instance(placement);
            if (created) {
                selection = created;
                set_status("Created instance " + placement.instance_id + ".");
            } else {
                set_status("Unknown prefab: " + placement.prefab_id);
            }
        } catch (const std::exception& error) {
            set_status(std::string{"Create rejected: "} + error.what());
        }
    }

    void draw_history(SceneEditor& editor) {
        if (!ImGui::Begin("History")) {
            ImGui::End();
            return;
        }
        ImGui::BeginDisabled(!editor.can_undo());
        if (ImGui::Button("Undo")) {
            static_cast<void>(editor.undo());
            set_status("Undid the last command.");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!editor.can_redo());
        if (ImGui::Button("Redo")) {
            static_cast<void>(editor.redo());
            set_status("Redid the last undone command.");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu undone", editor.undone_count());

        ImGui::Separator();
        const std::vector<SceneEdit> history = editor.history();
        if (history.empty()) {
            ImGui::TextDisabled("No commands applied.");
        }
        for (std::size_t index = history.size(); index > 0; --index) {
            ImGui::BulletText("%s", history[index - 1].label.c_str());
        }
        ImGui::End();
    }

    void draw_statistics(SceneEditor& editor, const EditorStats& stats, EditorActions& actions) {
        if (!ImGui::Begin("Statistics")) {
            ImGui::End();
            return;
        }
        ImGui::Text("%d FPS | fixed %.0f Hz | tick %llu", stats.frames_per_second,
                    stats.fixed_update_hz,
                    static_cast<unsigned long long>(stats.simulated_ticks));
        ImGui::Text("entities %zu | bodies %zu | textures %zu", stats.entity_count,
                    stats.physics_body_count, stats.loaded_texture_count);
        ImGui::Text("sprites %zu visible | %zu culled | %zu batches", stats.visible_sprites,
                    stats.culled_sprites, stats.estimated_batches);
        const bool gameplay_has_keys =
            !backend.wants_text_input() && !backend.item_active();
        ImGui::TextColored(gameplay_has_keys ? ImVec4{0.45F, 0.87F, 0.75F, 1.0F}
                                             : ImVec4{0.65F, 0.68F, 0.75F, 1.0F},
                           gameplay_has_keys
                               ? "Movement keys: game"
                               : "Movement keys: editor field being edited");
        ImGui::Separator();
        ImGui::Text("document %s", editor.document().source_path().filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextColored(editor.modified() ? ImVec4{0.97F, 0.76F, 0.35F, 1.0F}
                                             : ImVec4{0.45F, 0.87F, 0.75F, 1.0F},
                           editor.modified() ? "(unsaved)" : "(saved)");
        ImGui::BeginDisabled(!editor.modified());
        if (ImGui::Button("Save scene")) {
            save(editor);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Apply to running scene")) {
            actions.apply_document_to_running_scene = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(stats.paused ? "Resume" : "Pause")) {
            actions.toggle_pause = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            actions.reset_running_scene = true;
        }
        ImGui::End();
    }

    void draw_debug_channels(DebugVisuals& visuals) {
        if (!ImGui::Begin("Debug channels")) {
            ImGui::End();
            return;
        }
        bool enabled = visuals.enabled();
        if (ImGui::Checkbox("Debug visuals (F1)", &enabled)) {
            visuals.set_enabled(enabled);
        }
        ImGui::Separator();
        ImGui::BeginDisabled(!visuals.enabled());
        for (std::size_t index = 0; index < debug_channel_count; ++index) {
            const auto channel = static_cast<DebugChannel>(index);
            const bool implemented = debug_channel_implemented(channel);
            bool selected = visuals.channel_selected(channel);
            ImGui::BeginDisabled(!implemented);
            if (ImGui::Checkbox(std::string{debug_channel_name(channel)}.c_str(), &selected)) {
                visuals.set_channel_selected(channel, selected);
            }
            ImGui::EndDisabled();
            if (!implemented && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("No lighting module exists yet.");
            }
        }
        ImGui::EndDisabled();
        ImGui::End();
    }

    void draw_viewport(const EditorCanvas& canvas) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
        const bool open = ImGui::Begin("Viewport");
        ImGui::PopStyleVar();
        if (!open || canvas.texture_id == 0U || canvas.width <= 0 || canvas.height <= 0) {
            ImGui::End();
            return;
        }
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float scale = std::min(available.x / static_cast<float>(canvas.width),
                                     available.y / static_cast<float>(canvas.height));
        if (scale > 0.0F) {
            const ImVec2 size{static_cast<float>(canvas.width) * scale,
                              static_cast<float>(canvas.height) * scale};
            const ImVec2 cursor = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2{cursor.x + (available.x - size.x) * 0.5F,
                                       cursor.y + (available.y - size.y) * 0.5F});
            // The canvas is a render target, so its rows arrive bottom-up.
            ImGui::Image(static_cast<ImTextureID>(canvas.texture_id), size,
                         ImVec2{0.0F, 1.0F}, ImVec2{1.0F, 0.0F});
        }
        ImGui::End();
    }
};

EditorShell::EditorShell() : impl_{std::make_unique<Impl>()} {
    if (impl_->backend.available()) {
        log(LogLevel::info, "Development editor shell initialized.");
    }
}

EditorShell::~EditorShell() = default;
EditorShell::EditorShell(EditorShell&&) noexcept = default;
EditorShell& EditorShell::operator=(EditorShell&&) noexcept = default;

bool EditorShell::available() const noexcept { return impl_->backend.available(); }
bool EditorShell::visible() const noexcept { return impl_->visible && available(); }
void EditorShell::set_visible(const bool visible) noexcept { impl_->visible = visible; }
void EditorShell::toggle_visible() noexcept { impl_->visible = !impl_->visible; }

bool EditorShell::blocks_gameplay_input() const noexcept {
    return visible() &&
           (impl_->backend.wants_text_input() || impl_->backend.item_active());
}

bool EditorShell::wants_mouse() const noexcept {
    return visible() && impl_->backend.wants_mouse();
}

EditorActions EditorShell::draw(
    SceneEditor& scene_editor,
    DebugVisuals& debug_visuals,
    const EditorStats& stats,
    const EditorCanvas& canvas
) {
    EditorActions actions{};
    if (!visible() || !impl_->backend.new_frame()) {
        return actions;
    }

    const ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (!impl_->layout_built) {
        impl_->build_layout(dockspace);
        impl_->layout_built = true;
    }

    const std::vector<SceneDocumentEntity> entities = scene_editor.entities();
    impl_->refresh_selection_fields(entities);

    impl_->draw_menu_bar(scene_editor, debug_visuals, actions);
    impl_->draw_hierarchy(entities);
    impl_->draw_inspector(scene_editor, entities);
    impl_->draw_history(scene_editor);
    impl_->draw_statistics(scene_editor, stats, actions);
    impl_->draw_debug_channels(debug_visuals);
    impl_->draw_viewport(canvas);

    impl_->backend.render();
    return actions;
}

} // namespace ic2d
