#include "ic2d/render2d.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace ic2d {

const Camera2DState& RenderFrame2D::camera() const noexcept { return camera_; }

std::span<const GroundQuadSubmission2D> RenderFrame2D::ground_quads() const noexcept {
    return ground_quads_;
}

std::span<const SpriteSubmission2D> RenderFrame2D::sprites() const noexcept { return sprites_; }

std::size_t RenderFrame2D::submitted_sprites() const noexcept { return submitted_sprites_; }

std::size_t RenderFrame2D::culled_sprites() const noexcept { return culled_sprites_; }

std::size_t RenderFrame2D::submitted_ground_quads() const noexcept { return ground_quads_.size(); }

namespace {

// A sprite is kept when its bounding box meets the view. A rotated camera has
// no axis-aligned view box to test against, so nothing is rejected there.
[[nodiscard]] bool sprite_visible(const SpriteSubmission2D& sprite, const Camera2DState& camera,
                                  const int canvas_width, const int canvas_height) noexcept {
    if (camera.rotation_degrees != 0.0F || canvas_width <= 0 || canvas_height <= 0) {
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
    return left + width >= view_left && left <= view_right && top + height >= view_top &&
           top <= view_bottom;
}

} // namespace

void RenderQueue2D::begin(const Camera2DState& camera, const int canvas_width,
                          const int canvas_height) {
    if (!std::isfinite(camera.zoom) || camera.zoom <= 0.0F) {
        throw std::invalid_argument{"Camera zoom must be finite and greater than zero."};
    }
    camera_ = camera;
    canvas_width_ = canvas_width;
    canvas_height_ = canvas_height;
    submitted_sprites_ = 0;
    culled_sprites_ = 0;
    ground_quads_.clear();
    sprites_.clear();
    building_ = true;
}

void RenderQueue2D::submit_ground(GroundQuadSubmission2D ground_quad) {
    if (!building_) {
        throw std::logic_error{"Ground submission requires begin() first."};
    }
    const bool finite = std::ranges::all_of(ground_quad.points, [](const Vec2& point) {
        return std::isfinite(point.x) && std::isfinite(point.y);
    });
    if (!finite) {
        throw std::invalid_argument{"Ground quad points must be finite."};
    }
    ground_quads_.push_back(std::move(ground_quad));
}

void RenderQueue2D::submit(SpriteSubmission2D sprite) {
    if (!building_) {
        throw std::logic_error{"Render submission requires begin() first."};
    }
    if (!std::isfinite(sprite.sort_depth)) {
        throw std::invalid_argument{"Sprite sort depth must be finite."};
    }
    ++submitted_sprites_;
    if (!sprite_visible(sprite, camera_, canvas_width_, canvas_height_)) {
        ++culled_sprites_;
        return;
    }
    sprites_.push_back(std::move(sprite));
}

RenderFrame2D RenderQueue2D::finish() {
    if (!building_) {
        throw std::logic_error{"Cannot finish a render frame that has not begun."};
    }

    // Order a compact key rather than the submissions themselves. A sprite
    // submission is an order of magnitude wider than its ordering fields, and
    // a comparison sort moves each element many times, so sorting keys and
    // permuting once is markedly cheaper for a frame holding tens of thousands
    // of sprites. Carrying the submission index as the final tie-break makes
    // the order total, which both removes the need for a stable sort and its
    // temporary buffer, and reproduces the previous submission-order result
    // exactly when two sprites agree on every other field.
    sort_keys_.clear();
    sort_keys_.reserve(sprites_.size());
    for (std::size_t index = 0; index < sprites_.size(); ++index) {
        const SpriteSubmission2D& sprite = sprites_[index];
        sort_keys_.push_back({
            .layer = sprite.layer,
            .sort_depth = sprite.sort_depth,
            .stable_id = sprite.stable_id,
            .index = static_cast<std::uint32_t>(index),
        });
    }
    std::ranges::sort(sort_keys_, [](const SpriteSortKey& left, const SpriteSortKey& right) {
        if (left.layer != right.layer) {
            return left.layer < right.layer;
        }
        if (left.sort_depth != right.sort_depth) {
            return left.sort_depth < right.sort_depth;
        }
        if (left.stable_id != right.stable_id) {
            return left.stable_id < right.stable_id;
        }
        return left.index < right.index;
    });

    RenderFrame2D frame;
    frame.camera_ = camera_;
    frame.submitted_sprites_ = submitted_sprites_;
    frame.culled_sprites_ = culled_sprites_;
    frame.ground_quads_ = std::move(ground_quads_);
    frame.sprites_.resize(sort_keys_.size());
    for (std::size_t position = 0; position < sort_keys_.size(); ++position) {
        frame.sprites_[position] = sprites_[sort_keys_[position].index];
    }
    ground_quads_.clear();
    // Cleared rather than moved from, so the submission buffer keeps the
    // capacity it grew to and a steady frame stops reallocating entirely.
    sprites_.clear();
    building_ = false;
    return frame;
}

bool RenderQueue2D::building() const noexcept { return building_; }

} // namespace ic2d
