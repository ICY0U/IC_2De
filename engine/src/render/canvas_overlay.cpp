#include "render/canvas_overlay.hpp"

#include <algorithm>
#include <cmath>

namespace ic2d {

// Projects a world position into canvas space, returning nothing when it falls
// outside the canvas. Used for the crosshair and the assist marker, which are
// both world positions that must not be drawn off-screen.
[[nodiscard]] std::optional<Vec2> canvas_position_of(const Vec3& world_position,
                                                     const int canvas_width,
                                                     const int canvas_height,
                                                     const Camera25DState& camera) noexcept {
    const ProjectedPoint25D projected = project_world_point(world_position, camera);
    const Vec2 canvas{
        projected.position.x + static_cast<float>(canvas_width) * 0.5F,
        projected.position.y + static_cast<float>(canvas_height) * 0.5F,
    };
    if (canvas.x < 0.0F || canvas.y < 0.0F || canvas.x >= static_cast<float>(canvas_width) ||
        canvas.y >= static_cast<float>(canvas_height)) {
        return std::nullopt;
    }
    return canvas;
}

// A ring on the actor the aim is being helped toward. Assist moves the shot,
// not the cursor, so the crosshair stays under the player's hand and this is
// what tells them the help is happening.
// The target the aim is being helped toward. Four corner brackets rather than a
// ring: brackets read as a deliberate lock at this pixel scale, where a thin
// circle turns into a jagged blob, and they leave the actor's silhouette
// unobscured. Drawn in canvas coordinates like the crosshair, because this is
// inside the scene render target.
void draw_assist_marker(const Vec2 canvas_position, const bool firing) {
    const int x = static_cast<int>(std::round(canvas_position.x));
    const int y = static_cast<int>(std::round(canvas_position.y));

    // Tightens on the target while firing, which is the whole readout: the
    // player sees the lock close rather than a static decoration.
    const int reach = firing ? 9 : 11;
    constexpr int arm = 4;
    constexpr int thickness = 1;
    constexpr Color shadow{13, 30, 29, 190};
    const Color bracket = firing ? Color{248, 194, 89, 255} : Color{117, 238, 211, 225};

    // Each corner is two strokes over a one-pixel shadow, so the mark stays
    // legible against both the bright and the dark parts of a sprite.
    for (int corner = 0; corner < 4; ++corner) {
        const int sx = (corner & 1) == 0 ? -1 : 1;
        const int sy = (corner & 2) == 0 ? -1 : 1;
        const int cx = x + sx * reach;
        const int cy = y + sy * reach;
        const int left = sx < 0 ? cx : cx - arm + 1;
        const int top = sy < 0 ? cy : cy - arm + 1;
        DrawRectangle(left, cy - (sy < 0 ? 0 : thickness - 1) + 1, arm, thickness, shadow);
        DrawRectangle(cx - (sx < 0 ? 0 : thickness - 1) + 1, top, thickness, arm, shadow);
        DrawRectangle(left, cy - (sy < 0 ? 0 : thickness - 1), arm, thickness, bracket);
        DrawRectangle(cx - (sx < 0 ? 0 : thickness - 1), top, thickness, arm, bracket);
    }
}

[[nodiscard]] std::optional<Vec2>
crosshair_canvas_position(const AimInput& input, const std::optional<Vec2> pointer_canvas,
                          const std::optional<Vec3> aim_point, const int canvas_width,
                          const int canvas_height, const Camera25DState& camera) noexcept {
    if (input.pointer_active) {
        return pointer_canvas;
    }
    if (!aim_point) {
        return std::nullopt;
    }

    const ProjectedPoint25D projected = project_world_point(*aim_point, camera);
    const Vec2 canvas{
        projected.position.x + static_cast<float>(canvas_width) * 0.5F,
        projected.position.y + static_cast<float>(canvas_height) * 0.5F,
    };
    if (canvas.x < 0.0F || canvas.y < 0.0F || canvas.x >= static_cast<float>(canvas_width) ||
        canvas.y >= static_cast<float>(canvas_height)) {
        return std::nullopt;
    }
    return canvas;
}

void draw_pixel_crosshair(const Vec2& canvas_position, const int canvas_width,
                          const int canvas_height, const bool firing) {
    const int x = std::clamp(static_cast<int>(std::round(canvas_position.x)), 0, canvas_width - 1);
    const int y = std::clamp(static_cast<int>(std::round(canvas_position.y)), 0, canvas_height - 1);
    constexpr Color outline{13, 30, 29, 230};
    constexpr Color thread_gold{248, 194, 89, 255};
    constexpr Color centre{117, 238, 211, 255};
    const int spread = firing ? 2 : 0;
    const Color active_centre = firing ? thread_gold : centre;

    DrawRectangle(x - 9 - spread, y - 2, 7, 3, outline);
    DrawRectangle(x + 3 + spread, y - 2, 7, 3, outline);
    DrawRectangle(x - 2, y - 9 - spread, 3, 7, outline);
    DrawRectangle(x - 2, y + 3 + spread, 3, 7, outline);
    DrawRectangle(x - 8 - spread, y - 1, 6, 1, thread_gold);
    DrawRectangle(x + 3 + spread, y - 1, 6, 1, thread_gold);
    DrawRectangle(x - 1, y - 8 - spread, 1, 6, thread_gold);
    DrawRectangle(x - 1, y + 3 + spread, 1, 6, thread_gold);
    DrawRectangle(x - 1, y - 1, 2, 2, active_centre);
}

// One authoritative simulation tick: move the character, let layers observe the
// results, then ease the camera toward the player.
// A prompt over the item the player can use. It is drawn in canvas space like
// the crosshair, so it scales with the presentation rather than the window.
void draw_interaction_prompt(const Vec2 canvas_position) {
    // Canvas coordinates, not screen: this is drawn inside the scene render
    // target, exactly like the crosshair.
    const int x = static_cast<int>(std::round(canvas_position.x));
    const int y = static_cast<int>(std::round(canvas_position.y));
    constexpr Color shell{8, 14, 18, 215};
    constexpr Color rim{117, 238, 211, 235};
    DrawCircle(x, y, 8.0F, shell);
    DrawCircleLines(x, y, 8.0F, rim);
    constexpr int font_size = 10;
    DrawText("E", x - MeasureText("E", font_size) / 2, y - font_size / 2, font_size, rim);
}

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS

[[nodiscard]] Vector2 canvas_point(const ProjectedPoint25D& projected,
                                   const ApplicationConfig& config) noexcept {
    return {
        static_cast<float>(config.canvas_width) * 0.5F + projected.position.x,
        static_cast<float>(config.canvas_height) * 0.5F + projected.position.y,
    };
}

// The picker and the selection outline mirror the renderer's destination
// rectangle exactly, so what the pointer hits is what the viewport shows.
[[nodiscard]] CanvasRect sprite_canvas_rect(const EntityBlueprint& entity,
                                            const Camera25DState& camera,
                                            const ApplicationConfig& config) {
    const Vector2 anchor =
        canvas_point(project_world_point(entity.transform.position, camera), config);
    const Sprite2D& sprite = *entity.sprite;
    const float width = std::abs(sprite.size.x * entity.transform.scale.x);
    const float height = std::abs(sprite.size.y * entity.transform.scale.y);
    return {
        anchor.x - sprite.normalized_origin.x * width,
        anchor.y - sprite.normalized_origin.y * height,
        width,
        height,
    };
}

void draw_selection_outline(const WorldSnapshot& snapshot, const Camera25DState& camera,
                            const ApplicationConfig& config, const EntityUuid selection) {
    for (const EntityBlueprint& entity : snapshot.entities) {
        if (entity.uuid != selection || !entity.sprite) {
            continue;
        }
        const CanvasRect rect = sprite_canvas_rect(entity, camera, config);
        DrawRectangleLinesEx(
            Rectangle{rect.left - 1.0F, rect.top - 1.0F, rect.width + 2.0F, rect.height + 2.0F},
            1.0F, Color{248, 194, 89, 235});
        return;
    }
}

// The viewport shows the running scene, but the gizmo edits the authored
// document, so a drag has nothing to move on screen until it is applied. The
// preview is what closes that gap: the selection's own outline, drawn again at
// the offset the drag has accumulated.
void draw_selection_drag_preview(const WorldSnapshot& snapshot, const Camera25DState& camera,
                                 const ApplicationConfig& config, const EntityUuid selection,
                                 const Vec2 canvas_offset) {
    for (const EntityBlueprint& entity : snapshot.entities) {
        if (entity.uuid != selection || !entity.sprite) {
            continue;
        }
        const CanvasRect rect = sprite_canvas_rect(entity, camera, config);
        DrawRectangleLinesEx(Rectangle{rect.left + canvas_offset.x, rect.top + canvas_offset.y,
                                       rect.width, rect.height},
                             1.0F, Color{117, 238, 211, 245});
        return;
    }
}

void draw_projected_ground_grid(const ApplicationConfig& config, const Camera25DState& camera,
                                const RectXZ& bounds) {
    constexpr float spacing = 64.0F;
    const float first_x = std::ceil(bounds.x / spacing) * spacing;
    const float first_z = std::ceil(bounds.z / spacing) * spacing;
    const float maximum_x = bounds.x + bounds.width;
    const float maximum_z = bounds.z + bounds.depth;
    const Color minor{58, 91, 88, 150};
    const Color major{77, 126, 116, 190};

    int index = 0;
    for (float x = first_x; x <= maximum_x; x += spacing, ++index) {
        const auto start = canvas_point(project_world_point({x, 0.0F, bounds.z}, camera), config);
        const auto end = canvas_point(project_world_point({x, 0.0F, maximum_z}, camera), config);
        DrawLineV(start, end, index % 4 == 0 ? major : minor);
    }

    index = 0;
    for (float z = first_z; z <= maximum_z; z += spacing, ++index) {
        const auto start = canvas_point(project_world_point({bounds.x, 0.0F, z}, camera), config);
        const auto end = canvas_point(project_world_point({maximum_x, 0.0F, z}, camera), config);
        DrawLineV(start, end, index % 4 == 0 ? major : minor);
    }
}
#endif

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
void draw_compact_hud(const ApplicationConfig& config, const RunState run_state,
                      const bool dropped_time, const std::string_view pacing_description) {
    DrawText("IC_2DE", 18, 16, 18, Color{74, 222, 190, 255});
    DrawText(TextFormat("%d FPS | %s", GetFPS(), pacing_description.data()),
             config.canvas_width - 218, 17, 10, LIME);
    DrawText("F2 TOOLS  |  F1 WORLD DEBUG", 18, config.canvas_height - 24, 9,
             Color{166, 178, 198, 225});

    // Only a paused run is dimmed and labelled. An editor sitting in edit mode
    // is showing the authored scene, which is the thing an author opened it to
    // look at; covering it with a banner would hide exactly that.
    if (run_state == RunState::paused) {
        DrawRectangle(0, 0, config.canvas_width, config.canvas_height, Fade(BLACK, 0.42F));
        DrawText("PAUSED", config.canvas_width / 2 - 43, config.canvas_height / 2 - 18, 22,
                 RAYWHITE);
        DrawText("Press O to advance one simulation tick", config.canvas_width / 2 - 124,
                 config.canvas_height / 2 + 12, 10, Color{248, 194, 89, 255});
    }

    if (dropped_time) {
        DrawText("FRAME STALL CLAMPED", config.canvas_width - 142, 36, 9, ORANGE);
    }
}

#endif

} // namespace ic2d
