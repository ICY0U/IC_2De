#pragma once

#include "ic2d/assets.hpp"
#include "ic2d/types.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>
#include <vector>

namespace ic2d {

struct Camera2DState {
    Vec2 center{320.0F, 180.0F};
    float rotation_degrees{0.0F};
    float zoom{1.0F};
};

struct SpriteSubmission2D {
    std::uint64_t stable_id{0};
    TextureHandle texture{};
    RectF source{}; // A zero-sized source uses the complete texture.
    Vec2 position{};
    Vec2 size{16.0F, 16.0F};
    Vec2 scale{1.0F, 1.0F};
    Vec2 normalized_origin{};
    float rotation_degrees{0.0F};
    float sort_depth{0.0F};
    ColorRgba8 tint{};
    std::int32_t layer{0};
};

// A projected ground-aligned surface. Points are supplied clockwise around the
// perimeter; surfaces render in submission order before camera-facing sprites.
struct GroundQuadSubmission2D {
    std::uint64_t stable_id{0};
    std::array<Vec2, 4> points{};
    ColorRgba8 tint{};
};

struct RenderDiagnostics2D {
    std::size_t submitted_ground_quads{0};
    std::size_t visible_ground_quads{0};
    std::size_t culled_ground_quads{0};
    std::size_t submitted_sprites{0};
    std::size_t visible_sprites{0};
    std::size_t culled_sprites{0};
    std::size_t estimated_batches{0};
    std::size_t estimated_draw_calls{0};
    std::size_t texture_switches{0};
    std::size_t visible_vertices{0};
};

class RenderFrame2D final {
public:
    [[nodiscard]] const Camera2DState& camera() const noexcept;
    [[nodiscard]] std::span<const GroundQuadSubmission2D> ground_quads() const noexcept;
    [[nodiscard]] std::span<const SpriteSubmission2D> sprites() const noexcept;

private:
    friend class RenderQueue2D;
    Camera2DState camera_{};
    std::vector<GroundQuadSubmission2D> ground_quads_;
    std::vector<SpriteSubmission2D> sprites_;
};

// A frame becomes immutable once finish() returns it.
class RenderQueue2D final {
public:
    void begin(const Camera2DState& camera);
    void submit_ground(GroundQuadSubmission2D ground_quad);
    void submit(SpriteSubmission2D sprite);
    [[nodiscard]] RenderFrame2D finish();

    [[nodiscard]] bool building() const noexcept;

private:
    bool building_{false};
    Camera2DState camera_{};
    std::vector<GroundQuadSubmission2D> ground_quads_;
    std::vector<SpriteSubmission2D> sprites_;
};

} // namespace ic2d
