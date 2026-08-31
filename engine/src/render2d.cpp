#include "ic2d/render2d.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace ic2d {

const Camera2DState& RenderFrame2D::camera() const noexcept {
    return camera_;
}

std::span<const GroundQuadSubmission2D> RenderFrame2D::ground_quads() const noexcept {
    return ground_quads_;
}

std::span<const SpriteSubmission2D> RenderFrame2D::sprites() const noexcept {
    return sprites_;
}

void RenderQueue2D::begin(const Camera2DState& camera) {
    if (!std::isfinite(camera.zoom) || camera.zoom <= 0.0F) {
        throw std::invalid_argument{"Camera zoom must be finite and greater than zero."};
    }
    camera_ = camera;
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
    sprites_.push_back(std::move(sprite));
}

RenderFrame2D RenderQueue2D::finish() {
    if (!building_) {
        throw std::logic_error{"Cannot finish a render frame that has not begun."};
    }

    std::ranges::stable_sort(sprites_, [](const SpriteSubmission2D& left, const SpriteSubmission2D& right) {
        if (left.layer != right.layer) {
            return left.layer < right.layer;
        }
        if (left.sort_depth != right.sort_depth) {
            return left.sort_depth < right.sort_depth;
        }
        return left.stable_id < right.stable_id;
    });

    RenderFrame2D frame;
    frame.camera_ = camera_;
    frame.ground_quads_ = std::move(ground_quads_);
    frame.sprites_ = std::move(sprites_);
    ground_quads_.clear();
    sprites_.clear();
    building_ = false;
    return frame;
}

bool RenderQueue2D::building() const noexcept {
    return building_;
}

} // namespace ic2d
