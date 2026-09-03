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
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <unordered_map>
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

// Which part of the translate gizmo a drag is constrained to.
enum class GizmoHandle { none, axis_x, axis_z, plane };

[[nodiscard]] ImVec2 add(const ImVec2 left, const ImVec2 right) noexcept {
    return {left.x + right.x, left.y + right.y};
}

[[nodiscard]] ImVec2 scaled(const ImVec2 value, const float factor) noexcept {
    return {value.x * factor, value.y * factor};
}

[[nodiscard]] ImVec2 normalized(const ImVec2 value) noexcept {
    const float length = std::sqrt(value.x * value.x + value.y * value.y);
    return length > 0.0001F ? ImVec2{value.x / length, value.y / length} : ImVec2{0.0F, 0.0F};
}

[[nodiscard]] float distance_between(const ImVec2 left, const ImVec2 right) noexcept {
    const ImVec2 offset{left.x - right.x, left.y - right.y};
    return std::sqrt(offset.x * offset.x + offset.y * offset.y);
}

// Distance from a point to a segment, used to hit-test the axis shafts. A
// segment test rather than a bounding box keeps the two axes separable when
// the camera yaw brings them close together on screen.
[[nodiscard]] float distance_to_segment(
    const ImVec2 point,
    const ImVec2 from,
    const ImVec2 to
) noexcept {
    const ImVec2 span{to.x - from.x, to.y - from.y};
    const float length_squared = span.x * span.x + span.y * span.y;
    float t = 0.0F;
    if (length_squared > 0.0001F) {
        t = ((point.x - from.x) * span.x + (point.y - from.y) * span.y) / length_squared;
        t = std::clamp(t, 0.0F, 1.0F);
    }
    const ImVec2 closest{from.x + span.x * t, from.y + span.y * t};
    const ImVec2 offset{point.x - closest.x, point.y - closest.y};
    return std::sqrt(offset.x * offset.x + offset.y * offset.y);
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
    const bool /*item_active*/,
    const bool gizmo_active
) noexcept {
    // Mouse interaction is routed separately using the actual gameplay
    // viewport rectangle. Treating every active ImGui item as keyboard capture
    // made a left-click on the viewport erase held WASD for the next frame.
    //
    // A gizmo drag is the exception: it is a left-press inside the viewport
    // that must not also fire the weapon, so while one is held the editor owns
    // the frame's input outright.
    return wants_text_input || gizmo_active;
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

    bool camera_panning{false};
    bool detached_view{false};

    // The window gesture in progress and the strip that starts one. Both are
    // inert unless the application asked the shell to supply window chrome.
    EditorWindowDrag window_drag{EditorWindowDrag::none};
    ImVec2 menu_bar_min{};
    ImVec2 menu_bar_max{};

    // The gizmo reports its drag as a total offset from where it began, so a
    // dropped frame cannot accumulate error and the application can commit one
    // move command from the final value.
    GizmoHandle gizmo_handle{GizmoHandle::none};
    bool gizmo_dragging{false};
    Vec2 gizmo_start_canvas{};

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

    // Sprite edits are buffered like the name and position: the widget owns the
    // value while it is being dragged, and one command is committed on release
    // so a drag becomes a single undo step rather than one per frame.
    SceneDocumentSprite sprite_field{};
    TextField texture_field{};
    bool sprite_field_active{false};

    TextField new_instance_id{};
    TextField new_instance_name{};
    std::array<float, 3> new_instance_position{};
    int selected_prefab{0};

    // The create-entity dialog keeps its own draft so a half-filled form
    // survives clicking elsewhere in the editor.
    TextField new_entity_id{};
    TextField new_entity_name{};
    TextField new_entity_texture{};
    std::array<float, 3> new_entity_position{};
    std::array<float, 2> new_entity_size{32.0F, 32.0F};
    std::array<float, 4> new_entity_tint{1.0F, 1.0F, 1.0F, 1.0F};
    float new_entity_depth_span{0.0F};
    bool open_create_entity{false};

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
            sprite_field_active = false;
        }
        if (!name_field_active) {
            assign(name_field, found->name);
        }
        if (!position_field_active) {
            position_field = {found->position.x, found->position.y, found->position.z};
        }
        if (!sprite_field_active) {
            sprite_field = found->sprite;
            assign(texture_field, found->sprite.texture_id);
        }
    }

    // The window controls the undecorated window no longer has. Drawn rather
    // than typed: the editor font carries no box-drawing or multiplication
    // glyphs, and a caption button has to read the same at every DPI.
    enum class CaptionGlyph { minimize, maximize, restore, close };

    [[nodiscard]] static bool caption_button(
        const char* id,
        const CaptionGlyph glyph,
        const ImU32 accent
    ) {
        const float height = ImGui::GetFrameHeight();
        const ImVec2 size{46.0F, height};
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton(id, size);
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList& draw = *ImGui::GetWindowDrawList();
        if (hovered) {
            // The close button lights up in its own colour because it is the
            // one press in the bar that cannot be undone.
            const ImU32 wash = glyph == CaptionGlyph::close
                                   ? IM_COL32(196, 62, 62, 255)
                                   : IM_COL32(58, 58, 63, 255);
            draw.AddRectFilled(origin, add(origin, size), wash);
        }
        const ImU32 mark = hovered ? editor_ui::colors::text_bright : accent;
        const ImVec2 centre{origin.x + size.x * 0.5F, origin.y + size.y * 0.5F};
        constexpr float arm = 5.0F;
        constexpr float thickness = 1.3F;
        switch (glyph) {
        case CaptionGlyph::minimize:
            draw.AddLine({centre.x - arm, centre.y}, {centre.x + arm, centre.y}, mark,
                         thickness);
            break;
        case CaptionGlyph::maximize:
            draw.AddRect({centre.x - arm, centre.y - arm}, {centre.x + arm, centre.y + arm},
                         mark, 0.0F, 0, thickness);
            break;
        case CaptionGlyph::restore:
            // Two offset outlines, the way every desktop draws "put it back".
            draw.AddRect({centre.x - arm, centre.y - arm + 2.0F},
                         {centre.x + arm - 2.0F, centre.y + arm}, mark, 0.0F, 0, thickness);
            draw.AddRect({centre.x - arm + 2.0F, centre.y - arm},
                         {centre.x + arm, centre.y + arm - 2.0F}, mark, 0.0F, 0, thickness);
            break;
        case CaptionGlyph::close:
            draw.AddLine({centre.x - arm, centre.y - arm}, {centre.x + arm, centre.y + arm},
                         mark, thickness);
            draw.AddLine({centre.x + arm, centre.y - arm}, {centre.x - arm, centre.y + arm},
                         mark, thickness);
            break;
        }
        return pressed;
    }

    void draw_caption_buttons(const EditorStats& stats, EditorActions& actions) {
        constexpr float buttons_width = 46.0F * 3.0F;
        // Menu bar items are laid out with spacing between them; the caption
        // group has none, so it is positioned as one block at the far right.
        ImGui::SameLine(ImGui::GetWindowWidth() - buttons_width);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0F, 0.0F});
        if (caption_button("##minimize", CaptionGlyph::minimize,
                           editor_ui::colors::highlight)) {
            actions.window.minimize = true;
        }
        ImGui::SameLine();
        if (caption_button("##maximize",
                           stats.window_maximized ? CaptionGlyph::restore
                                                  : CaptionGlyph::maximize,
                           editor_ui::colors::positive)) {
            actions.window.toggle_maximize = true;
        }
        ImGui::SameLine();
        if (caption_button("##close", CaptionGlyph::close, editor_ui::colors::danger)) {
            actions.window.close = true;
        }
        ImGui::PopStyleVar();
    }

    // Which edge band the pointer is in, or none. Corners win over edges, so a
    // corner drag sizes both axes rather than whichever edge was tested first.
    [[nodiscard]] static EditorWindowDrag resize_zone_at(const ImVec2 point) {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        constexpr float band = 6.0F;
        const bool left = point.x <= band;
        const bool right = point.x >= display.x - band;
        const bool top = point.y <= band;
        const bool bottom = point.y >= display.y - band;
        if (top && left) {
            return EditorWindowDrag::resize_top_left;
        }
        if (top && right) {
            return EditorWindowDrag::resize_top_right;
        }
        if (bottom && left) {
            return EditorWindowDrag::resize_bottom_left;
        }
        if (bottom && right) {
            return EditorWindowDrag::resize_bottom_right;
        }
        if (left) {
            return EditorWindowDrag::resize_left;
        }
        if (right) {
            return EditorWindowDrag::resize_right;
        }
        if (top) {
            return EditorWindowDrag::resize_top;
        }
        if (bottom) {
            return EditorWindowDrag::resize_bottom;
        }
        return EditorWindowDrag::none;
    }

    static void apply_drag_cursor(const EditorWindowDrag zone) {
        switch (zone) {
        case EditorWindowDrag::move:
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            break;
        case EditorWindowDrag::resize_left:
        case EditorWindowDrag::resize_right:
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            break;
        case EditorWindowDrag::resize_top:
        case EditorWindowDrag::resize_bottom:
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            break;
        case EditorWindowDrag::resize_top_left:
        case EditorWindowDrag::resize_bottom_right:
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
            break;
        case EditorWindowDrag::resize_top_right:
        case EditorWindowDrag::resize_bottom_left:
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
            break;
        case EditorWindowDrag::none:
            break;
        }
    }

    // Moving and sizing a window the OS no longer decorates. A hold begun on
    // the chrome is followed to its release wherever the pointer travels, or a
    // fast drag would be dropped the moment it left a six-pixel band.
    void update_window_chrome(const EditorStats& stats, EditorActions& actions) {
        if (!stats.custom_window_chrome) {
            window_drag = EditorWindowDrag::none;
            return;
        }
        if (window_drag != EditorWindowDrag::none) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                actions.window.drag = window_drag;
                apply_drag_cursor(window_drag);
            } else {
                window_drag = EditorWindowDrag::none;
            }
            return;
        }
        // A maximized window has no edges to pull and nowhere to be moved to.
        if (stats.window_maximized) {
            return;
        }
        // Anything ImGui is already using the pointer for wins: a dock splitter
        // dragged to the screen edge must not become a window resize.
        if (ImGui::IsAnyItemActive() || ImGui::IsAnyItemHovered() ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                            ImGuiPopupFlags_AnyPopupLevel)) {
            return;
        }
        const ImVec2 pointer = ImGui::GetIO().MousePos;
        const EditorWindowDrag zone = resize_zone_at(pointer);
        if (zone != EditorWindowDrag::none) {
            apply_drag_cursor(zone);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                window_drag = zone;
                actions.window.drag = zone;
                actions.window.drag_started = true;
            }
            return;
        }
        if (!ImGui::IsMouseHoveringRect(menu_bar_min, menu_bar_max, false)) {
            return;
        }
        // Double-click on the bar is the gesture people try before they look
        // for the button.
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            actions.window.toggle_maximize = true;
            return;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            window_drag = EditorWindowDrag::move;
            actions.window.drag = EditorWindowDrag::move;
            actions.window.drag_started = true;
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
            menu_bar_min = {};
            menu_bar_max = {};
            return;
        }
        // Recorded for the drag hit-test, which runs after the bar has closed
        // and so can no longer ask ImGui where it was.
        menu_bar_min = ImGui::GetWindowPos();
        menu_bar_max = add(menu_bar_min, ImGui::GetWindowSize());
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
            ImGui::Separator();
            if (ImGui::MenuItem("Frame selection", "F", false, selection != EntityUuid{})) {
                actions.camera_frame_selection = true;
            }
            if (ImGui::MenuItem("Follow player", nullptr, false, stats.camera_detached)) {
                actions.camera_follow_player = true;
                set_status("View reattached to the gameplay camera.");
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
        if (stats.custom_window_chrome) {
            draw_caption_buttons(stats, actions);
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

        // Three named states rather than one toggle, so the transport reads as
        // the state it is in: Play is the offer while the scene is still, and
        // Restart is the way back to the authored scene from either of the
        // other two.
        const bool running = simulating(stats.run_state);
        constexpr float transport_button_width = 92.0F;
        const float transport_width =
            transport_button_width * 3.0F + ImGui::GetStyle().ItemSpacing.x * 2.0F;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                      (ImGui::GetWindowWidth() - transport_width) * 0.5F));
        ImGui::BeginDisabled(running);
        const bool play_pressed = running
                                      ? editor_ui::tool_button("Play", false,
                                                               transport_button_width)
                                      : editor_ui::accent_button("Play", transport_button_width);
        ImGui::EndDisabled();
        if (play_pressed) {
            actions.set_run_state = EditorRunState::running;
            set_status(stats.run_state == EditorRunState::editing ? "Playing the scene."
                                                                  : "Resumed.");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!running);
        const bool pause_pressed =
            editor_ui::tool_button("Pause", false, transport_button_width);
        ImGui::EndDisabled();
        if (pause_pressed) {
            actions.set_run_state = EditorRunState::paused;
            set_status("Paused.");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(stats.run_state == EditorRunState::editing);
        const bool restart_pressed =
            editor_ui::tool_button("Restart", false, transport_button_width);
        ImGui::EndDisabled();
        if (restart_pressed) {
            actions.reset_running_scene = true;
            set_status("Restored the authored scene.");
        }

        if (stats.camera_detached) {
            ImGui::SameLine();
            if (editor_ui::tool_button("Follow player", true)) {
                actions.camera_follow_player = true;
                set_status("View reattached to the gameplay camera.");
            }
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

    // The placement tree, resolved once per draw. Roots keep authored order and
    // so do children, so the panel reads in the order the scene file does
    // however deeply it nests.
    struct HierarchyTree {
        std::vector<std::size_t> roots;
        std::vector<std::vector<std::size_t>> children;
        // Whether the row or anything under it matches the search box. A
        // parent whose child matches has to survive the filter, or the match
        // would have nothing to hang from.
        std::vector<bool> matches;
    };

    [[nodiscard]] HierarchyTree build_hierarchy(
        const std::vector<SceneDocumentEntity>& entities
    ) const {
        HierarchyTree tree;
        tree.children.resize(entities.size());
        tree.matches.assign(entities.size(), false);
        std::unordered_map<std::uint64_t, std::size_t> by_uuid;
        for (std::size_t index = 0; index < entities.size(); ++index) {
            by_uuid.emplace(entities[index].uuid.value, index);
        }
        for (std::size_t index = 0; index < entities.size(); ++index) {
            const auto parent = entities[index].parent
                                    ? by_uuid.find(entities[index].parent.value)
                                    : by_uuid.end();
            // A parent the document cannot resolve reads as no parent, so a
            // half-finished edit still lists every placement exactly once.
            if (parent == by_uuid.end() || parent->second == index) {
                tree.roots.push_back(index);
            } else {
                tree.children[parent->second].push_back(index);
            }
        }
        // A child is authored after its parent, so one pass from the end marks
        // every ancestor of a match.
        for (std::size_t offset = entities.size(); offset > 0; --offset) {
            const std::size_t index = offset - 1;
            const std::string searchable = entity_search_text(entities[index]);
            bool matched = hierarchy_filter.PassFilter(
                searchable.c_str(), searchable.c_str() + searchable.size());
            for (const std::size_t child : tree.children[index]) {
                matched = matched || tree.matches[child];
            }
            tree.matches[index] = matched;
        }
        return tree;
    }

    void draw_hierarchy_node(
        const std::vector<SceneDocumentEntity>& entities,
        const HierarchyTree& tree,
        const std::size_t index,
        std::size_t& shown
    ) {
        if (!tree.matches[index]) {
            return;
        }
        const SceneDocumentEntity& entity = entities[index];
        ++shown;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        push_entity_id(entity.uuid);
        const bool selected = selection == entity.uuid;
        const bool leaf = tree.children[index].empty();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns |
                                   ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                   ImGuiTreeNodeFlags_DefaultOpen;
        if (selected) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        if (leaf) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        const bool open = ImGui::TreeNodeEx(entity.name.c_str(), flags);
        // The arrow owns its own click, so collapsing a parent must not also
        // select it. Everything else on the row selects, and clicking the
        // selected row again lets go of it: the same gesture the viewport
        // answers to, so a selection is always undone the way it was made.
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            selection = selected ? EntityUuid{} : entity.uuid;
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
        if (open && !leaf) {
            for (const std::size_t child : tree.children[index]) {
                draw_hierarchy_node(entities, tree, child, shown);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
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
        const HierarchyTree tree = build_hierarchy(entities);
        const float footer = ImGui::GetTextLineHeightWithSpacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
        if (ImGui::BeginChild("##placements", ImVec2{0.0F, -footer},
                              ImGuiChildFlags_None)) {
            constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg |
                                                    ImGuiTableFlags_NoSavedSettings |
                                                    ImGuiTableFlags_PadOuterX;
            // Nesting costs horizontal space in a panel that is already
            // narrow, and a truncated name is worth less than the indent that
            // ate it, so a step is only wide enough to read as one.
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0F);
            if (ImGui::BeginTable("##placement_rows", 2, table_flags)) {
                ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("tag", ImGuiTableColumnFlags_WidthFixed, 46.0F);
                for (const std::size_t root : tree.roots) {
                    draw_hierarchy_node(entities, tree, root, shown);
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        if (editor_ui::tool_button("Create entity", false, -FLT_MIN)) {
            open_create_entity = true;
        }
        if (shown == entities.size()) {
            editor_ui::text_dim("%zu placements", entities.size());
        } else {
            editor_ui::text_dim("%zu of %zu placements", shown, entities.size());
        }
        ImGui::End();
    }

    void draw_inspector(
        SceneEditor& editor,
        const std::vector<SceneDocumentEntity>& entities,
        const EditorStats& stats,
        EditorActions& actions
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
            // Named rather than shown as an identity: the parent is a
            // placement the reader can find in the Hierarchy, and a raw UUID
            // would not help them find it.
            const auto parent = std::ranges::find(entities, found->parent,
                                                  &SceneDocumentEntity::uuid);
            editor_ui::property_text("Parent", "%s",
                                     found->parent && parent != entities.end()
                                         ? parent->name.c_str()
                                         : "None");
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

        if (found->has_own_sprite) {
            draw_sprite_section(editor, found->uuid);
        } else {
            editor_ui::section_header("Sprite");
            editor_ui::text_dim("Drawn from prefab \"%s\".", found->prefab_id.c_str());
        }

        draw_runtime_section(stats, actions, found->uuid);

        editor_ui::section_header(found->prefab_id.empty() ? "Placement" : "Instance");
        ImGui::PushStyleColor(ImGuiCol_Button, editor_ui::to_vec4(IM_COL32(96, 42, 42, 255)));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              editor_ui::to_vec4(IM_COL32(132, 56, 56, 255)));
        if (found->prefab_id.empty()) {
            if (ImGui::Button("Delete entity", ImVec2{-FLT_MIN, 0.0F})) {
                destroy_entity(editor, found->uuid);
            }
        } else if (ImGui::Button("Destroy instance", ImVec2{-FLT_MIN, 0.0F})) {
            destroy_instance(editor, found->uuid);
        }
        ImGui::PopStyleColor(2);
        editor_ui::text_dim("Records that still reference it will block removal.");

        ImGui::Spacing();
        draw_prefab_creation(editor);
        ImGui::End();
    }

    // What the running scene makes of a placement. A .scene document records
    // where things stand; only the run knows which of them is the player, which
    // are attackers, and what is left of them. The section is drawn from the
    // read-only snapshots the shell already receives, so inspecting an actor
    // cannot disturb the simulation.
    struct RuntimeActorView {
        bool is_player{false};
        bool is_enemy{false};
        bool has_health{false};
        bool alive{false};
        float health{0.0F};
        float maximum_health{0.0F};
    };

    [[nodiscard]] static RuntimeActorView runtime_actor_view(
        const EditorStats& stats,
        const EntityUuid uuid
    ) {
        RuntimeActorView view;
        view.is_player = uuid && uuid == stats.player_uuid;
        view.is_enemy = std::ranges::any_of(
            stats.enemy_intent.actors,
            [uuid](const EnemyActorIntentSnapshot& actor) { return actor.actor == uuid; });
        const auto health = std::ranges::find(
            stats.health.targets, uuid, &HealthTargetSnapshot::target);
        if (health != stats.health.targets.end()) {
            view.has_health = true;
            view.alive = health->alive;
            view.health = health->current_health;
            view.maximum_health = health->maximum_health;
        }
        return view;
    }

    // One checkbox that reports the override it wants rather than writing it.
    // The application owns every override, so the shell can neither disagree
    // with the run nor keep a stale copy of it.
    static void override_checkbox(
        EditorActions& actions,
        const EditorStats& stats,
        const EntityUuid actor,
        const ActorDebugFlag flag,
        const char* label,
        const char* tooltip
    ) {
        bool held = stats.actor_debug.enabled(actor, flag);
        if (ImGui::Checkbox(label, &held)) {
            actions.actor_debug_requests.push_back({
                .actor = actor,
                .flag = flag,
                .enabled = held,
            });
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
    }

    void draw_runtime_section(
        const EditorStats& stats,
        EditorActions& actions,
        const EntityUuid uuid
    ) {
        const RuntimeActorView view = runtime_actor_view(stats, uuid);
        // Scenery, walls and markers get nothing. An inspector that offers
        // "Kill" on a crate is the clutter this section is trying to avoid.
        if (!view.is_player && !view.is_enemy && !view.has_health) {
            return;
        }

        editor_ui::section_header("Debug");
        if (editor_ui::begin_property_grid("##runtime_properties")) {
            editor_ui::property_label("Role");
            editor_ui::text_colored(
                view.is_player ? editor_ui::colors::highlight : editor_ui::colors::accent,
                "%s", view.is_player ? "player" : view.is_enemy ? "attacker" : "actor");
            if (view.has_health) {
                editor_ui::property_label("Health");
                if (view.alive) {
                    editor_ui::text_colored(
                        view.health <= view.maximum_health * 0.34F
                            ? editor_ui::colors::danger
                            : editor_ui::colors::positive,
                        "%.0f / %.0f", static_cast<double>(view.health),
                        static_cast<double>(view.maximum_health));
                } else {
                    editor_ui::text_colored(editor_ui::colors::text_muted, "dead");
                }
            }
            editor_ui::end_property_grid();
        }

        if (view.is_enemy) {
            override_checkbox(actions, stats, uuid, ActorDebugFlag::frozen,
                              "Freeze in place",
                              "Held where it stands: no pursuit, no attacks. "
                              "Still solid, still shootable, still alive.");
        }
        if (view.has_health) {
            override_checkbox(actions, stats, uuid, ActorDebugFlag::invulnerable,
                              "Invulnerable",
                              "Damage aimed at this actor is discarded before it "
                              "reaches health, so the run carries on around it.");
        }
        if (view.is_player) {
            override_checkbox(actions, stats, uuid, ActorDebugFlag::infinite_ammo,
                              "Infinite ammo",
                              "The magazine is refilled every tick, so firing "
                              "costs nothing and no reload is ever needed.");
        }

        ImGui::BeginDisabled(!view.alive);
        ImGui::PushStyleColor(ImGuiCol_Button, editor_ui::to_vec4(IM_COL32(96, 42, 42, 255)));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              editor_ui::to_vec4(IM_COL32(132, 56, 56, 255)));
        if (ImGui::Button("Kill actor", ImVec2{-FLT_MIN, 0.0F})) {
            actions.kill_actor = uuid;
            set_status(simulating(stats.run_state)
                           ? "Killed the selected actor."
                           : "Kill queued for the next simulated tick.");
        }
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        if (!view.alive && view.has_health) {
            editor_ui::text_dim("Restart the scene to bring it back.");
        }
    }

    // Every widget reports whether it is still being held and whether it just
    // finished, so the section commits exactly once per completed interaction.
    struct FieldActivity {
        bool active{false};
        bool finished{false};

        void observe() {
            active = active || ImGui::IsItemActive();
            finished = finished || ImGui::IsItemDeactivatedAfterEdit();
        }
    };

    void draw_sprite_section(SceneEditor& editor, const EntityUuid uuid) {
        editor_ui::section_header("Sprite");
        if (!editor_ui::begin_property_grid("##sprite_properties")) {
            return;
        }
        FieldActivity activity;

        editor_ui::property_label("Size");
        std::array<float, 2> size{sprite_field.size.x, sprite_field.size.y};
        if (ImGui::DragFloat2("##size", size.data(), 0.5F, 1.0F, 8192.0F, "%.1f")) {
            sprite_field.size = {size[0], size[1]};
        }
        activity.observe();

        editor_ui::property_label("Origin", "Normalized: 0.5, 1 anchors the sprite's feet.");
        std::array<float, 2> origin{sprite_field.normalized_origin.x,
                                    sprite_field.normalized_origin.y};
        if (ImGui::DragFloat2("##origin", origin.data(), 0.01F, -2.0F, 2.0F, "%.2f")) {
            sprite_field.normalized_origin = {origin[0], origin[1]};
        }
        activity.observe();

        editor_ui::property_label("Tint");
        std::array<float, 4> tint{
            static_cast<float>(sprite_field.tint.red) / 255.0F,
            static_cast<float>(sprite_field.tint.green) / 255.0F,
            static_cast<float>(sprite_field.tint.blue) / 255.0F,
            static_cast<float>(sprite_field.tint.alpha) / 255.0F,
        };
        // Four numeric fields do not fit a docked inspector, so the row is the
        // swatch alone and the picker popup carries the precision.
        constexpr ImGuiColorEditFlags tint_flags = ImGuiColorEditFlags_AlphaBar |
                                                   ImGuiColorEditFlags_AlphaPreviewHalf |
                                                   ImGuiColorEditFlags_NoInputs;
        if (ImGui::ColorEdit4("##tint", tint.data(), tint_flags)) {
            sprite_field.tint = {
                static_cast<std::uint8_t>(std::lround(tint[0] * 255.0F)),
                static_cast<std::uint8_t>(std::lround(tint[1] * 255.0F)),
                static_cast<std::uint8_t>(std::lround(tint[2] * 255.0F)),
                static_cast<std::uint8_t>(std::lround(tint[3] * 255.0F)),
            };
        }
        activity.observe();

        editor_ui::property_label("Layer", "Higher layers draw over lower ones at equal depth.");
        ImGui::DragInt("##layer", &sprite_field.layer, 0.2F, -1024, 1024);
        activity.observe();

        editor_ui::property_label("Texture", "Empty draws a flat quad in the tint.");
        ImGui::InputTextWithHint("##texture", "untextured", texture_field.data(),
                                 texture_field.size());
        activity.observe();

        editor_ui::property_label(
            "Depth span",
            "World units this sprite runs along depth. Zero is a flat billboard;\n"
            "a positive span is a wall the renderer slices so actors sort against it.");
        ImGui::DragFloat("##depth_span", &sprite_field.depth_span, 1.0F, 0.0F, 100000.0F,
                         "%.1f");
        activity.observe();

        editor_ui::end_property_grid();

        sprite_field_active = activity.active;
        if (activity.finished) {
            sprite_field.texture_id = texture_field.data();
            apply_sprite(editor, uuid);
        }
        if (sprite_field.depth_span > 0.0F) {
            editor_ui::text_dim("Rendered as a depth-sliced surface.");
        }
    }

    void apply_sprite(SceneEditor& editor, const EntityUuid uuid) {
        try {
            if (editor.set_entity_sprite(uuid, sprite_field)) {
                set_status("Edited sprite.");
            } else {
                set_status("Sprite target no longer exists.");
            }
        } catch (const std::exception& error) {
            set_status(std::string{"Sprite edit rejected: "} + error.what());
        }
        sprite_field_active = false;
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

    // A modal keeps the draft in one place and makes the identity fields
    // explicit: an entity id is a cross-reference other records use, so it is
    // chosen deliberately rather than generated.
    void draw_create_entity_dialog(SceneEditor& editor) {
        constexpr const char* title = "Create entity";
        if (open_create_entity) {
            ImGui::OpenPopup(title);
            open_create_entity = false;
        }
        const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2{0.5F, 0.5F});
        ImGui::SetNextWindowSize(ImVec2{420.0F, 0.0F}, ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            return;
        }

        if (editor_ui::begin_property_grid("##create_entity")) {
            editor_ui::property_label("Id", "Cross-reference other records use. Must be unique.");
            ImGui::InputText("##entity_id", new_entity_id.data(), new_entity_id.size());
            editor_ui::property_label("Name");
            ImGui::InputText("##entity_name", new_entity_name.data(), new_entity_name.size());
            static_cast<void>(editor_ui::vec3_control("Position", new_entity_position.data()));
            editor_ui::property_label("Size");
            ImGui::DragFloat2("##entity_size", new_entity_size.data(), 0.5F, 1.0F, 8192.0F,
                              "%.1f");
            editor_ui::property_label("Tint");
            ImGui::ColorEdit4("##entity_tint", new_entity_tint.data(),
                              ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
            editor_ui::property_label("Texture", "Empty draws a flat quad in the tint.");
            ImGui::InputTextWithHint("##entity_texture", "untextured",
                                     new_entity_texture.data(), new_entity_texture.size());
            editor_ui::property_label(
                "Depth span",
                "World units along depth. Positive makes this a wall the renderer slices.");
            ImGui::DragFloat("##entity_span", &new_entity_depth_span, 1.0F, 0.0F, 100000.0F,
                             "%.1f");
            editor_ui::end_property_grid();
        }

        ImGui::Spacing();
        const bool named = new_entity_id[0] != '\0' && new_entity_name[0] != '\0';
        const float half =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
        // Accent means "this is the action to take". A form that cannot be
        // submitted yet must not wear it.
        ImGui::BeginDisabled(!named);
        const bool create = named ? editor_ui::accent_button("Create", half)
                                  : editor_ui::tool_button("Create", false, half);
        ImGui::EndDisabled();
        if (!named) {
            ImGui::SetItemTooltip("An id and a name are required.");
        }
        ImGui::SameLine();
        if (editor_ui::tool_button("Cancel", false, half)) {
            ImGui::CloseCurrentPopup();
        }
        if (create) {
            create_entity(editor);
        }
        ImGui::EndPopup();
    }

    void create_entity(SceneEditor& editor) {
        const auto channel = [](const float value) {
            return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
        };
        const SceneEntityPlacement placement{
            .id = new_entity_id.data(),
            .name = new_entity_name.data(),
            .position = {new_entity_position[0], new_entity_position[1],
                         new_entity_position[2]},
            .sprite = {
                .size = {new_entity_size[0], new_entity_size[1]},
                .normalized_origin = {0.5F, 1.0F},
                .tint = {channel(new_entity_tint[0]), channel(new_entity_tint[1]),
                         channel(new_entity_tint[2]), channel(new_entity_tint[3])},
                .layer = 0,
                .texture_id = new_entity_texture.data(),
                .depth_span = new_entity_depth_span,
            },
        };
        try {
            const EntityUuid created = editor.create_entity(placement);
            if (created) {
                selection = created;
                reveal_selection = true;
                set_status("Created entity " + placement.id + ".");
                assign(new_entity_id, {});
                assign(new_entity_name, {});
                ImGui::CloseCurrentPopup();
            } else {
                set_status("Entity could not be created.");
            }
        } catch (const std::exception& error) {
            set_status(std::string{"Create rejected: "} + error.what());
        }
    }

    void destroy_entity(SceneEditor& editor, const EntityUuid uuid) {
        try {
            if (editor.destroy_entity(uuid)) {
                selection = {};
                set_status("Destroyed entity.");
            } else {
                set_status("Entity no longer exists.");
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
        if (ImGui::CollapsingHeader("Aim", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (editor_ui::begin_property_grid("##aim_stats")) {
                editor_ui::property_text_colored(
                    stats.aim.aiming ? editor_ui::colors::positive
                                     : editor_ui::colors::text_muted,
                    "State", "%s%s",
                    stats.aim.aiming ? "AIMING" : "idle",
                    stats.aim.turning ? "  |  turning" : "");
                editor_ui::property_text("Source",
                                         stats.aim.pointer_source ? "pointer" : "stick");
                editor_ui::property_text("Direction", "X %.3f  Z %.3f",
                                         stats.aim.direction.x, stats.aim.direction.y);
                editor_ui::property_text("Range", "%.1f", stats.aim.distance);
                editor_ui::property_text("Muzzle", "X %.1f  Y %.1f  Z %.1f",
                                         stats.aim.origin.x, stats.aim.origin.y,
                                         stats.aim.origin.z);
                editor_ui::property_text("Aim point", "X %.1f  Z %.1f",
                                         stats.aim.aim_point.x, stats.aim.aim_point.z);
                if (stats.aim.assisted_target) {
                    editor_ui::property_text_colored(
                        editor_ui::colors::accent, "Assist", "target %llu",
                        static_cast<unsigned long long>(stats.aim.assisted_target->value));
                } else {
                    editor_ui::property_text_colored(editor_ui::colors::text_muted, "Assist",
                                                     "no candidate in the cone");
                }
                editor_ui::end_property_grid();
            }
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
        if (ImGui::CollapsingHeader("Interaction", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (editor_ui::begin_property_grid("##interaction_stats")) {
                editor_ui::property_text("Fixed tick", "%llu",
                                         static_cast<unsigned long long>(
                                             stats.interaction.tick));
                if (stats.interaction.candidate) {
                    editor_ui::property_text_colored(
                        editor_ui::colors::positive, "In range", "%s  |  %.0f  |  %.1f away",
                        std::string{interaction_kind_name(stats.interaction.candidate->kind)}
                            .c_str(),
                        stats.interaction.candidate->amount,
                        stats.interaction.candidate->distance);
                    editor_ui::property_text("Item", "%llu",
                                             static_cast<unsigned long long>(
                                                 stats.interaction.candidate->entity.value));
                } else {
                    editor_ui::property_text_colored(editor_ui::colors::text_muted, "In range",
                                                     "nothing usable");
                }
                editor_ui::property_text("Reachable", "%zu",
                                         stats.interaction.available_count);
                editor_ui::property_text("Remaining", "%zu", stats.interaction.remaining_count);
                editor_ui::property_text("Used", "%llu",
                                         static_cast<unsigned long long>(
                                             stats.interaction.performed_count));
                editor_ui::end_property_grid();
            }
            editor_ui::text_dim("Press E while a prompt is shown.");
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
        if (ImGui::Button(simulating(stats.run_state) ? "Pause" : "Play")) {
            actions.set_run_state = simulating(stats.run_state) ? EditorRunState::paused
                                                                : EditorRunState::running;
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
        const EditorRunState run_state,
        const bool detached
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
        float next_top =
            place_right(readout.data(), editor_ui::colors::text_dim, canvas_max.y - 10.0F);
        // Editing is the ordinary state of an open editor, so it is stated
        // quietly rather than warned about: a still scene is what an author
        // asked for, where a paused run is a run waiting to be let go.
        if (run_state == EditorRunState::editing) {
            next_top = place_right("EDITING", editor_ui::colors::text_dim, next_top - 6.0F);
        } else if (run_state == EditorRunState::paused) {
            next_top = place_right("PAUSED", editor_ui::colors::warning, next_top - 6.0F);
        }
        if (detached) {
            place_right("FREE VIEW  -  F frames, MMB pans, wheel zooms",
                        editor_ui::colors::accent, next_top - 6.0F);
        }
    }

    // Draws the translate gizmo over the canvas and turns a drag on one of its
    // handles into a canvas offset. The panel deliberately knows nothing about
    // the world: the application supplies the anchor and the two axis
    // directions, and receives pixels back.
    void draw_translate_gizmo(
        const EditorStats& stats,
        const float scale,
        EditorActions& actions
    ) {
        if (!stats.selection_canvas_point) {
            gizmo_dragging = false;
            gizmo_handle = GizmoHandle::none;
            return;
        }

        const ImVec2 image_origin = ImGui::GetItemRectMin();
        const auto to_screen = [&](const Vec2 canvas_point) {
            return ImVec2{image_origin.x + canvas_point.x * scale,
                          image_origin.y + canvas_point.y * scale};
        };
        const ImVec2 centre = to_screen(*stats.selection_canvas_point);
        const ImVec2 axis_x =
            normalized({stats.selection_axis_x_canvas.x, stats.selection_axis_x_canvas.y});
        const ImVec2 axis_z =
            normalized({stats.selection_axis_z_canvas.x, stats.selection_axis_z_canvas.y});

        constexpr float shaft_length = 52.0F;
        constexpr float shaft_pick_radius = 7.0F;
        constexpr float plane_radius = 9.0F;
        const ImVec2 x_end = add(centre, scaled(axis_x, shaft_length));
        const ImVec2 z_end = add(centre, scaled(axis_z, shaft_length));

        const ImVec2 pointer = ImGui::GetIO().MousePos;
        const bool viewport_hovered = ImGui::IsWindowHovered();
        GizmoHandle hovered = GizmoHandle::none;
        if (!gizmo_dragging && viewport_hovered && stats.selection_movable) {
            if (distance_between(pointer, centre) <= plane_radius) {
                hovered = GizmoHandle::plane;
            } else if (distance_to_segment(pointer, centre, x_end) <= shaft_pick_radius) {
                hovered = GizmoHandle::axis_x;
            } else if (distance_to_segment(pointer, centre, z_end) <= shaft_pick_radius) {
                hovered = GizmoHandle::axis_z;
            }
        }

        if (hovered != GizmoHandle::none && viewport_pointer &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            gizmo_dragging = true;
            gizmo_handle = hovered;
            gizmo_start_canvas = *viewport_pointer;
        }
        if (gizmo_dragging) {
            const bool released = !ImGui::IsMouseDown(ImGuiMouseButton_Left);
            Vec2 offset{};
            if (viewport_pointer) {
                offset = {viewport_pointer->x - gizmo_start_canvas.x,
                          viewport_pointer->y - gizmo_start_canvas.y};
            }
            // An axis drag keeps only the component along that axis, so the
            // placement slides on one world axis however the pointer wanders.
            if (gizmo_handle == GizmoHandle::axis_x || gizmo_handle == GizmoHandle::axis_z) {
                const ImVec2 axis = gizmo_handle == GizmoHandle::axis_x ? axis_x : axis_z;
                const float along = offset.x * axis.x + offset.y * axis.y;
                offset = {axis.x * along, axis.y * along};
            }
            actions.gizmo_drag = EditorActions::GizmoDrag{
                .canvas_offset = offset,
                .finished = released,
                .snap = ImGui::GetIO().KeyCtrl,
            };
            if (released) {
                gizmo_dragging = false;
                gizmo_handle = GizmoHandle::none;
            }
        }

        const GizmoHandle lit = gizmo_dragging ? gizmo_handle : hovered;
        const ImU32 x_color = lit == GizmoHandle::axis_x ? editor_ui::colors::accent
                                                         : IM_COL32(214, 96, 96, 235);
        const ImU32 z_color = lit == GizmoHandle::axis_z ? editor_ui::colors::accent
                                                         : IM_COL32(96, 140, 224, 235);
        const ImU32 plane_color = lit == GizmoHandle::plane
                                      ? editor_ui::colors::accent
                                      : IM_COL32(226, 226, 232, 220);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (!stats.selection_movable) {
            // A bound placement still gets a marker, because the reader needs
            // to see what is selected and why it cannot be dragged.
            draw->AddCircle(centre, plane_radius, IM_COL32(150, 150, 158, 200), 0, 1.5F);
            return;
        }

        const auto arrow = [&](const ImVec2 end, const ImVec2 axis, const ImU32 color) {
            draw->AddLine(centre, end, color, 2.0F);
            const ImVec2 side{-axis.y, axis.x};
            const ImVec2 tip = add(end, scaled(axis, 9.0F));
            draw->AddTriangleFilled(tip, add(end, scaled(side, 4.5F)),
                                    add(end, scaled(side, -4.5F)), color);
        };
        arrow(x_end, axis_x, x_color);
        arrow(z_end, axis_z, z_color);
        draw->AddCircleFilled(centre, plane_radius * 0.55F, plane_color);
        draw->AddCircle(centre, plane_radius, plane_color, 0, 1.5F);
    }

    void draw_viewport(
        const EditorCanvas& canvas,
        const EditorStats& stats,
        EditorActions& actions
    ) {
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
            const ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsItemHovered()) {
                const ImVec2 image_origin = ImGui::GetItemRectMin();
                const ImVec2 pointer = io.MousePos;
                viewport_pointer = Vec2{(pointer.x - image_origin.x) / scale,
                                        (pointer.y - image_origin.y) / scale};
                if (io.KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    pending_pick = true;
                    pending_pick_point = *viewport_pointer;
                }
                // The wheel is a gameplay action, so it only drives the view
                // while the pointer is over the panel that owns the view.
                if (io.MouseWheel != 0.0F) {
                    actions.camera_zoom_notches += io.MouseWheel;
                }
            }
            // Middle-drag pans. The drag is reported in screen pixels and the
            // camera works in canvas pixels, so it is divided by the same
            // scale the image was drawn at.
            if (ImGui::IsItemHovered() || camera_panning) {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                    camera_panning = true;
                    actions.camera_pan_canvas.x += io.MouseDelta.x / scale;
                    actions.camera_pan_canvas.y += io.MouseDelta.y / scale;
                } else {
                    camera_panning = false;
                }
            }
            if (ImGui::IsWindowHovered() && ImGui::IsKeyPressed(ImGuiKey_F, false) &&
                !backend.wants_text_input()) {
                actions.camera_frame_selection = true;
            }
            draw_translate_gizmo(stats, scale, actions);
            draw_viewport_overlay(canvas, scale, stats.run_state, detached_view);
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
    return visible() && editor_blocks_gameplay_input(impl_->backend.wants_text_input(),
                                                     impl_->backend.item_active(),
                                                     impl_->gizmo_dragging);
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
    // After the bar, so a press that landed on a menu or a caption button has
    // already claimed the pointer and cannot also start a window drag.
    impl_->update_window_chrome(stats, actions);
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
    impl_->draw_inspector(scene_editor, entities, stats, actions);
    impl_->draw_history(scene_editor);
    impl_->draw_statistics(scene_editor, stats, actions);
    impl_->draw_debug_channels(debug_visuals);
    impl_->draw_create_entity_dialog(scene_editor);
    impl_->detached_view = stats.camera_detached;
    impl_->draw_viewport(canvas, stats, actions);
    impl_->reveal_selection = false;

    actions.viewport_picked = impl_->pending_pick;
    actions.viewport_pick_canvas_point = impl_->pending_pick_point;
    impl_->pending_pick = false;

    impl_->backend.render();
    return actions;
}

} // namespace ic2d
