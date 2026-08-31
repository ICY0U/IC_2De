#pragma once

#include <cstdint>

namespace ic2d {

struct Vec2 {
    float x{0.0F};
    float y{0.0F};
};

struct Vec3 {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

struct RectF {
    float x{0.0F};
    float y{0.0F};
    float width{0.0F};
    float height{0.0F};
};

struct RectXZ {
    float x{0.0F};
    float z{0.0F};
    float width{0.0F};
    float depth{0.0F};
};

struct ColorRgba8 {
    std::uint8_t red{255};
    std::uint8_t green{255};
    std::uint8_t blue{255};
    std::uint8_t alpha{255};
};

} // namespace ic2d
