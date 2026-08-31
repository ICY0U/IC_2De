#pragma once

#include "ic2d/types.hpp"

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace ic2d {

// Stable engine-owned handle. Box2D identifiers never cross this interface.
struct PhysicsBodyId {
    std::uint32_t slot{0};
    std::uint32_t generation{0};

    [[nodiscard]] explicit operator bool() const noexcept { return slot != 0; }
    auto operator<=>(const PhysicsBodyId&) const = default;
};

enum class PhysicsMotionType {
    static_body,
    kinematic_body,
    dynamic_body,
};

struct PhysicsWorldConfig {
    // Gameplay coordinates are pixels/world units; Box2D runs in metres.
    float pixels_per_metre{32.0F};
    Vec2 gravity_pixels_per_second_squared{};
    std::int32_t substep_count{4};
    bool enable_sleep{true};
};

struct PhysicsBoxDefinition {
    PhysicsMotionType motion{PhysicsMotionType::static_body};
    Vec2 center{}; // Engine X/Z maps to this X/Y ground-plane pair.
    Vec2 half_extents{8.0F, 8.0F};
    Vec2 linear_velocity{};
    float rotation_radians{0.0F};
    float angular_velocity_radians{0.0F};
    float linear_damping{0.0F};
    float angular_damping{0.0F};
    float gravity_scale{1.0F};
    float density{1.0F};
    float friction{0.6F};
    float restitution{0.0F};
    std::uint64_t category_bits{1};
    std::uint64_t mask_bits{~std::uint64_t{0}};
    std::uint32_t tag{0};
    bool sensor{false};
    bool fixed_rotation{false};
};

struct PhysicsBodySnapshot {
    PhysicsBodyId body{};
    Vec2 center{};
    Vec2 linear_velocity{};
    float rotation_radians{0.0F};
    bool awake{false};
};

enum class PhysicsEventKind {
    contact_begin,
    contact_end,
    trigger_begin,
    trigger_end,
};

struct PhysicsEvent {
    PhysicsEventKind kind{PhysicsEventKind::contact_begin};
    PhysicsBodyId body_a{};
    PhysicsBodyId body_b{};
    std::uint32_t tag_a{0};
    std::uint32_t tag_b{0};
};

struct PhysicsStepResult {
    std::vector<PhysicsBodySnapshot> bodies;
    std::vector<PhysicsEvent> events;
};

struct PhysicsFootprint {
    PhysicsBodyId body{};
    PhysicsMotionType motion{PhysicsMotionType::static_body};
    Vec2 center{};
    Vec2 half_extents{};
    float rotation_radians{0.0F};
    bool sensor{false};
};

// Owns the Box2D world, unit conversion, handle lifetime, and event copying.
// Invalid numeric definitions throw; stale handles return false/nullopt.
class PhysicsWorld final {
public:
    explicit PhysicsWorld(PhysicsWorldConfig config = {});
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;

    [[nodiscard]] PhysicsBodyId create_box(const PhysicsBoxDefinition& definition);
    [[nodiscard]] bool destroy_body(PhysicsBodyId body) noexcept;
    [[nodiscard]] bool set_transform(
        PhysicsBodyId body,
        const Vec2& center,
        float rotation_radians = 0.0F
    );
    [[nodiscard]] bool set_linear_velocity(PhysicsBodyId body, const Vec2& velocity);
    [[nodiscard]] bool set_kinematic_target(
        PhysicsBodyId body,
        const Vec2& center,
        float time_step_seconds
    );

    [[nodiscard]] std::optional<PhysicsBodySnapshot> snapshot(PhysicsBodyId body) const noexcept;
    [[nodiscard]] PhysicsStepResult step(float time_step_seconds);
    [[nodiscard]] std::vector<PhysicsFootprint> debug_footprints() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
