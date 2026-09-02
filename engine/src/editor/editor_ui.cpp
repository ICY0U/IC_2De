#include "editor/editor_ui.hpp"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace ic2d::editor_ui {
namespace {

ImFont* g_bold_font = nullptr;
ImFont* g_small_font = nullptr;

constexpr float body_font_size = 16.0F;
constexpr float small_font_size = 13.0F;

struct AxisStyle {
    const char* name;
    ImU32 normal;
    ImU32 hovered;
    ImU32 active;
};

// Desaturated relative to Hazel's primary red/green/blue so three saturated
// tabs never dominate a dense inspector.
constexpr std::array<AxisStyle, 3> axis_styles{{
    {"X", IM_COL32(186, 84, 84, 255), IM_COL32(210, 104, 104, 255), IM_COL32(166, 70, 70, 255)},
    {"Y", IM_COL32(110, 168, 100, 255), IM_COL32(130, 190, 118, 255), IM_COL32(94, 148, 86, 255)},
    {"Z", IM_COL32(82, 126, 196, 255), IM_COL32(102, 148, 218, 255), IM_COL32(70, 110, 176, 255)},
}};

[[nodiscard]] constexpr ImU32 with_alpha(const ImU32 color, const unsigned int alpha) noexcept {
    return (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

[[nodiscard]] std::filesystem::path windows_font_directory() {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "WINDIR") != 0 || value == nullptr || length <= 1U) {
        std::free(value);
        return {};
    }
    const std::filesystem::path root{value};
    std::free(value);
    return root / "Fonts";
#else
    const char* value = std::getenv("WINDIR");
    return value != nullptr && *value != '\0' ? std::filesystem::path{value} / "Fonts"
                                              : std::filesystem::path{};
#endif
}

[[nodiscard]] ImFont* load_face(
    const std::filesystem::path& directory,
    const char* file_name,
    const float size
) {
    if (directory.empty()) {
        return nullptr;
    }
    const std::filesystem::path face = directory / file_name;
    std::error_code error;
    if (!std::filesystem::is_regular_file(face, error) || error) {
        return nullptr;
    }
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    config.PixelSnapH = false;
    const std::string bytes = face.string();
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(bytes.c_str(), size, &config);
}

// A docked panel can be narrower than a label and a field placed side by side.
// Below that width the grid stacks the value under its label instead of letting
// both shrink into unreadable stubs, so the same panel stays usable at any dock
// size. The mode is decided once per grid and remembered for its rows.
constexpr float stacked_layout_width = 250.0F;
bool g_grid_is_stacked = false;

// Opens the row and leaves the cursor where the value belongs. The item width
// is the caller's choice, because axis controls subdivide the space.
void begin_property_row(const char* label, const char* tooltip) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    if (!g_grid_is_stacked) {
        ImGui::AlignTextToFramePadding();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, colors::text_dim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (tooltip != nullptr && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
    if (!g_grid_is_stacked) {
        ImGui::TableSetColumnIndex(1);
    }
}

void draw_badge(const ImU32 color, const char* label) {
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    const ImVec2 padding{6.0F, 2.0F};
    const ImVec2 size{text_size.x + padding.x * 2.0F, text_size.y + padding.y * 2.0F};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 corner{origin.x + size.x, origin.y + size.y};
    draw->AddRectFilled(origin, corner, with_alpha(color, 38U), 3.0F);
    draw->AddRect(origin, corner, with_alpha(color, 110U), 3.0F);
    draw->AddText(ImVec2{origin.x + padding.x, origin.y + padding.y}, color, label);
}

void format_into(std::array<char, 512>& buffer, const char* fmt, va_list args) {
    const int written = std::vsnprintf(buffer.data(), buffer.size(), fmt, args);
    if (written < 0) {
        buffer[0] = '\0';
    }
}

} // namespace

ImVec4 to_vec4(const ImU32 color) noexcept { return ImGui::ColorConvertU32ToFloat4(color); }

float toolbar_height() noexcept { return 38.0F; }

float status_bar_height() noexcept {
    return ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y;
}

void configure_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    const std::filesystem::path fonts = windows_font_directory();

    // Segoe UI is the platform UI face on every machine this editor builds for.
    // Loading it costs nothing at runtime and is what separates the shell from
    // a default-ImGui prototype look.
    ImFont* body = load_face(fonts, "segoeui.ttf", body_font_size);
    if (body == nullptr) {
        body = io.Fonts->AddFontDefault();
    }
    io.FontDefault = body;

    g_bold_font = load_face(fonts, "seguisb.ttf", body_font_size);
    if (g_bold_font == nullptr) {
        g_bold_font = load_face(fonts, "segoeuib.ttf", body_font_size);
    }
    if (g_bold_font == nullptr) {
        g_bold_font = body;
    }

    g_small_font = load_face(fonts, "segoeui.ttf", small_font_size);
    if (g_small_font == nullptr) {
        g_small_font = body;
    }
}

ImFont* bold_font() noexcept {
    return g_bold_font != nullptr ? g_bold_font : ImGui::GetIO().FontDefault;
}

ImFont* small_font() noexcept {
    return g_small_font != nullptr ? g_small_font : ImGui::GetIO().FontDefault;
}

void apply_theme() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowPadding = ImVec2{10.0F, 10.0F};
    style.FramePadding = ImVec2{8.0F, 5.0F};
    style.CellPadding = ImVec2{7.0F, 4.0F};
    style.ItemSpacing = ImVec2{8.0F, 6.0F};
    style.ItemInnerSpacing = ImVec2{6.0F, 5.0F};
    style.IndentSpacing = 20.0F;
    style.ScrollbarSize = 12.0F;
    style.GrabMinSize = 10.0F;

    // Square windows and tabs read as panels; only the interactive controls are
    // rounded. Mixing both is what makes an ImGui shell look improvised.
    style.WindowRounding = 0.0F;
    style.ChildRounding = 3.0F;
    style.FrameRounding = 3.0F;
    style.PopupRounding = 4.0F;
    style.GrabRounding = 3.0F;
    style.TabRounding = 0.0F;
    style.ScrollbarRounding = 6.0F;

    style.WindowBorderSize = 0.0F;
    style.ChildBorderSize = 1.0F;
    style.PopupBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    style.TabBarBorderSize = 1.0F;
    style.DockingSeparatorSize = 1.0F;
    style.SeparatorTextBorderSize = 1.0F;
    style.SeparatorTextPadding = ImVec2{14.0F, 5.0F};
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.WindowTitleAlign = ImVec2{0.0F, 0.5F};

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = to_vec4(colors::text);
    c[ImGuiCol_TextDisabled] = to_vec4(colors::text_muted);
    c[ImGuiCol_TextSelectedBg] = to_vec4(with_alpha(colors::accent, 70U));

    c[ImGuiCol_WindowBg] = to_vec4(colors::background);
    c[ImGuiCol_ChildBg] = to_vec4(colors::background_dark);
    c[ImGuiCol_PopupBg] = to_vec4(colors::surface);
    c[ImGuiCol_MenuBarBg] = to_vec4(colors::titlebar);
    c[ImGuiCol_Border] = to_vec4(colors::border);
    c[ImGuiCol_BorderShadow] = ImVec4{0.0F, 0.0F, 0.0F, 0.0F};

    c[ImGuiCol_FrameBg] = to_vec4(colors::property_field);
    c[ImGuiCol_FrameBgHovered] = to_vec4(IM_COL32(28, 28, 31, 255));
    c[ImGuiCol_FrameBgActive] = to_vec4(IM_COL32(34, 34, 38, 255));

    c[ImGuiCol_TitleBg] = to_vec4(colors::titlebar);
    c[ImGuiCol_TitleBgActive] = to_vec4(colors::titlebar);
    c[ImGuiCol_TitleBgCollapsed] = to_vec4(colors::titlebar);

    c[ImGuiCol_Button] = to_vec4(colors::group_header);
    c[ImGuiCol_ButtonHovered] = to_vec4(IM_COL32(62, 62, 68, 255));
    c[ImGuiCol_ButtonActive] = to_vec4(IM_COL32(74, 74, 81, 255));

    c[ImGuiCol_Header] = to_vec4(with_alpha(colors::accent, 34U));
    c[ImGuiCol_HeaderHovered] = to_vec4(with_alpha(colors::accent, 58U));
    c[ImGuiCol_HeaderActive] = to_vec4(with_alpha(colors::accent, 82U));

    c[ImGuiCol_Separator] = to_vec4(colors::border);
    c[ImGuiCol_SeparatorHovered] = to_vec4(with_alpha(colors::accent, 140U));
    c[ImGuiCol_SeparatorActive] = to_vec4(colors::accent);

    c[ImGuiCol_ResizeGrip] = ImVec4{0.0F, 0.0F, 0.0F, 0.0F};
    c[ImGuiCol_ResizeGripHovered] = to_vec4(with_alpha(colors::accent, 110U));
    c[ImGuiCol_ResizeGripActive] = to_vec4(colors::accent);

    c[ImGuiCol_Tab] = to_vec4(colors::background_dark);
    c[ImGuiCol_TabHovered] = to_vec4(colors::group_header);
    c[ImGuiCol_TabSelected] = to_vec4(colors::background);
    c[ImGuiCol_TabSelectedOverline] = to_vec4(colors::accent);
    c[ImGuiCol_TabDimmed] = to_vec4(colors::titlebar);
    c[ImGuiCol_TabDimmedSelected] = to_vec4(colors::background_dark);
    c[ImGuiCol_TabDimmedSelectedOverline] = to_vec4(with_alpha(colors::accent, 90U));

    c[ImGuiCol_ScrollbarBg] = ImVec4{0.0F, 0.0F, 0.0F, 0.0F};
    c[ImGuiCol_ScrollbarGrab] = to_vec4(IM_COL32(56, 56, 61, 255));
    c[ImGuiCol_ScrollbarGrabHovered] = to_vec4(IM_COL32(72, 72, 78, 255));
    c[ImGuiCol_ScrollbarGrabActive] = to_vec4(with_alpha(colors::accent, 190U));

    c[ImGuiCol_CheckMark] = to_vec4(colors::accent);
    c[ImGuiCol_SliderGrab] = to_vec4(IM_COL32(120, 120, 128, 255));
    c[ImGuiCol_SliderGrabActive] = to_vec4(colors::accent);

    c[ImGuiCol_DockingPreview] = to_vec4(with_alpha(colors::accent, 130U));
    c[ImGuiCol_DockingEmptyBg] = to_vec4(colors::background_dark);

    c[ImGuiCol_TableHeaderBg] = to_vec4(colors::group_header);
    c[ImGuiCol_TableBorderStrong] = to_vec4(colors::border);
    c[ImGuiCol_TableBorderLight] = to_vec4(IM_COL32(44, 44, 48, 255));
    c[ImGuiCol_TableRowBg] = ImVec4{0.0F, 0.0F, 0.0F, 0.0F};
    c[ImGuiCol_TableRowBgAlt] = to_vec4(IM_COL32(255, 255, 255, 6));

    c[ImGuiCol_NavCursor] = to_vec4(colors::accent);
    c[ImGuiCol_DragDropTarget] = to_vec4(colors::accent);
    c[ImGuiCol_PlotLines] = to_vec4(colors::highlight);
    c[ImGuiCol_PlotHistogram] = to_vec4(colors::accent);
}

void text_colored(const ImU32 color, const char* fmt, ...) {
    std::array<char, 512> buffer{};
    va_list args;
    va_start(args, fmt);
    format_into(buffer, fmt, args);
    va_end(args);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(buffer.data());
    ImGui::PopStyleColor();
}

void text_dim(const char* fmt, ...) {
    std::array<char, 512> buffer{};
    va_list args;
    va_start(args, fmt);
    format_into(buffer, fmt, args);
    va_end(args);
    ImGui::PushStyleColor(ImGuiCol_Text, colors::text_muted);
    ImGui::TextUnformatted(buffer.data());
    ImGui::PopStyleColor();
}

void section_header(const char* label) {
    ImGui::PushFont(bold_font());
    ImGui::PushStyleColor(ImGuiCol_Text, colors::text_dim);
    ImGui::SeparatorText(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void badge(const ImU32 color, const char* fmt, ...) {
    std::array<char, 512> buffer{};
    va_list args;
    va_start(args, fmt);
    format_into(buffer, fmt, args);
    va_end(args);
    draw_badge(color, buffer.data());
}

void badge_same_line(const ImU32 color, const char* fmt, ...) {
    std::array<char, 512> buffer{};
    va_list args;
    va_start(args, fmt);
    format_into(buffer, fmt, args);
    va_end(args);
    ImGui::SameLine();
    draw_badge(color, buffer.data());
}

bool begin_property_grid(const char* id, const float label_width) {
    // No sizing flag, so the table keeps the stretch policy: the label column
    // holds its fixed width and the value column takes exactly what is left.
    // A fixed-fit table sizes the value column to its widest text instead, and
    // then a long value pushes the fields out of a narrow docked panel.
    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings;
    g_grid_is_stacked = ImGui::GetContentRegionAvail().x < stacked_layout_width;
    if (!ImGui::BeginTable(id, g_grid_is_stacked ? 1 : 2, flags)) {
        return false;
    }
    if (g_grid_is_stacked) {
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, label_width);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void end_property_grid() {
    ImGui::EndTable();
    g_grid_is_stacked = false;
}

void property_label(const char* label, const char* tooltip) {
    begin_property_row(label, tooltip);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void property_text(const char* label, const char* fmt, ...) {
    std::array<char, 512> buffer{};
    va_list args;
    va_start(args, fmt);
    format_into(buffer, fmt, args);
    va_end(args);
    begin_property_row(label, nullptr);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(buffer.data());
}

void property_text_colored(const ImU32 color, const char* label, const char* fmt, ...) {
    std::array<char, 512> buffer{};
    va_list args;
    va_start(args, fmt);
    format_into(buffer, fmt, args);
    va_end(args);
    begin_property_row(label, nullptr);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(buffer.data());
    ImGui::PopStyleColor();
}

Vec3ControlResult vec3_control(
    const char* label,
    float values[3],
    const float reset_value,
    const float speed
) {
    Vec3ControlResult result{};
    ImGui::PushID(label);
    begin_property_row(label, nullptr);

    const ImGuiStyle& style = ImGui::GetStyle();
    const float inner = style.ItemInnerSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    const float tab_width = ImGui::GetFrameHeight() * 0.42F;
    const float group_width = (available - style.ItemSpacing.x * 2.0F) / 3.0F;
    const float field_width = group_width - tab_width - inner;

    for (std::size_t axis = 0; axis < axis_styles.size(); ++axis) {
        const AxisStyle& look = axis_styles[axis];
        ImGui::PushID(static_cast<int>(axis));

        // The coloured tab is the reset affordance, exactly as in Hazel, but it
        // is a slim edge marker rather than a full-width labelled button.
        ImGui::PushStyleColor(ImGuiCol_Button, look.normal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, look.hovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, look.active);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0F);
        if (ImGui::Button("##axis", ImVec2{tab_width, ImGui::GetFrameHeight()})) {
            values[axis] = reset_value;
            result.changed = true;
            // A reset is a complete edit, so callers that commit on release
            // commit it in the same frame.
            result.deactivated_after_edit = true;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s - click to reset to %.3g", look.name, reset_value);
        }

        ImGui::SameLine(0.0F, inner);
        ImGui::SetNextItemWidth(field_width);
        const std::string field_label = std::string{"##"} + look.name;
        if (ImGui::DragFloat(field_label.c_str(), &values[axis], speed, 0.0F, 0.0F, "%.2f")) {
            result.changed = true;
        }
        result.active = result.active || ImGui::IsItemActive();
        result.deactivated_after_edit =
            result.deactivated_after_edit || ImGui::IsItemDeactivatedAfterEdit();
        ImGui::PopID();
        if (axis + 1 < axis_styles.size()) {
            ImGui::SameLine(0.0F, style.ItemSpacing.x);
        }
    }
    ImGui::PopID();
    return result;
}

bool tool_button(const char* label, const bool active, const float width) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, with_alpha(colors::accent, 46U));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, with_alpha(colors::accent, 70U));
        ImGui::PushStyleColor(ImGuiCol_Text, colors::accent);
    }
    const bool pressed = ImGui::Button(label, ImVec2{width, 0.0F});
    if (active) {
        ImGui::PopStyleColor(3);
    }
    return pressed;
}

bool accent_button(const char* label, const float width) {
    ImGui::PushStyleColor(ImGuiCol_Button, with_alpha(colors::accent, 210U));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors::accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, with_alpha(colors::accent, 170U));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(24, 20, 12, 255));
    ImGui::PushFont(bold_font());
    const bool pressed = ImGui::Button(label, ImVec2{width, 0.0F});
    ImGui::PopFont();
    ImGui::PopStyleColor(4);
    return pressed;
}

bool search_field(
    const char* id,
    char* buffer,
    const std::size_t capacity,
    const char* hint
) {
    ImGui::PushID(id);
    const bool has_text = buffer[0] != '\0';
    const float clear_width =
        has_text ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x : 0.0F;
    ImGui::SetNextItemWidth(has_text ? -clear_width : -FLT_MIN);
    const bool changed = ImGui::InputTextWithHint("##field", hint, buffer, capacity);
    if (has_text) {
        ImGui::SameLine(0.0F, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::Button("x", ImVec2{ImGui::GetFrameHeight(), ImGui::GetFrameHeight()})) {
            buffer[0] = '\0';
            ImGui::PopID();
            return true;
        }
    }
    ImGui::PopID();
    return changed;
}

} // namespace ic2d::editor_ui
