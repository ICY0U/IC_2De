#include "ic2d/editor.hpp"

#include "ic2d/core/log.hpp"
#include "editor/editor_layout.hpp"
#include "editor/editor_ui.hpp"
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
#include <cstdio>
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

[[nodiscard]] const char* enemy_intent_state_name(const EnemyIntentState state) noexcept {
    switch (state) {
    case EnemyIntentState::unaware:
        return "UNAWARE";
    case EnemyIntentState::pursuing:
        return "PURSUING";
    case EnemyIntentState::attacking:
        return "ATTACKING";
    case EnemyIntentState::inactive:
        return "INACTIVE";
    }
    return "UNKNOWN";
}

// The row tag is the one-word classification shown in the Hierarchy's right
// column. The name column carries the name alone so it stays readable.
[[nodiscard]] const char* entity_tag(const SceneDocumentEntity& entity) noexcept {
    if (!entity.prefab_id.empty()) {
        return "prefab";
    }
    return entity.physics_bound ? "body" : "entity";
}

[[nodiscard]] ImU32 entity_tag_color(const SceneDocumentEntity& entity) noexcept {
    if (!entity.prefab_id.empty()) {
        return editor_ui::colors::highlight;
    }
    return entity.physics_bound ? editor_ui::colors::accent : editor_ui::colors::text_muted;
}

// Search matches anything a reader can see for the placement, so typing a
// prefab id finds every instance of it.
[[nodiscard]] std::string entity_search_text(const SceneDocumentEntity& entity) {
    std::string text = entity.name + " " + entity.id;
    if (!entity.prefab_id.empty()) {
        text += " " + entity.prefab_id;
    }
    return text;
}

// Right-aligns the next single-line text inside the current table cell.
void align_text_right(const char* text) {
    const float offset = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text).x;
    if (offset > 0.0F) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
    }
}

void push_entity_id(const EntityUuid uuid) {
    ImGui::PushID(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(uuid.value)));
}

} // namespace

bool editor_blocks_gameplay_input(
    const bool wants_text_input,
    const bool /*item_active*/
) noexcept {
    // Mouse interaction is routed separately using the actual gameplay
    // viewport rectangle. Treating every active ImGui item as keyboard capture
    // made a left-click on the viewport erase held WASD for the next frame.
    return wants_text_input;
}

struct EditorShell::Impl {
    ImGuiRaylibBackend backend{&editor_ui::configure_fonts};
    std::filesystem::path layout_path;
    bool layout_persistence_available{false};
    bool layout_restored{false};
    bool layout_reported{false};
    bool visible{false};
    bool layout_built{false};

    EntityUuid selection{};
    EntityUuid buffered_selection{};
    bool reveal_selection{false};

    bool pending_pick{false};
    Vec2 pending_pick_point{};
    std::optional<Vec2> viewport_pointer;

    // Reading the document parses its text, so derived views are cached until
    // a command, undo, or redo changes it.
    std::vector<SceneDocumentEntity> cached_entities;
    std::vector<SceneDocumentPrefab> cached_prefabs;
    ImGuiTextFilter hierarchy_filter;
    std::uint64_t cached_revision{0};
    bool cache_valid{false};
    TextField name_field{};
    std::array<float, 3> position_field{};
    bool name_field_active{false};
    bool position_field_active{false};

    TextField new_instance_id{};
    TextField new_instance_name{};
    std::array<float, 3> new_instance_position{};
    int selected_prefab{0};

    std::string status{"Editor ready."};

    // A short rolling window of real frame durations. The engine reports
    // distributions, but a reader also needs to see a spike arrive.
    std::array<float, 120> frame_ms_history{};
    std::size_t frame_ms_cursor{0};

    void record_frame_time() {
        frame_ms_history[frame_ms_cursor] = ImGui::GetIO().DeltaTime * 1000.0F;
        frame_ms_cursor = (frame_ms_cursor + 1) % frame_ms_history.size();
    }

    explicit Impl(const std::filesystem::path& requested_layout_path) {
        if (backend.available()) {
            editor_ui::apply_theme();
        }
        layout_path = editor_detail::resolve_layout_path(requested_layout_path);
        std::string diagnostic;
        layout_persistence_available =
            editor_detail::prepare_layout_path(layout_path, diagnostic) &&
            backend.configure_layout_file(layout_path);
        layout_restored = layout_persistence_available &&
                          editor_detail::layout_file_is_usable(layout_path);
        layout_built = layout_restored;
        if (!layout_persistence_available) {
            if (diagnostic.empty()) {
                diagnostic = "Dear ImGui backend is unavailable.";
            }
            set_status("Layout persistence unavailable: " + diagnostic);
            log(LogLevel::warning, status);
        }
    }

    void set_status(std::string message) { status = std::move(message); }

    void build_layout(ImGuiID dockspace_id) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

        ImGuiID centre = dockspace_id;
        // The toolbar is a docked strip rather than a floating overlay so it
        // participates in the same layout file as every other panel.
        const float work_height = std::max(ImGui::GetMainViewport()->WorkSize.y, 1.0F);
        const float toolbar_ratio =
            std::clamp(editor_ui::toolbar_height() / work_height, 0.02F, 0.2F);
        const ImGuiID top =
            ImGui::DockBuilderSplitNode(centre, ImGuiDir_Up, toolbar_ratio, nullptr, &centre);
        const ImGuiID left =
            ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.19F, nullptr, &centre);
        const ImGuiID right =
            ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.25F, nullptr, &centre);
        const ImGuiID bottom =
            ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.28F, nullptr, &centre);

        // A tab bar or a drag handle on the toolbar would let it be resized or
        // buried, which is not what a transport strip is for.
        if (ImGuiDockNode* toolbar_node = ImGui::DockBuilderGetNode(top)) {
            toolbar_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar |
                                        ImGuiDockNodeFlags_NoResizeY |
                                        ImGuiDockNodeFlags_NoDockingSplit;
        }

        ImGui::DockBuilderDockWindow("Toolbar", top);
        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("History", bottom);
        ImGui::DockBuilderDockWindow("Debug channels", bottom);
        ImGui::DockBuilderDockWindow("Statistics", bottom);
        ImGui::DockBuilderDockWindow("Viewport", centre);
        ImGui::DockBuilderFinish(dockspace_id);
    }

    void save_layout() {
        if (layout_persistence_available && backend.save_layout_now()) {
            set_status("Workspace layout saved.");
            return;
        }
        set_status("Workspace layout could not be saved.");
    }

    void reset_layout() {
        std::string diagnostic;
        if (!layout_persistence_available ||
            !editor_detail::remove_layout_file(layout_path, diagnostic)) {
            set_status(diagnostic.empty() ? "Workspace layout could not be reset." : diagnostic);
            return;
        }
        layout_built = false;
        layout_restored = false;
        layout_reported = true;
        set_status("Default workspace restored.");
    }

    void refresh_cache(const SceneEditor& editor) {
        if (cache_valid && cached_revision == editor.revision()) {
            return;
        }
        cached_entities = editor.entities();
        cached_prefabs = editor.prefabs();
        cached_revision = editor.revision();
        cache_valid = true;
    }

    void invalidate_cache() noexcept { cache_valid = false; }

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

    void draw_menu_bar(
        SceneEditor& editor,
        DebugVisuals& visuals,
        const EditorStats& stats,
        EditorActions& actions
    ) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0F, 7.0F});
        const bool menu_open = ImGui::BeginMainMenuBar();
        ImGui::PopStyleVar();
        if (!menu_open) {
            return;
        }
        ImGui::PushFont(editor_ui::bold_font());
        ImGui::PushStyleColor(ImGuiCol_Text, editor_ui::colors::accent);
        ImGui::TextUnformatted("IC_2DE");
        ImGui::PopStyleColor();
        ImGui::PopFont();
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
        if (ImGui::BeginMenu("Debug")) {
            if (ImGui::BeginMenu("Enemy stress test")) {
                ImGui::TextDisabled(
                    "Active Runners: %zu", stats.enemy_intent.actors.size());
                ImGui::TextColored(
                    stats.enemy_attack_damage_enabled
                        ? ImVec4{0.97F, 0.76F, 0.35F, 1.0F}
                        : ImVec4{0.45F, 0.87F, 0.75F, 1.0F},
                    stats.enemy_attack_damage_enabled
                        ? "Player damage: normal"
                        : "Player damage: disabled (stress mode)");
                ImGui::Separator();
                constexpr std::array<std::size_t, 8> stress_presets{
                    10, 25, 50, 100, 200, 1000, 5000, 10000,
                };
                for (const std::size_t count : stress_presets) {
                    const std::string label = std::to_string(count) + " Runners (total)";
                    if (ImGui::MenuItem(
                            label.c_str(), nullptr, stats.enemy_intent.actors.size() == count)) {
                        actions.enemy_stress_target_count = count;
                        set_status("Requested " + std::to_string(count) +
                                   " total stress-test Runners.");
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Restore authored scene")) {
                    actions.enemy_stress_target_count = 0;
                    set_status("Requested authored Runner layout.");
                }
                ImGui::TextDisabled("Rebuilds the unsaved running copy");
                ImGui::TextDisabled("and leaves the scene file untouched.");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Load scene")) {
                if (stats.selectable_scenes.empty()) {
                    ImGui::TextDisabled("No authored scenes were discovered.");
                }
                for (const std::filesystem::path& scene : stats.selectable_scenes) {
                    const std::string label = scene.filename().string();
                    const bool loaded = scene == stats.loaded_scene;
                    if (ImGui::MenuItem(label.c_str(), nullptr, loaded)) {
                        actions.load_scene_path = scene;
                        set_status("Requested scene " + label + ".");
                    }
                }
                ImGui::Separator();
                ImGui::TextDisabled("Discards unsaved document edits.");
                ImGui::EndMenu();
            }
            bool health_bars_visible = stats.enemy_health_bars_visible;
            if (ImGui::MenuItem("Enemy health bars", nullptr, &health_bars_visible)) {
                actions.enemy_health_bars_visible = health_bars_visible;
                set_status(health_bars_visible ? "Enemy health bars shown."
                                               : "Enemy health bars hidden.");
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Workspace")) {
            if (ImGui::MenuItem("Save layout now")) {
                save_layout();
            }
            if (ImGui::MenuItem("Reset to default layout")) {
                reset_layout();
            }
            ImGui::Separator();
            ImGui::TextDisabled("Auto-saves on change and exit");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", layout_path.string().c_str());
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // A docked strip directly under the menu bar: document actions on the
    // left, transport in the exact centre, document identity on the right.
    void draw_toolbar(SceneEditor& editor, const EditorStats& stats, EditorActions& actions) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0F, 6.0F});
        ImGui::PushStyleColor(
            ImGuiCol_WindowBg, editor_ui::to_vec4(editor_ui::colors::titlebar));
        const bool open = ImGui::Begin(
            "Toolbar", nullptr,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoCollapse);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (!open) {
            ImGui::End();
            return;
        }

        // The primary action is only primary while there is something to save.
        // A dimmed accent button still reads as the thing to click.
        const bool modified = editor.modified();
        ImGui::BeginDisabled(!modified);
        const bool save_pressed = modified ? editor_ui::accent_button("Save scene")
                                           : editor_ui::tool_button("Save scene");
        ImGui::EndDisabled();
        if (save_pressed) {
            save(editor);
        }
        ImGui::SameLine();
        if (editor_ui::tool_button("Apply to runtime")) {
            actions.apply_document_to_running_scene = true;
            set_status("Applied the document to the running scene.");
        }

        constexpr float transport_button_width = 92.0F;
        const float transport_width =
            transport_button_width * 2.0F + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                      (ImGui::GetWindowWidth() - transport_width) * 0.5F));
        if (editor_ui::tool_button(stats.paused ? "Resume" : "Pause", stats.paused,
                                   transport_button_width)) {
            actions.toggle_pause = true;
        }
        ImGui::SameLine();
        if (editor_ui::tool_button("Restart", false, transport_button_width)) {
            actions.reset_running_scene = true;
            set_status("Reset the running scene.");
        }

        const std::string scene_name =
            editor.document().source_path().filename().string();
        const char* save_state = editor.modified() ? "UNSAVED" : "SAVED";
        const float right_width = ImGui::CalcTextSize(scene_name.c_str()).x +
                                  ImGui::CalcTextSize(save_state).x +
                                  ImGui::GetStyle().ItemSpacing.x + 16.0F;
        ImGui::SameLine();
        ImGui::SetCursorPosX(
            std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - right_width - 10.0F));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(scene_name.c_str());
        editor_ui::badge_same_line(editor.modified() ? editor_ui::colors::warning
                                                     : editor_ui::colors::positive,
                                   "%s", save_state);
        ImGui::End();
    }

    // Drawn before the dockspace so Dear ImGui removes its height from the
    // work area and no panel is ever hidden behind it.
    void draw_status_bar(const EditorStats& stats) {
        ImGui::PushStyleColor(
            ImGuiCol_WindowBg, editor_ui::to_vec4(editor_ui::colors::titlebar));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{12.0F, 4.0F});
        static_cast<void>(ImGui::BeginViewportSideBar(
            "##status_bar", ImGui::GetMainViewport(), ImGuiDir_Down,
            editor_ui::status_bar_height(),
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoSavedSettings));

        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, editor_ui::colors::accent);
        ImGui::TextUnformatted("*");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextUnformatted(status.c_str());

        std::array<char, 128> summary{};
        static_cast<void>(std::snprintf(
            summary.data(), summary.size(),
            "%d FPS   |   %.2f ms p50   |   %zu entities   |   tick %llu",
            stats.frames_per_second, stats.frame_time_p50_ms, stats.entity_count,
            static_cast<unsigned long long>(stats.simulated_ticks)));
        const float summary_width = ImGui::CalcTextSize(summary.data()).x;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                      ImGui::GetWindowWidth() - summary_width - 12.0F));
        editor_ui::text_dim("%s", summary.data());

        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
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
        if (editor_ui::search_field("hierarchy_search", hierarchy_filter.InputBuf,
                                    IM_ARRAYSIZE(hierarchy_filter.InputBuf),
                                    "Search name, id, or prefab")) {
            hierarchy_filter.Build();
        }

        std::size_t shown = 0;
        const float footer = ImGui::GetTextLineHeightWithSpacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
        if (ImGui::BeginChild("##placements", ImVec2{0.0F, -footer},
                              ImGuiChildFlags_None)) {
            constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg |
                                                    ImGuiTableFlags_NoSavedSettings |
                                                    ImGuiTableFlags_PadOuterX;
            if (ImGui::BeginTable("##placement_rows", 2, table_flags)) {
                ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("tag", ImGuiTableColumnFlags_WidthFixed, 54.0F);
                for (const SceneDocumentEntity& entity : entities) {
                    const std::string searchable = entity_search_text(entity);
                    if (!hierarchy_filter.PassFilter(
                            searchable.c_str(), searchable.c_str() + searchable.size())) {
                        continue;
                    }
                    ++shown;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    push_entity_id(entity.uuid);
                    const bool selected = selection == entity.uuid;
                    if (ImGui::Selectable(entity.name.c_str(), selected,
                                          ImGuiSelectableFlags_SpanAllColumns)) {
                        selection = entity.uuid;
                    }
                    if (reveal_selection && selected) {
                        ImGui::SetScrollHereY(0.5F);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("id %s\nuuid %llu", entity.id.c_str(),
                                          static_cast<unsigned long long>(entity.uuid.value));
                    }
                    ImGui::TableSetColumnIndex(1);
                    const char* tag = entity_tag(entity);
                    align_text_right(tag);
                    editor_ui::text_colored(entity_tag_color(entity), "%s", tag);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        if (shown == entities.size()) {
            editor_ui::text_dim("%zu placements", entities.size());
        } else {
            editor_ui::text_dim("%zu of %zu placements", shown, entities.size());
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
            editor_ui::text_dim("Nothing selected.");
            ImGui::TextWrapped(
                "Pick a placement in the Hierarchy, or Ctrl+click one in the Viewport.");
            ImGui::Spacing();
            draw_prefab_creation(editor);
            ImGui::End();
            return;
        }

        ImGui::PushFont(editor_ui::bold_font());
        ImGui::PushStyleColor(ImGuiCol_Text, editor_ui::colors::text_bright);
        ImGui::TextUnformatted(found->name.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        editor_ui::badge(entity_tag_color(*found), "%s", entity_tag(*found));
        if (found->physics_bound && !found->prefab_id.empty()) {
            editor_ui::badge_same_line(editor_ui::colors::accent, "body");
        }

        editor_ui::section_header("Entity");
        if (editor_ui::begin_property_grid("##entity_properties")) {
            editor_ui::property_label("Name");
            ImGui::InputText("##name", name_field.data(), name_field.size());
            name_field_active = ImGui::IsItemActive();
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                apply_rename(editor, found->uuid);
            }
            editor_ui::property_text("Id", "%s", found->id.c_str());
            editor_ui::property_text("UUID", "%llu",
                                     static_cast<unsigned long long>(found->uuid.value));
            if (found->prefab_id.empty()) {
                editor_ui::property_text_colored(editor_ui::colors::text_muted, "Source",
                                                 "authored");
            } else {
                editor_ui::property_text_colored(editor_ui::colors::highlight, "Prefab", "%s",
                                                 found->prefab_id.c_str());
            }
            editor_ui::end_property_grid();
        }

        editor_ui::section_header("Transform");
        ImGui::BeginDisabled(found->physics_bound);
        if (editor_ui::begin_property_grid("##transform_properties")) {
            const editor_ui::Vec3ControlResult position =
                editor_ui::vec3_control("Position", position_field.data());
            position_field_active = position.active;
            if (position.deactivated_after_edit) {
                apply_move(editor, found->uuid);
            }
            editor_ui::end_property_grid();
        }
        ImGui::EndDisabled();
        if (found->physics_bound) {
            // Wrapped, because the note has to stay readable in a narrow dock.
            ImGui::PushStyleColor(ImGuiCol_Text, editor_ui::colors::text_muted);
            ImGui::TextWrapped("X and Z are owned by the bound physics body.");
            ImGui::PopStyleColor();
        }

        if (!found->prefab_id.empty()) {
            editor_ui::section_header("Instance");
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  editor_ui::to_vec4(IM_COL32(96, 42, 42, 255)));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  editor_ui::to_vec4(IM_COL32(132, 56, 56, 255)));
            if (ImGui::Button("Destroy instance", ImVec2{-FLT_MIN, 0.0F})) {
                destroy_instance(editor, found->uuid);
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::Spacing();
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
        if (!ImGui::CollapsingHeader("Instantiate prefab")) {
            return;
        }
        const std::vector<SceneDocumentPrefab>& prefabs = cached_prefabs;
        if (prefabs.empty()) {
            editor_ui::text_dim("This scene declares no prefabs.");
            return;
        }
        selected_prefab =
            std::clamp(selected_prefab, 0, static_cast<int>(prefabs.size()) - 1);
        if (editor_ui::begin_property_grid("##prefab_properties")) {
            editor_ui::property_label("Prefab");
            if (ImGui::BeginCombo(
                    "##prefab",
                    prefabs[static_cast<std::size_t>(selected_prefab)].id.c_str())) {
                for (std::size_t index = 0; index < prefabs.size(); ++index) {
                    const bool selected = static_cast<int>(index) == selected_prefab;
                    if (ImGui::Selectable(prefabs[index].id.c_str(), selected)) {
                        selected_prefab = static_cast<int>(index);
                    }
                }
                ImGui::EndCombo();
            }
            editor_ui::property_label("Instance id");
            ImGui::InputText("##instance_id", new_instance_id.data(), new_instance_id.size());
            editor_ui::property_label("Display name");
            ImGui::InputText("##instance_name", new_instance_name.data(),
                             new_instance_name.size());
            static_cast<void>(
                editor_ui::vec3_control("Position", new_instance_position.data()));
            editor_ui::end_property_grid();
        }
        ImGui::Spacing();
        if (!editor_ui::accent_button("Create instance", -FLT_MIN)) {
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
        const float half =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
        ImGui::BeginDisabled(!editor.can_undo());
        if (editor_ui::tool_button("Undo", false, half)) {
            static_cast<void>(editor.undo());
            set_status("Undid the last command.");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!editor.can_redo());
        if (editor_ui::tool_button("Redo", false, half)) {
            static_cast<void>(editor.redo());
            set_status("Redid the last undone command.");
        }
        ImGui::EndDisabled();
        if (editor.undone_count() > 0) {
            editor_ui::badge(editor_ui::colors::text_muted, "%zu undone",
                             editor.undone_count());
        }

        const std::vector<SceneEdit> history = editor.history();
        editor_ui::section_header("Applied commands");
        // A transparent list keeps the panel one surface instead of a box
        // inside a box, which is what a nested child background reads as.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
        if (ImGui::BeginChild("##history_rows")) {
            if (history.empty()) {
                editor_ui::text_dim("No commands applied.");
            }
            // Newest first: the entry a reader most often wants to undo is the
            // one that does not require scrolling.
            for (std::size_t index = history.size(); index > 0; --index) {
                const bool newest = index == history.size();
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      newest ? editor_ui::colors::text_bright
                                             : editor_ui::colors::text_dim);
                ImGui::Text("%2zu   %s", index, history[index - 1].label.c_str());
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::End();
    }

    void draw_statistics(SceneEditor& editor, const EditorStats& stats, EditorActions& actions) {
        if (!ImGui::Begin("Statistics")) {
            ImGui::End();
            return;
        }
        if (!ImGui::BeginTabBar("Statistics sections")) {
            ImGui::End();
            return;
        }
        if (ImGui::BeginTabItem("Overview")) {
            editor_ui::section_header("Frame");
            ImGui::PushStyleColor(ImGuiCol_FrameBg,
                                  editor_ui::to_vec4(editor_ui::colors::property_field));
            ImGui::PlotLines("##frame_ms", frame_ms_history.data(),
                             static_cast<int>(frame_ms_history.size()),
                             static_cast<int>(frame_ms_cursor), nullptr, 0.0F, FLT_MAX,
                             ImVec2{-FLT_MIN, 46.0F});
            ImGui::PopStyleColor();
            if (editor_ui::begin_property_grid("##frame_stats")) {
                editor_ui::property_text("Rate", "%d FPS   |   fixed %.0f Hz",
                                         stats.frames_per_second, stats.fixed_update_hz);
                editor_ui::property_text("Frame ms", "p50 %.2f   |   p95 %.2f   |   p99 %.2f",
                                         stats.frame_time_p50_ms, stats.frame_time_p95_ms,
                                         stats.frame_time_p99_ms);
                editor_ui::property_text("Simulated", "tick %llu",
                                         static_cast<unsigned long long>(stats.simulated_ticks));
                editor_ui::end_property_grid();
            }

            editor_ui::section_header("Scene");
            if (editor_ui::begin_property_grid("##scene_stats")) {
                editor_ui::property_text("Entities", "%zu", stats.entity_count);
                editor_ui::property_text("Physics bodies", "%zu", stats.physics_body_count);
                editor_ui::property_text("Textures", "%zu", stats.loaded_texture_count);
                editor_ui::property_text("CPU workers", "%zu", stats.cpu_worker_count);
                editor_ui::end_property_grid();
            }

            editor_ui::section_header("Rendering");
            if (editor_ui::begin_property_grid("##render_stats")) {
                editor_ui::property_text("Sprites", "%zu visible   |   %zu culled",
                                         stats.visible_sprites, stats.culled_sprites);
                editor_ui::property_text("Submission",
                                         "%zu batches   |   %zu draws   |   %zu vertices",
                                         stats.estimated_batches, stats.estimated_draw_calls,
                                         stats.visible_vertices);
                editor_ui::property_text("Passes",
                                         "%u GPU   |   %u target switches   |   %u shader",
                                         stats.estimated_gpu_passes,
                                         stats.render_target_switches, stats.shader_passes);
                editor_ui::end_property_grid();
            }

            editor_ui::section_header("Pipeline");
            editor_ui::badge(stats.post_process_active ? editor_ui::colors::positive
                                                       : editor_ui::colors::text_muted,
                             "POST %s", stats.post_process_active ? "ACTIVE" : "BYPASSED");
            editor_ui::badge_same_line(stats.texture_hot_reload_enabled
                                           ? editor_ui::colors::positive
                                           : editor_ui::colors::text_muted,
                                       "HOT SWAP %s",
                                       stats.texture_hot_reload_enabled ? "ON" : "OFF");
            if (!stats.post_process_available) {
                editor_ui::badge_same_line(editor_ui::colors::warning, "SHADER MISSING");
            }
            if (editor_ui::begin_property_grid("##pipeline_stats")) {
                editor_ui::property_text("Watched textures",
                                         "%zu watched   |   %zu reloaded   |   %zu rejected",
                                         stats.watched_texture_count,
                                         stats.successful_texture_reloads,
                                         stats.failed_texture_reloads);
                if (stats.gameplay_digest) {
                    editor_ui::property_text("Gameplay digest", "v%u   |   %llu",
                                             stats.gameplay_digest->schema_version,
                                             static_cast<unsigned long long>(
                                                 stats.gameplay_digest->value));
                } else {
                    editor_ui::property_text_colored(editor_ui::colors::text_muted,
                                                     "Gameplay digest",
                                                     "waiting for a completed fixed tick");
                }
                editor_ui::end_property_grid();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Navigation")) {
        ImGui::SeparatorText("Navigation grid");
        ImGui::Text("%d x %d cells | %.1f world units per cell",
                    stats.navigation_grid.columns,
                    stats.navigation_grid.rows,
                    stats.navigation_grid.cell_size);
        ImGui::Text("%zu walkable | %zu hard blocked",
                    stats.navigation_grid.walkable_cell_count,
                    stats.navigation_grid.blocked_cell_count);
        ImGui::Text("agent clearance %.1f X / %.1f Z | max step %.1f",
                    stats.navigation_grid.agent_half_extents.x,
                    stats.navigation_grid.agent_half_extents.y,
                    stats.navigation_grid.max_step_height);
        ImGui::TextDisabled(
            "Read-only bake. Enable Navigation grid in Debug channels.");
        ImGui::SeparatorText("Displayed A-star path");
        const std::string_view path_status = nav_path_status_name(stats.navigation_path.status);
        ImGui::Text("%.*s | %zu cells | %.1f world units",
                    static_cast<int>(path_status.size()), path_status.data(),
                    stats.navigation_path.cells.size(), stats.navigation_path.total_distance);
        ImGui::Text("%zu cells expanded", stats.navigation_path.expanded_cell_count);
        ImGui::TextDisabled(
            "Shows the active Runner route when pursuing, otherwise the standalone reference route.");
        ImGui::SeparatorText("Path-following agents");
        ImGui::Text("tick %llu | %llu searches | repath every %u ticks",
                    static_cast<unsigned long long>(stats.navigation_agents.tick),
                    static_cast<unsigned long long>(
                        stats.navigation_agents.total_search_count),
                    stats.navigation_agents.repath_interval_ticks);
        const std::size_t following_agents = static_cast<std::size_t>(std::ranges::count_if(
            stats.navigation_agents.actors,
            [](const NavAgentStateSnapshot& agent) { return agent.active; }));
        const std::size_t found_routes = static_cast<std::size_t>(std::ranges::count_if(
            stats.navigation_agents.actors,
            [](const NavAgentStateSnapshot& agent) {
                return agent.path_status == NavPathStatus::found;
            }));
        ImGui::Text("%zu agents | %zu following | %zu routes found",
                    stats.navigation_agents.actors.size(), following_agents, found_routes);
        if (stats.navigation_agents.actors.empty()) {
            ImGui::TextDisabled("No authored navigation agents.");
        }
        if (!stats.navigation_agents.actors.empty() &&
            ImGui::CollapsingHeader("Per-agent navigation details")) {
          for (const NavAgentStateSnapshot& agent : stats.navigation_agents.actors) {
            const std::string_view agent_status = nav_path_status_name(agent.path_status);
            ImGui::TextColored(
                agent.active ? ImVec4{0.45F, 0.87F, 0.75F, 1.0F}
                             : ImVec4{0.65F, 0.68F, 0.75F, 1.0F},
                "actor %llu -> %llu | %s | %.*s",
                static_cast<unsigned long long>(agent.actor.value),
                static_cast<unsigned long long>(agent.target.value),
                agent.active ? "FOLLOWING" : "IDLE",
                static_cast<int>(agent_status.size()), agent_status.data());
            ImGui::Text("path %zu cells | next %zu | searches %llu | reached %llu",
                        agent.path.size(), agent.waypoint_index,
                        static_cast<unsigned long long>(agent.search_count),
                        static_cast<unsigned long long>(agent.waypoint_advance_count));
            ImGui::Text("move X %.2f Z %.2f | next repath tick %llu",
                        agent.movement_direction.x, agent.movement_direction.y,
                        static_cast<unsigned long long>(agent.next_repath_tick));
          }
        }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Gameplay")) {
        if (ImGui::CollapsingHeader("Input", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Aim: %s | stick X %.2f Z %.2f",
                    stats.input.gameplay.aim.pointer_active ? "MOUSE" : "RIGHT STICK",
                    stats.input.gameplay.aim.horizontal,
                    stats.input.gameplay.aim.depth);
        if (stats.input.gameplay.aim.pointer_active) {
            ImGui::SameLine();
            ImGui::Text("| pointer %.0f, %.0f",
                        stats.input.gameplay.aim.pointer_screen_x,
                        stats.input.gameplay.aim.pointer_screen_y);
        }
        for (const GameplayAction action : gameplay_actions) {
            const ButtonState state = stats.input.gameplay.action(action);
            const char* state_label = state.pressed ? "PRESSED" : state.down ? "DOWN" : "ready";
            ImGui::TextColored(state.down ? ImVec4{0.97F, 0.76F, 0.35F, 1.0F}
                                          : ImVec4{0.65F, 0.68F, 0.75F, 1.0F},
                               "%s: %s", gameplay_action_name(action).data(), state_label);
        }
        ImGui::TextDisabled("LMB/R/Space/E/Q or wheel/X | pad RT/X/A/B/Y/LB");
        ImGui::TextDisabled("Development reset moved from R to F5.");
        }
        if (ImGui::CollapsingHeader("Combat & dodge", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("fixed tick %llu | queued %zu",
                    static_cast<unsigned long long>(stats.combat.tick),
                    stats.combat.pending_command_count);
        ImGui::Text("commands %llu consumed | intents %llu emitted",
                    static_cast<unsigned long long>(stats.combat.consumed_command_count),
                    static_cast<unsigned long long>(stats.combat.emitted_intent_count));
        ImGui::Text("world aim X %.2f Z %.2f", stats.combat.aim_direction.x,
                    stats.combat.aim_direction.y);
        ImGui::Text("Needle pistol: %u / %u",
                    stats.combat.weapon.magazine_ammo,
                    stats.combat.weapon.reserve_ammo);
        if (stats.combat.weapon.reloading) {
            ImGui::TextColored(ImVec4{0.45F, 0.87F, 0.75F, 1.0F},
                               "Reloading: %u ticks remaining",
                               stats.combat.weapon.reload_ticks_remaining);
        } else {
            ImGui::Text("Fire cooldown: %u ticks remaining",
                        stats.combat.weapon.fire_cooldown_ticks_remaining);
        }
        const DodgeSnapshot& dodge = stats.combat.dodge;
        if (dodge.active) {
            ImGui::TextColored(dodge.invulnerable
                                   ? ImVec4{0.45F, 0.87F, 0.75F, 1.0F}
                                   : ImVec4{0.97F, 0.76F, 0.35F, 1.0F},
                               "Dodge ACTIVE | %u active | %u invulnerable",
                               dodge.active_ticks_remaining,
                               dodge.invulnerable_ticks_remaining);
        } else {
            ImGui::Text("Dodge ready: %s | cooldown %u ticks",
                        dodge.cooldown_ticks_remaining == 0 ? "yes" : "no",
                        dodge.cooldown_ticks_remaining);
        }
        ImGui::Text("Dodge starts: %llu",
                    static_cast<unsigned long long>(dodge.started_count));
        ImGui::Text("Dodge direction: X %.2f Z %.2f | travelled %.1f",
                    dodge.direction.x, dodge.direction.y,
                    stats.dodge_distance_travelled);
        if (stats.dodge_movement_blocked) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4{0.97F, 0.55F, 0.25F, 1.0F}, "BLOCKED");
        }
        if (stats.last_dodge) {
            ImGui::TextDisabled("last dodge: tick %llu | event #%llu | X %.2f Z %.2f",
                                static_cast<unsigned long long>(stats.last_dodge->tick),
                                static_cast<unsigned long long>(stats.last_dodge->sequence),
                                stats.last_dodge->direction.x,
                                 stats.last_dodge->direction.y);
        }
        }
        if (ImGui::CollapsingHeader("Projectiles")) {
        ImGui::Text("Projectiles: %llu spawned | %llu observed",
                    static_cast<unsigned long long>(stats.combat.spawned_projectile_count),
                    static_cast<unsigned long long>(stats.observed_projectiles));
        ImGui::Text("Projectile simulation: %zu active | %llu impacted | %llu expired",
                    stats.projectiles.active.size(),
                    static_cast<unsigned long long>(stats.projectiles.total_impacted),
                    static_cast<unsigned long long>(stats.projectiles.total_expired));
        if (stats.last_projectile) {
            ImGui::TextColored(ImVec4{0.45F, 0.87F, 0.75F, 1.0F},
                               "last projectile #%llu | event #%llu | %.0f speed | %.0f damage",
                               static_cast<unsigned long long>(stats.last_projectile->projectile_id),
                               static_cast<unsigned long long>(stats.last_projectile->sequence),
                               stats.last_projectile->speed,
                               stats.last_projectile->damage);
        } else {
            ImGui::TextDisabled("last projectile: none");
        }
        if (stats.last_expired_projectile) {
            ImGui::TextDisabled("last expiry: projectile #%llu at tick %llu",
                                static_cast<unsigned long long>(
                                    stats.last_expired_projectile->projectile_id),
                                static_cast<unsigned long long>(
                                    stats.last_expired_projectile->tick));
        }
        if (stats.last_projectile_impact) {
            ImGui::TextColored(ImVec4{0.97F, 0.76F, 0.35F, 1.0F},
                               "last impact: projectile #%llu | target %llu | tag %u | %.0f damage",
                               static_cast<unsigned long long>(
                                   stats.last_projectile_impact->projectile_id),
                               static_cast<unsigned long long>(
                                   stats.last_projectile_impact->target.value),
                               stats.last_projectile_impact->tag,
                                stats.last_projectile_impact->damage);
        }
        }
        if (ImGui::CollapsingHeader("Enemy intent")) {
        ImGui::Text("fixed tick %llu | %llu acquired | %llu attacks",
                    static_cast<unsigned long long>(stats.enemy_intent.tick),
                    static_cast<unsigned long long>(stats.enemy_intent.acquisition_count),
                    static_cast<unsigned long long>(stats.enemy_intent.attack_count));
        const std::size_t pursuing_enemies = static_cast<std::size_t>(std::ranges::count_if(
            stats.enemy_intent.actors,
            [](const EnemyActorIntentSnapshot& enemy) {
                return enemy.state == EnemyIntentState::pursuing;
            }));
        const std::size_t attacking_enemies = static_cast<std::size_t>(std::ranges::count_if(
            stats.enemy_intent.actors,
            [](const EnemyActorIntentSnapshot& enemy) {
                return enemy.state == EnemyIntentState::attacking;
            }));
        ImGui::Text("%zu Runners | %zu pursuing | %zu attacking",
                    stats.enemy_intent.actors.size(), pursuing_enemies, attacking_enemies);
        if (stats.enemy_intent.actors.empty()) {
            ImGui::TextDisabled("No authored attacker intent actors.");
        }
        if (!stats.enemy_intent.actors.empty() &&
            ImGui::CollapsingHeader("Per-enemy intent details")) {
          for (const EnemyActorIntentSnapshot& enemy : stats.enemy_intent.actors) {
            ImGui::TextColored(
                enemy.state == EnemyIntentState::attacking
                    ? ImVec4{0.97F, 0.76F, 0.35F, 1.0F}
                    : ImVec4{0.45F, 0.87F, 0.75F, 1.0F},
                "actor %llu -> %llu | %s | range %.1f",
                static_cast<unsigned long long>(enemy.actor.value),
                static_cast<unsigned long long>(enemy.target.value),
                enemy_intent_state_name(enemy.state), enemy.distance_to_target);
            ImGui::Text("move X %.2f Z %.2f | cooldown %u | attacks %llu",
                        enemy.movement_direction.x, enemy.movement_direction.y,
                        enemy.attack_cooldown_ticks_remaining,
                        static_cast<unsigned long long>(enemy.attack_count));
          }
        }
        ImGui::Text("collision-resolved travel %.1f | player damage %.0f",
                    stats.enemy_distance_travelled,
                    stats.enemy_damage_applied_to_player);
        if (!stats.enemy_attack_damage_enabled) {
            ImGui::TextColored(
                ImVec4{0.45F, 0.87F, 0.75F, 1.0F},
                "Stress mode: attack requests are active; player damage is suppressed.");
        }
        if (stats.enemy_movement_blocked) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4{0.97F, 0.55F, 0.25F, 1.0F}, "BLOCKED");
        }
        ImGui::TextDisabled("attacks rejected by dodge invulnerability: %llu",
                            static_cast<unsigned long long>(
                                stats.invulnerable_enemy_attacks_rejected));
        if (stats.last_enemy_acquisition) {
            ImGui::TextDisabled("last acquire: tick %llu | actor %llu | event #%llu",
                                static_cast<unsigned long long>(
                                    stats.last_enemy_acquisition->tick),
                                static_cast<unsigned long long>(
                                    stats.last_enemy_acquisition->actor.value),
                                static_cast<unsigned long long>(
                                    stats.last_enemy_acquisition->sequence));
        }
        if (stats.last_enemy_attack) {
            ImGui::TextDisabled("last attack: tick %llu | actor %llu | %.0f damage | event #%llu",
                                static_cast<unsigned long long>(stats.last_enemy_attack->tick),
                                static_cast<unsigned long long>(
                                    stats.last_enemy_attack->actor.value),
                                stats.last_enemy_attack->damage,
                                static_cast<unsigned long long>(
                                     stats.last_enemy_attack->sequence));
        }
        }
        if (ImGui::CollapsingHeader("Health & events")) {
        ImGui::Text("fixed tick %llu | %llu hits | %llu deaths",
                    static_cast<unsigned long long>(stats.health.tick),
                    static_cast<unsigned long long>(stats.health.applied_hit_count),
                    static_cast<unsigned long long>(stats.health.death_count));
        ImGui::TextDisabled("duplicate hit identities rejected: %llu",
                            static_cast<unsigned long long>(
                                stats.health.rejected_duplicate_hit_count));
        const std::size_t living_targets = static_cast<std::size_t>(std::ranges::count_if(
            stats.health.targets,
            [](const HealthTargetSnapshot& target) { return target.alive; }));
        ImGui::Text("%zu targets | %zu alive | %zu retired",
                    stats.health.targets.size(), living_targets,
                    stats.health.targets.size() - living_targets);
        if (stats.health.targets.empty()) {
            ImGui::TextDisabled("No authored enemy health targets.");
        }
        if (!stats.health.targets.empty() &&
            ImGui::CollapsingHeader("Per-target health details")) {
          for (const HealthTargetSnapshot& target : stats.health.targets) {
            ImGui::TextColored(target.alive ? ImVec4{0.45F, 0.87F, 0.75F, 1.0F}
                                            : ImVec4{0.78F, 0.32F, 0.38F, 1.0F},
                               "target %llu | %.0f / %.0f | %s",
                               static_cast<unsigned long long>(target.target.value),
                               target.current_health, target.maximum_health,
                               target.alive ? "ALIVE" : "DEAD");
          }
        }
        if (stats.last_damage) {
            ImGui::Text("last damage: target %llu | %.0f applied | %.0f remaining",
                        static_cast<unsigned long long>(stats.last_damage->target.value),
                        stats.last_damage->applied_damage,
                        stats.last_damage->health_after);
        }
        if (stats.last_death) {
            ImGui::TextColored(ImVec4{0.78F, 0.32F, 0.38F, 1.0F},
                               "last death: target %llu | event #%llu",
                               static_cast<unsigned long long>(stats.last_death->target.value),
                               static_cast<unsigned long long>(stats.last_death->sequence));
        }
        if (stats.last_combat_intent) {
            ImGui::TextColored(ImVec4{0.97F, 0.76F, 0.35F, 1.0F},
                               "last: %s | event #%llu | observed %llu",
                               combat_intent_name(stats.last_combat_intent->intent).data(),
                               static_cast<unsigned long long>(stats.last_combat_intent->sequence),
                               static_cast<unsigned long long>(stats.observed_combat_intents));
        } else {
            ImGui::TextDisabled("last: none | no fixed-tick combat intent observed");
        }
        }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Scene")) {
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
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        ImGui::End();
    }

    void draw_debug_channels(DebugVisuals& visuals) {
        if (!ImGui::Begin("Debug channels")) {
            ImGui::End();
            return;
        }
        bool enabled = visuals.enabled();
        if (ImGui::Checkbox("Debug visuals", &enabled)) {
            visuals.set_enabled(enabled);
        }
        editor_ui::badge_same_line(enabled ? editor_ui::colors::positive
                                           : editor_ui::colors::text_muted,
                                   "F1");

        const float half =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
        if (editor_ui::tool_button("Clean viewport", false, half)) {
            visuals.set_enabled(true);
            for (std::size_t index = 0; index < debug_channel_count; ++index) {
                visuals.set_channel_selected(static_cast<DebugChannel>(index), false);
            }
            visuals.set_channel_selected(DebugChannel::stats_overlay, true);
        }
        ImGui::SameLine();
        if (editor_ui::tool_button("Hide all", false, half)) {
            visuals.set_enabled(false);
        }

        ImGui::BeginDisabled(!visuals.enabled());
        for (std::size_t index = 0; index < debug_channel_count; ++index) {
            const auto channel = static_cast<DebugChannel>(index);
            if (channel == DebugChannel::collision_shapes) {
                editor_ui::section_header("World");
            } else if (channel == DebugChannel::navigation_grid) {
                editor_ui::section_header("Navigation");
            } else if (channel == DebugChannel::stats_overlay) {
                editor_ui::section_header("Presentation");
            } else if (channel == DebugChannel::lights) {
                editor_ui::section_header("Reserved");
            }
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

    // Corner readout over the canvas. It is drawn after the image so it is
    // never covered, and it never intercepts the pointer.
    void draw_viewport_overlay(
        const EditorCanvas& canvas,
        const float scale,
        const bool paused
    ) {
        // The game's own debug overlay owns the top corners and the bottom
        // left of the canvas, so the panel readout takes the bottom right and
        // never lands on top of engine text.
        const ImVec2 canvas_max = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        std::array<char, 96> readout{};
        static_cast<void>(std::snprintf(readout.data(), readout.size(), "%d x %d   %.0f%%",
                                        canvas.width, canvas.height, scale * 100.0F));
        const ImVec2 pad{8.0F, 4.0F};
        const auto place_right = [&](const char* label, const ImU32 color, const float bottom) {
            const ImVec2 size = ImGui::CalcTextSize(label);
            const ImVec2 box_max{canvas_max.x - 10.0F, bottom};
            const ImVec2 box_min{box_max.x - size.x - pad.x * 2.0F,
                                 box_max.y - size.y - pad.y * 2.0F};
            draw->AddRectFilled(box_min, box_max, IM_COL32(0, 0, 0, 150), 3.0F);
            draw->AddText(ImVec2{box_min.x + pad.x, box_min.y + pad.y}, color, label);
            return box_min.y;
        };
        const float readout_top =
            place_right(readout.data(), editor_ui::colors::text_dim, canvas_max.y - 10.0F);
        if (paused) {
            place_right("PAUSED", editor_ui::colors::warning, readout_top - 6.0F);
        }
    }

    void draw_viewport(const EditorCanvas& canvas, const bool paused) {
        viewport_pointer.reset();
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
            // The canvas is a render target, so its rows arrive bottom-up. The
            // flip is a sampling detail: the panel rectangle still maps
            // top-left to canvas top-left.
            ImGui::Image(static_cast<ImTextureID>(canvas.texture_id), size,
                         ImVec2{0.0F, 1.0F}, ImVec2{1.0F, 0.0F});
            // A hairline frame separates the rendered canvas from the letterbox
            // so a dark scene does not appear to bleed into the panel.
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(),
                                                ImGui::GetItemRectMax(),
                                                editor_ui::colors::border);
            draw_viewport_overlay(canvas, scale, paused);
            if (ImGui::IsItemHovered()) {
                const ImVec2 image_origin = ImGui::GetItemRectMin();
                const ImVec2 pointer = ImGui::GetIO().MousePos;
                viewport_pointer = Vec2{(pointer.x - image_origin.x) / scale,
                                        (pointer.y - image_origin.y) / scale};
                if (ImGui::GetIO().KeyCtrl &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    pending_pick = true;
                    pending_pick_point = *viewport_pointer;
                }
            }
        }
        ImGui::End();
    }
};

EditorShell::EditorShell(std::filesystem::path layout_path)
    : impl_{std::make_unique<Impl>(layout_path)} {
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

EntityUuid EditorShell::selection() const noexcept { return impl_->selection; }

void EditorShell::select_entity(const EntityUuid uuid) noexcept {
    if (impl_->selection != uuid) {
        impl_->selection = uuid;
        impl_->reveal_selection = true;
    }
}

bool EditorShell::blocks_gameplay_input() const noexcept {
    return visible() && editor_blocks_gameplay_input(
                            impl_->backend.wants_text_input(), impl_->backend.item_active());
}

bool EditorShell::wants_mouse() const noexcept {
    return visible() && impl_->backend.wants_mouse();
}

std::optional<Vec2> EditorShell::viewport_pointer_canvas() const noexcept {
    return visible() ? impl_->viewport_pointer : std::nullopt;
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

    if (impl_->layout_restored && !impl_->layout_reported) {
        impl_->layout_reported = true;
        impl_->set_status("Workspace layout restored.");
        log(LogLevel::info, "Restored editor layout: " + impl_->layout_path.string());
    }

    impl_->record_frame_time();

    // The menu bar and the status bar claim their strips of the viewport work
    // area before the dockspace measures what is left for the panels.
    impl_->draw_menu_bar(scene_editor, debug_visuals, stats, actions);
    impl_->draw_status_bar(stats);

    const ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (!impl_->layout_built) {
        impl_->build_layout(dockspace);
        impl_->layout_built = true;
        impl_->save_layout();
        log(LogLevel::info, "Built default editor layout: " + impl_->layout_path.string());
    }

    impl_->refresh_cache(scene_editor);
    const std::vector<SceneDocumentEntity>& entities = impl_->cached_entities;
    impl_->refresh_selection_fields(entities);

    impl_->draw_toolbar(scene_editor, stats, actions);
    impl_->draw_hierarchy(entities);
    impl_->draw_inspector(scene_editor, entities);
    impl_->draw_history(scene_editor);
    impl_->draw_statistics(scene_editor, stats, actions);
    impl_->draw_debug_channels(debug_visuals);
    impl_->draw_viewport(canvas, stats.paused);
    impl_->reveal_selection = false;

    actions.viewport_picked = impl_->pending_pick;
    actions.viewport_pick_canvas_point = impl_->pending_pick_point;
    impl_->pending_pick = false;

    impl_->backend.render();
    return actions;
}

} // namespace ic2d
