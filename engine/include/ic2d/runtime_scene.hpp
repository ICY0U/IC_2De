#pragma once

#include "ic2d/assets.hpp"
#include "ic2d/events.hpp"
#include "ic2d/physics2d.hpp"
#include "ic2d/scene.hpp"
#include "ic2d/world.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ic2d {

struct RuntimeSceneTickResult {
    bool player_blocked{false};
    bool player_elevated{false};
    bool primary_prop_moved{false};
    std::optional<std::uint32_t> active_trigger;
    std::vector<EngineEvent> events;
};

// Owns a playable scene's World, GroundMap, PhysicsWorld, asset handles, and
// transform bindings. Callers provide one ground-plane direction per fixed tick.
class RuntimeScene final {
public:
    RuntimeScene(SceneDefinition definition, TextureAssets& textures);
    ~RuntimeScene();

    RuntimeScene(const RuntimeScene&) = delete;
    RuntimeScene& operator=(const RuntimeScene&) = delete;
    RuntimeScene(RuntimeScene&&) noexcept;
    RuntimeScene& operator=(RuntimeScene&&) noexcept;

    void reset();
    [[nodiscard]] RuntimeSceneTickResult tick(
        const Vec2& player_ground_direction,
        float fixed_step_seconds
    );

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] const Camera25DState& initial_camera() const noexcept;
    [[nodiscard]] const GroundMapDefinition& ground_definition() const noexcept;
    [[nodiscard]] Vec3 player_position() const noexcept;
    [[nodiscard]] Vec3 primary_prop_position() const noexcept;
    [[nodiscard]] WorldSnapshot world_snapshot() const;
    [[nodiscard]] std::vector<RenderItem2D> collect_render_items(float interpolation_alpha) const;
    [[nodiscard]] std::vector<PhysicsFootprint> debug_footprints() const;
    [[nodiscard]] std::size_t entity_count() const noexcept;
    [[nodiscard]] std::size_t physics_body_count() const noexcept;
    [[nodiscard]] std::size_t animation_binding_count() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
