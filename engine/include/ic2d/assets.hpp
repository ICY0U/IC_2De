#pragma once

#include "ic2d/types.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace ic2d {

struct TextureHandle {
    std::uint32_t index{0};
    std::uint32_t generation{0};

    [[nodiscard]] explicit operator bool() const noexcept { return index != 0 && generation != 0; }
    auto operator<=>(const TextureHandle&) const = default;
};

enum class TextureSampling {
    pixel,
    smooth,
};

struct TextureInfo {
    int width{0};
    int height{0};
    std::string source;
    std::size_t reference_count{0};
    bool fallback{false};
};

class RaylibRenderer2D;

// Must be created after the graphics context and destroyed before it closes.
class TextureAssets final {
public:
    TextureAssets();
    ~TextureAssets();

    TextureAssets(const TextureAssets&) = delete;
    TextureAssets& operator=(const TextureAssets&) = delete;
    TextureAssets(TextureAssets&&) = delete;
    TextureAssets& operator=(TextureAssets&&) = delete;

    [[nodiscard]] TextureHandle acquire(
        const std::filesystem::path& path,
        TextureSampling sampling = TextureSampling::pixel
    );
    [[nodiscard]] TextureHandle create_checker(
        std::string name,
        int width,
        int height,
        int cell_size,
        ColorRgba8 first,
        ColorRgba8 second,
        TextureSampling sampling = TextureSampling::pixel
    );
    [[nodiscard]] TextureHandle create_radial_gradient(
        std::string name,
        int width,
        int height,
        ColorRgba8 inner,
        ColorRgba8 outer,
        TextureSampling sampling = TextureSampling::smooth
    );
    void release(TextureHandle handle) noexcept;
    void shutdown() noexcept;

    [[nodiscard]] TextureHandle fallback() const noexcept;
    [[nodiscard]] bool alive(TextureHandle handle) const noexcept;
    [[nodiscard]] std::optional<TextureInfo> info(TextureHandle handle) const;
    [[nodiscard]] std::size_t loaded_texture_count() const noexcept;

private:
    friend class RaylibRenderer2D;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
