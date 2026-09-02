#include "render/raylib_renderer2d.hpp"

#include "assets/texture_assets_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

#include <raylib.h>

namespace ic2d {
namespace {

[[nodiscard]] Color to_raylib_color(const ColorRgba8 color) noexcept {
    return {color.red, color.green, color.blue, color.alpha};
}

[[nodiscard]] bool visible(
    const SpriteSubmission2D& sprite,
    const Camera2DState& camera,
    const int canvas_width,
    const int canvas_height
) noexcept {
    if (camera.rotation_degrees != 0.0F) {
        return true;
    }

    const float width = std::abs(sprite.size.x * sprite.scale.x);
    const float height = std::abs(sprite.size.y * sprite.scale.y);
    const float left = sprite.position.x - sprite.normalized_origin.x * width;
    const float top = sprite.position.y - sprite.normalized_origin.y * height;
    const float half_view_width = static_cast<float>(canvas_width) * 0.5F / camera.zoom;
    const float half_view_height = static_cast<float>(canvas_height) * 0.5F / camera.zoom;
    const float view_left = camera.center.x - half_view_width;
    const float view_right = camera.center.x + half_view_width;
    const float view_top = camera.center.y - half_view_height;
    const float view_bottom = camera.center.y + half_view_height;
    return left + width >= view_left && left <= view_right &&
           top + height >= view_top && top <= view_bottom;
}

[[nodiscard]] bool visible(
    const GroundQuadSubmission2D& ground_quad,
    const Camera2DState& camera,
    const int canvas_width,
    const int canvas_height
) noexcept {
    float left = ground_quad.points.front().x;
    float right = left;
    float top = ground_quad.points.front().y;
    float bottom = top;
    for (const Vec2& point : ground_quad.points) {
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }

    const float half_view_width = static_cast<float>(canvas_width) * 0.5F / camera.zoom;
    const float half_view_height = static_cast<float>(canvas_height) * 0.5F / camera.zoom;
    const float view_left = camera.center.x - half_view_width;
    const float view_right = camera.center.x + half_view_width;
    const float view_top = camera.center.y - half_view_height;
    const float view_bottom = camera.center.y + half_view_height;
    return right >= view_left && left <= view_right && bottom >= view_top && top <= view_bottom;
}

[[nodiscard]] std::uint64_t material_key(const TextureHandle handle) noexcept {
    return (static_cast<std::uint64_t>(handle.generation) << 32U) | handle.index;
}

} // namespace

RaylibRenderer2D::RaylibRenderer2D(TextureAssets& textures) noexcept
    : textures_{textures} {}

RenderDiagnostics2D RaylibRenderer2D::render(
    const RenderFrame2D& frame,
    const int canvas_width,
    const int canvas_height
) const {
    RenderDiagnostics2D diagnostics{
        .submitted_ground_quads = frame.ground_quads().size(),
        .submitted_sprites = frame.submitted_sprites(),
        .culled_sprites = frame.culled_sprites(),
    };
    std::optional<std::uint64_t> previous_material;

    const Camera2D camera{
        .offset = {static_cast<float>(canvas_width) * 0.5F, static_cast<float>(canvas_height) * 0.5F},
        .target = {frame.camera().center.x, frame.camera().center.y},
        .rotation = frame.camera().rotation_degrees,
        .zoom = frame.camera().zoom,
    };

    BeginMode2D(camera);
    for (const GroundQuadSubmission2D& ground_quad : frame.ground_quads()) {
        if (!visible(ground_quad, frame.camera(), canvas_width, canvas_height)) {
            ++diagnostics.culled_ground_quads;
            continue;
        }

        ++diagnostics.visible_ground_quads;
        diagnostics.visible_vertices += 4U;
        const Vector2 points[]{
            {ground_quad.points[0].x, ground_quad.points[0].y},
            {ground_quad.points[3].x, ground_quad.points[3].y},
            {ground_quad.points[2].x, ground_quad.points[2].y},
            {ground_quad.points[1].x, ground_quad.points[1].y},
        };
        DrawTriangleFan(points, 4, to_raylib_color(ground_quad.tint));
    }

    // Culling happened at submission, so every sprite reaching the renderer is
    // one it is going to draw.
    for (const SpriteSubmission2D& sprite : frame.sprites()) {
        ++diagnostics.visible_sprites;
        diagnostics.visible_vertices += 4U;
        const float width = sprite.size.x * sprite.scale.x;
        const float height = sprite.size.y * sprite.scale.y;
        const Rectangle destination{sprite.position.x, sprite.position.y, width, height};
        const Vector2 origin{sprite.normalized_origin.x * width, sprite.normalized_origin.y * height};
        const Color tint = to_raylib_color(sprite.tint);
        std::uint64_t current_material = 0;

        if (sprite.texture) {
            const TextureAssets::Impl::Slot* slot = textures_.impl_->resolve(sprite.texture);
            TextureHandle resolved_handle = sprite.texture;
            if (!slot) {
                resolved_handle = textures_.impl_->fallback_handle;
                slot = textures_.impl_->resolve(resolved_handle);
            }

            if (slot) {
                Rectangle source = sprite.source.width > 0.0F && sprite.source.height > 0.0F
                                       ? Rectangle{sprite.source.x, sprite.source.y,
                                                   sprite.source.width, sprite.source.height}
                                       : Rectangle{0.0F, 0.0F,
                                                   static_cast<float>(slot->texture.width),
                                                   static_cast<float>(slot->texture.height)};
                if (sprite.flip_x) {
                    source.width = -source.width;
                }
                DrawTexturePro(slot->texture, source, destination, origin, sprite.rotation_degrees, tint);
                current_material = material_key(resolved_handle);
            }
        } else {
            DrawRectanglePro(destination, origin, sprite.rotation_degrees, tint);
        }

        if (!previous_material || *previous_material != current_material) {
            ++diagnostics.estimated_batches;
            if (previous_material) {
                ++diagnostics.texture_switches;
            }
            previous_material = current_material;
        }
    }
    EndMode2D();
    diagnostics.estimated_draw_calls = diagnostics.estimated_batches +
                                       (diagnostics.visible_ground_quads > 0U ? 1U : 0U);
    return diagnostics;
}

} // namespace ic2d
