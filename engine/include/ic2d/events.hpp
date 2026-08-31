#pragma once

#include "ic2d/identity.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

namespace ic2d {

// Engine-owned events copied out of simulation producers after a fixed tick.
// No Box2D, raylib, EnTT, or editor implementation type crosses this seam.
struct SceneContactEvent {
    bool began{false};
    std::uint32_t tag_a{0};
    std::uint32_t tag_b{0};
};

struct SceneTriggerEvent {
    bool entered{false};
    std::uint32_t tag{0};
    bool player_visitor{false};
};

struct SceneAnimationEvent {
    EntityUuid entity_uuid{};
    std::string entity_id;
    std::string clip_id;
    std::string name;
    std::size_t frame_index{0};
};

using EngineEvent = std::variant<SceneContactEvent, SceneTriggerEvent, SceneAnimationEvent>;

} // namespace ic2d
