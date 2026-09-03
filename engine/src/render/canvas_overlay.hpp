#pragma once

#include "ic2d/application.hpp"
#include "ic2d/input.hpp"
#include "ic2d/projection25d.hpp"
#include "ic2d/run_state.hpp"
#include "ic2d/types.hpp"
#include "ic2d/world.hpp"

#include <optional>
#include <string_view>

#include <raylib.h>

#ifndef IC2DE_ENABLE_DEVELOPMENT_TOOLS
#define IC2DE_ENABLE_DEVELOPMENT_TOOLS 1
#endif

namespace ic2d {

// Canvas-space geometry and the overlays drawn in it.
//
// Everything here draws into the 640x360 virtual canvas rather than the window,
// which is why it takes canvas dimensions rather than screen ones and why the
// crosshair lines up with the world at any window size. It was part of
// application.cpp, where the drawing sat between the frame loop and the
// simulation it draws.

// Projects a world position into canvas space, returning nothing when it falls
// outside the canvas. Used for the crosshair and the assist marker, which are
// both world positions that must not be drawn off-screen.
[[nodiscard]] std::optional<Vec2> canvas_position_of(const Vec3& world_position, int canvas_width,
                                                     int canvas_height,
                                                     const Camera25DState& camera) noexcept;

// Where the crosshair belongs this frame: under the pointer when the pointer is
// aiming, or at the projected stick target when the controller is.
[[nodiscard]] std::optional<Vec2> crosshair_canvas_position(const AimInput& input,
                                                            std::optional<Vec2> pointer_canvas,
                                                            std::optional<Vec3> aim_point,
                                                            int canvas_width, int canvas_height,
                                                            const Camera25DState& camera) noexcept;

void draw_assist_marker(Vec2 canvas_position, bool firing);

void draw_pixel_crosshair(const Vec2& canvas_position, int canvas_width, int canvas_height,
                          bool firing);

void draw_interaction_prompt(Vec2 canvas_position);

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS

// A projected point in canvas pixels, which is what the editor overlays and the
// picking code both work in.
[[nodiscard]] Vector2 canvas_point(const ProjectedPoint25D& projected,
                                   const ApplicationConfig& config) noexcept;

struct CanvasRect {
    float left{0.0F};
    float top{0.0F};
    float width{0.0F};
    float height{0.0F};
};

// The canvas rectangle a placement's sprite occupies, origin-corrected. Shared
// by the selection overlays and by viewport picking, so that what an author
// clicks is exactly what they see outlined.
[[nodiscard]] CanvasRect sprite_canvas_rect(const EntityBlueprint& entity,
                                            const Camera25DState& camera,
                                            const ApplicationConfig& config);

void draw_selection_outline(const WorldSnapshot& snapshot, const Camera25DState& camera,
                            const ApplicationConfig& config, EntityUuid selected);

void draw_selection_drag_preview(const WorldSnapshot& snapshot, const Camera25DState& camera,
                                 const ApplicationConfig& config, EntityUuid selected,
                                 Vec2 canvas_offset);

void draw_projected_ground_grid(const ApplicationConfig& config, const Camera25DState& camera,
                                const RectXZ& bounds);

void draw_compact_hud(const ApplicationConfig& config, RunState run_state, bool dropped_time,
                      std::string_view pacing_description);

#endif

} // namespace ic2d
