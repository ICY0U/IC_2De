#pragma once

#include "ic2d/editor.hpp"

#include <raylib.h>

namespace ic2d {

// Moving and sizing a window the platform no longer decorates.
//
// The editor draws its own title bar and borders, so the platform stops doing
// it and something has to take over. The shell hit-tests and names the gesture;
// this owns the window and is the only thing that may move or size it.
//
// The anchor is what makes a drag stable. Resolving it against the previous
// frame would feed the window's own movement back into the next delta, so a
// window under a still pointer would drift; anchoring the pointer and the rect
// once, at the press, means a still pointer always resolves to the rect it
// started from.
struct WindowChromeDrag {
    EditorWindowDrag gesture{EditorWindowDrag::none};
    Vector2 pointer{};
    Vector2 position{};
    Vector2 size{};
};

// Small enough to tuck a window away, large enough that the docked layout still
// has somewhere to put every panel.
inline constexpr float minimum_window_width = 720.0F;
inline constexpr float minimum_window_height = 480.0F;

// The pointer in desktop coordinates rather than window ones, which is the only
// frame of reference that stays still while the window it is dragging moves.
[[nodiscard]] Vector2 desktop_pointer() noexcept;

// Applies one frame of the shell's window request: minimise, maximise, move or
// resize from any edge or corner.
void apply_window_chrome(const EditorWindowActions& request, WindowChromeDrag& drag);

} // namespace ic2d
