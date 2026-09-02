#pragma once

#include "ic2d/assets.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <raylib.h>

namespace ic2d {

struct TextureFileStamp {
    std::filesystem::file_time_type write_time{};
    std::uintmax_t size{0};
    bool exists{false};

    auto operator<=>(const TextureFileStamp&) const = default;
};

struct TextureAssets::Impl {
    struct Slot {
        Texture2D texture{};
        std::uint32_t generation{1};
        std::size_t reference_count{0};
        std::uint64_t revision{1};
        std::string source;
        TextureSampling sampling{TextureSampling::pixel};
        TextureFileStamp observed_stamp{};
        TextureFileStamp processed_stamp{};
        std::uint8_t stable_observations{0};
        bool occupied{false};
        bool fallback{false};
        bool file_backed{false};
    };

    std::vector<Slot> slots;
    std::vector<std::uint32_t> free_indices;
    std::unordered_map<std::string, std::uint32_t> path_cache;
    TextureHandle fallback_handle{};

    [[nodiscard]] Slot* resolve(TextureHandle handle) noexcept;
    [[nodiscard]] const Slot* resolve(TextureHandle handle) const noexcept;
};

} // namespace ic2d
