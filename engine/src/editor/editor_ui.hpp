#pragma once

#include <cstddef>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <imgui.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// Presentation vocabulary for the development editor: one palette, one style
// pass, and the small set of composite widgets every panel reuses.
//
// The palette follows Hazel's editor identity (neutral graphite surfaces with a
// single amber accent) rather than a per-panel colour choice, so a new panel
// inherits the look instead of inventing one. Nothing here knows about scenes,
// documents, or statistics: these are drawing primitives only.
namespace ic2d::editor_ui {

// Named surfaces and text tones. Panels reference these instead of literal
// ImVec4 values so a theme change stays one edit.
namespace colors {

inline constexpr ImU32 accent = IM_COL32(236, 158, 36, 255);
inline constexpr ImU32 accent_muted = IM_COL32(236, 158, 36, 40);
inline constexpr ImU32 highlight = IM_COL32(58, 178, 233, 255);
inline constexpr ImU32 positive = IM_COL32(106, 200, 148, 255);
inline constexpr ImU32 warning = IM_COL32(233, 176, 84, 255);
inline constexpr ImU32 danger = IM_COL32(219, 94, 94, 255);

inline constexpr ImU32 titlebar = IM_COL32(21, 21, 23, 255);
inline constexpr ImU32 background_dark = IM_COL32(26, 26, 28, 255);
inline constexpr ImU32 background = IM_COL32(33, 33, 36, 255);
inline constexpr ImU32 surface = IM_COL32(40, 40, 44, 255);
inline constexpr ImU32 group_header = IM_COL32(47, 47, 51, 255);
inline constexpr ImU32 property_field = IM_COL32(18, 18, 20, 255);
inline constexpr ImU32 border = IM_COL32(58, 58, 63, 255);

inline constexpr ImU32 text = IM_COL32(202, 202, 206, 255);
inline constexpr ImU32 text_bright = IM_COL32(233, 233, 238, 255);
inline constexpr ImU32 text_dim = IM_COL32(134, 134, 141, 255);
inline constexpr ImU32 text_muted = IM_COL32(96, 96, 102, 255);

} // namespace colors

[[nodiscard]] ImVec4 to_vec4(ImU32 color) noexcept;

// Height of the toolbar and status strips, so the dock builder and the drawing
// code agree without a magic number in either.
[[nodiscard]] float toolbar_height() noexcept;
[[nodiscard]] float status_bar_height() noexcept;

// Loads the editor typefaces into the shared atlas. Must run after the Dear
// ImGui context exists and before the atlas is uploaded, which is why the
// backend calls it rather than the shell. Falls back to the built-in font when
// no system typeface is readable, so the editor still opens.
void configure_fonts();

// Body font is the ImGui default font. These are the additional roles; both may
// return the body font when only the fallback typeface exists.
[[nodiscard]] ImFont* bold_font() noexcept;
[[nodiscard]] ImFont* small_font() noexcept;

// Applies spacing, rounding, and the palette above to the active style.
void apply_theme();

// --- Text -----------------------------------------------------------------

void text_colored(ImU32 color, const char* fmt, ...) IM_FMTARGS(2);
void text_dim(const char* fmt, ...) IM_FMTARGS(1);
// Small uppercase label used to introduce a group inside a panel.
void section_header(const char* label);
// Filled rounded label. Used for states a reader should find without reading.
void badge(ImU32 color, const char* fmt, ...) IM_FMTARGS(2);
// Draws the same badge inline after the previous item.
void badge_same_line(ImU32 color, const char* fmt, ...) IM_FMTARGS(2);

// --- Property grid --------------------------------------------------------

// Two-column label/value grid. Every property panel uses one so labels and
// fields line up across unrelated sections.
[[nodiscard]] bool begin_property_grid(const char* id, float label_width = 92.0F);
void end_property_grid();
// Opens a row and writes the label, leaving the cursor in the value cell with
// the item width already stretched to it.
void property_label(const char* label, const char* tooltip = nullptr);
// Label plus a read-only formatted value.
void property_text(const char* label, const char* fmt, ...) IM_FMTARGS(2);
void property_text_colored(ImU32 color, const char* label, const char* fmt, ...) IM_FMTARGS(3);

// Editing a multi-component control is not one item, so callers that commit on
// release need the aggregate of all three fields rather than the last one.
struct Vec3ControlResult {
    bool changed{false};
    bool active{false};
    bool deactivated_after_edit{false};
};

// Hazel's axis control: a coloured X/Y/Z tab beside each component, where
// clicking the tab restores the reset value.
Vec3ControlResult vec3_control(
    const char* label,
    float values[3],
    float reset_value = 0.0F,
    float speed = 0.5F
);

// --- Chrome ---------------------------------------------------------------

// Wide flat button sized to the remaining row width when width is 0.
bool tool_button(const char* label, bool active = false, float width = 0.0F);
// Accent-filled primary action.
bool accent_button(const char* label, float width = 0.0F);

// Search field with placeholder text and a clear affordance. Returns true when
// the buffer changed this frame.
bool search_field(const char* id, char* buffer, std::size_t capacity, const char* hint);

} // namespace ic2d::editor_ui
