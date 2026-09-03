#pragma once

#include "ic2d/animation.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ic2d {

struct AsepriteImportResult {
    std::filesystem::path atlas_path;
    std::vector<AnimationClip> clips;
};

// Imports Aseprite CLI --format json-array metadata. JSON types remain private
// to this adapter; the rest of the engine consumes only owned animation clips.
[[nodiscard]] AsepriteImportResult import_aseprite_json(const std::filesystem::path& metadata_path,
                                                        std::uint32_t fixed_update_hz = 60);

} // namespace ic2d
