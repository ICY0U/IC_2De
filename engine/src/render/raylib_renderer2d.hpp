#pragma once

#include "ic2d/render2d.hpp"

namespace ic2d {

class TextureAssets;

class RaylibRenderer2D final {
public:
    explicit RaylibRenderer2D(TextureAssets& textures) noexcept;

    [[nodiscard]] RenderDiagnostics2D render(
        const RenderFrame2D& frame,
        int canvas_width,
        int canvas_height
    ) const;

private:
    TextureAssets& textures_;
};

} // namespace ic2d
