#pragma once

#include "ic2d/assets.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <raylib.h>

namespace ic2d {

struct TextureAssets::Impl {
    struct Slot {
        Texture2D texture{};
        std::uint32_t generation{1};
        std::size_t reference_count{0};
        std::string source;
        bool occupied{false};
        bool fallback{false};
    };

    std::vector<Slot> slots;
    std::vector<std::uint32_t> free_indices;
    std::unordered_map<std::string, std::uint32_t> path_cache;
    TextureHandle fallback_handle{};

    [[nodiscard]] Slot* resolve(TextureHandle handle) noexcept;
    [[nodiscard]] const Slot* resolve(TextureHandle handle) const noexcept;
};

} // namespace ic2d
