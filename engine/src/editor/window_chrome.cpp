#include "editor/window_chrome.hpp"

#include <algorithm>
#include <cmath>

namespace ic2d {

[[nodiscard]] Vector2 desktop_pointer() noexcept {
    const Vector2 window = GetWindowPosition();
    const Vector2 pointer = GetMousePosition();
    return {window.x + pointer.x, window.y + pointer.y};
}

void apply_window_chrome(const EditorWindowActions& request, WindowChromeDrag& drag) {
    if (request.minimize) {
        MinimizeWindow();
    }
    if (request.toggle_maximize) {
        if (IsWindowMaximized()) {
            RestoreWindow();
        } else {
            MaximizeWindow();
        }
    }
    if (request.drag == EditorWindowDrag::none) {
        drag.gesture = EditorWindowDrag::none;
        return;
    }
    if (request.drag_started || drag.gesture != request.drag) {
        drag = {
            .gesture = request.drag,
            .pointer = desktop_pointer(),
            .position = GetWindowPosition(),
            .size = {static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())},
        };
        return;
    }

    const Vector2 pointer = desktop_pointer();
    const float moved_x = pointer.x - drag.pointer.x;
    const float moved_y = pointer.y - drag.pointer.y;
    if (request.drag == EditorWindowDrag::move) {
        SetWindowPosition(static_cast<int>(std::lround(drag.position.x + moved_x)),
                          static_cast<int>(std::lround(drag.position.y + moved_y)));
        return;
    }

    const bool west = request.drag == EditorWindowDrag::resize_left ||
                      request.drag == EditorWindowDrag::resize_top_left ||
                      request.drag == EditorWindowDrag::resize_bottom_left;
    const bool east = request.drag == EditorWindowDrag::resize_right ||
                      request.drag == EditorWindowDrag::resize_top_right ||
                      request.drag == EditorWindowDrag::resize_bottom_right;
    const bool north = request.drag == EditorWindowDrag::resize_top ||
                       request.drag == EditorWindowDrag::resize_top_left ||
                       request.drag == EditorWindowDrag::resize_top_right;
    const bool south = request.drag == EditorWindowDrag::resize_bottom ||
                       request.drag == EditorWindowDrag::resize_bottom_left ||
                       request.drag == EditorWindowDrag::resize_bottom_right;

    float x = drag.position.x;
    float y = drag.position.y;
    float width = drag.size.x;
    float height = drag.size.y;
    if (east) {
        width = std::max(minimum_window_width, drag.size.x + moved_x);
    } else if (west) {
        // Clamped on the width, then the origin is derived from it, so a
        // window pulled past its minimum stops growing instead of walking its
        // left edge across the desktop.
        width = std::max(minimum_window_width, drag.size.x - moved_x);
        x = drag.position.x + (drag.size.x - width);
    }
    if (south) {
        height = std::max(minimum_window_height, drag.size.y + moved_y);
    } else if (north) {
        height = std::max(minimum_window_height, drag.size.y - moved_y);
        y = drag.position.y + (drag.size.y - height);
    }
    SetWindowSize(static_cast<int>(std::lround(width)), static_cast<int>(std::lround(height)));
    SetWindowPosition(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
}

} // namespace ic2d
