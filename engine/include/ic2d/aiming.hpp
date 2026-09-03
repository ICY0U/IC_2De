#pragma once

#include "ic2d/identity.hpp"
#include "ic2d/types.hpp"

#include <optional>
#include <span>

namespace ic2d {

// A candidate the aim may be nudged toward. The module never reaches into a
// scene: the caller copies out whatever it considers shootable this frame.
struct AimTarget {
    EntityUuid actor{};
    Vec3 position{};
};

// Where a shot leaves the actor, relative to its ground position and its aim.
// Aim resolution, the crosshair, and the projectile spawn all resolve their
// origin from this one value: if they each carried their own constants, shots
// would visibly leave from somewhere other than where the player aimed.
struct MuzzleGeometry {
    float forward{9.0F};
    float height{17.0F};
};

// The world position a shot leaves from. The aim direction need not be
// normalized; a zero direction yields the actor's own position raised to
// muzzle height rather than a division by zero.
[[nodiscard]] Vec3 muzzle_origin(Vec3 actor_position, Vec2 aim_direction,
                                 MuzzleGeometry geometry) noexcept;

// Feel, expressed as data so it can be tuned without touching resolution rules.
struct AimingConfig {
    // Below this deflection the stick is at rest and the aim holds, so a
    // resting thumb never drifts the crosshair.
    float stick_dead_zone{0.24F};
    // Full-deflection turn rate. Partial deflection turns proportionally
    // slower, which is what makes a stick usable for fine adjustment.
    float stick_turn_degrees_per_second{620.0F};

    // Assist is a bounded nudge, never a lock: it closes at most this fraction
    // of the angle to the best candidate, so the player keeps authority.
    float assist_strength{0.35F};
    float assist_cone_degrees{13.0F};
    float assist_range{420.0F};

    MuzzleGeometry muzzle{};

    // A pointer resting on the actor has no meaningful direction, so the aim
    // holds rather than spinning with sub-pixel noise.
    float pointer_minimum_distance{7.0F};
    // How far ahead the crosshair sits when a stick is aiming and nothing is
    // being assisted toward.
    float stick_aim_distance{92.0F};
};

// One resolved frame of aim. Everything downstream (combat, crosshair,
// telemetry) reads this rather than re-deriving aim from raw input.
struct AimingSnapshot {
    // Normalized world X/Z. Valid whether or not the player is aiming, because
    // a weapon always points somewhere; `aiming` says whether it was requested.
    Vec2 direction{0.0F, 1.0F};
    // World position a shot leaves from.
    Vec3 origin{};
    // World ground position the aim resolves to, at the actor's elevation.
    Vec3 aim_point{};
    float distance{0.0F};
    // Set when assist contributed this frame, so the crosshair can show it.
    std::optional<EntityUuid> assisted_target;
    bool pointer_source{false};
    // False when neither a pointer nor a deflected stick asked for a direction.
    bool aiming{false};
    // True while a stick is still turning toward what it asked for.
    bool turning{false};
};

struct AimingInputs {
    Vec3 actor_position{};
    bool pointer_active{false};
    // The world ground point under the pointer, at the actor's elevation. The
    // application owns the camera, so it resolves this and the module stays
    // free of projection.
    std::optional<Vec3> pointer_world_point;
    // Stick deflection already rotated from camera space into world X/Z. Its
    // length is the deflection and is what scales the turn rate.
    Vec2 stick_world{};
    float delta_seconds{0.0F};
    // Adopt the requested direction exactly: no smoothing and no assist.
    // Automation states an exact direction, and a recorded run must not depend
    // on frame pacing or on which enemies happen to be nearby.
    bool direct{false};
};

// Resolves where the player is aiming, from raw input and copied target facts.
//
// It owns the parts that make aiming feel deliberate rather than raw: a stick
// dead zone and turn-rate cap, a bounded assist nudge, and a muzzle origin so
// the crosshair and the projectile agree. It holds one piece of state, the
// direction it settled on last frame, which is what lets a stick turn smoothly
// and what a resting input holds onto.
class Aiming final {
public:
    Aiming() noexcept = default;
    explicit Aiming(AimingConfig config) noexcept : config_{config} {}

    [[nodiscard]] const AimingConfig& config() const noexcept { return config_; }
    void set_config(const AimingConfig& config) noexcept { config_ = config; }

    // Returns to facing along +Z with no aim requested.
    void reset() noexcept;

    // Resolves this frame. Targets are copied facts, valid only for this call.
    const AimingSnapshot& resolve(const AimingInputs& inputs, std::span<const AimTarget> targets);

    [[nodiscard]] const AimingSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    AimingConfig config_{};
    AimingSnapshot snapshot_{};
};

} // namespace ic2d
