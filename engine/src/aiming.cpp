#include "ic2d/aiming.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ic2d {
namespace {

[[nodiscard]] float radians(const float degrees) noexcept {
    return degrees * std::numbers::pi_v<float> / 180.0F;
}

[[nodiscard]] float length_of(const Vec2 value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

[[nodiscard]] bool finite(const Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool finite(const Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] Vec2 normalized(const Vec2 value) noexcept {
    const float length = length_of(value);
    return length > 0.00001F ? Vec2{value.x / length, value.y / length} : Vec2{0.0F, 0.0F};
}

// Signed angle from `from` to `to`, in radians, on the ground plane. The sign is
// what lets a rotation be capped without losing which way it was going.
[[nodiscard]] float signed_angle(const Vec2 from, const Vec2 to) noexcept {
    const float cross = from.x * to.y - from.y * to.x;
    const float dot = from.x * to.x + from.y * to.y;
    return std::atan2(cross, dot);
}

[[nodiscard]] Vec2 rotated(const Vec2 value, const float angle) noexcept {
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    return {value.x * cosine - value.y * sine, value.x * sine + value.y * cosine};
}

} // namespace

Vec3 muzzle_origin(
    const Vec3 actor_position,
    const Vec2 aim_direction,
    const MuzzleGeometry geometry
) noexcept {
    if (!finite(actor_position) || !finite(aim_direction) ||
        !std::isfinite(geometry.forward) || !std::isfinite(geometry.height)) {
        return actor_position;
    }
    const Vec2 direction = normalized(aim_direction);
    return {
        actor_position.x + direction.x * geometry.forward,
        actor_position.y + geometry.height,
        actor_position.z + direction.y * geometry.forward,
    };
}

void Aiming::reset() noexcept { snapshot_ = AimingSnapshot{}; }

const AimingSnapshot& Aiming::resolve(
    const AimingInputs& inputs,
    const std::span<const AimTarget> targets
) {
    // A rejected frame holds the previous aim rather than snapping to a
    // default, because a weapon that resets its facing on one bad sample reads
    // as a glitch.
    if (!finite(inputs.actor_position) || !finite(inputs.stick_world) ||
        !std::isfinite(inputs.delta_seconds)) {
        snapshot_.aiming = false;
        snapshot_.turning = false;
        snapshot_.assisted_target.reset();
        return snapshot_;
    }

    const Vec2 held = length_of(snapshot_.direction) > 0.00001F
                          ? normalized(snapshot_.direction)
                          : Vec2{0.0F, 1.0F};

    // --- what the player asked for ------------------------------------------
    Vec2 requested = held;
    bool aiming = false;
    bool pointer_source = false;
    std::optional<float> pointer_distance;

    if (inputs.pointer_active && inputs.pointer_world_point &&
        finite(*inputs.pointer_world_point)) {
        const Vec2 offset{inputs.pointer_world_point->x - inputs.actor_position.x,
                          inputs.pointer_world_point->z - inputs.actor_position.z};
        const float distance = length_of(offset);
        pointer_source = true;
        if (distance >= config_.pointer_minimum_distance) {
            requested = normalized(offset);
            pointer_distance = distance;
            aiming = true;
        }
    } else {
        const float deflection = length_of(inputs.stick_world);
        if (deflection >= config_.stick_dead_zone) {
            requested = normalized(inputs.stick_world);
            aiming = true;
        }
    }

    // --- how fast it may turn ------------------------------------------------
    Vec2 direction = requested;
    bool turning = false;
    if (aiming && !inputs.direct && !pointer_source) {
        // A pointer is absolute and must land exactly where it points. A stick
        // is a rate control, so it turns at a bounded speed scaled by how far
        // it is pushed: a light touch adjusts, a full push whips around.
        const float deflection = std::clamp(length_of(inputs.stick_world), 0.0F, 1.0F);
        const float span = std::max(1.0F - config_.stick_dead_zone, 0.0001F);
        const float eased = std::clamp((deflection - config_.stick_dead_zone) / span, 0.0F, 1.0F);
        const float maximum_turn =
            radians(config_.stick_turn_degrees_per_second) * eased * eased *
            std::max(inputs.delta_seconds, 0.0F);
        const float wanted = signed_angle(held, requested);
        if (maximum_turn > 0.0F && std::abs(wanted) > maximum_turn) {
            direction = rotated(held, std::copysign(maximum_turn, wanted));
            turning = true;
        }
    }

    // --- assist --------------------------------------------------------------
    std::optional<EntityUuid> assisted;
    float assisted_distance = 0.0F;
    if (aiming && !inputs.direct && config_.assist_strength > 0.0F &&
        config_.assist_cone_degrees > 0.0F) {
        const float cone = radians(config_.assist_cone_degrees);
        float best_angle = cone;
        Vec2 best_direction{};
        for (const AimTarget& target : targets) {
            if (!finite(target.position)) {
                continue;
            }
            const Vec2 offset{target.position.x - inputs.actor_position.x,
                              target.position.z - inputs.actor_position.z};
            const float distance = length_of(offset);
            if (!(distance > 0.0001F) || distance > config_.assist_range) {
                continue;
            }
            const Vec2 to_target = normalized(offset);
            const float angle = std::abs(signed_angle(direction, to_target));
            // Nearest by angle, then by range, so two candidates on the same
            // bearing resolve to the one actually in the way.
            if (angle < best_angle ||
                (angle == best_angle && assisted && distance < assisted_distance)) {
                best_angle = angle;
                best_direction = to_target;
                assisted = target.actor;
                assisted_distance = distance;
            }
        }
        if (assisted) {
            const float correction =
                signed_angle(direction, best_direction) * std::clamp(config_.assist_strength,
                                                                     0.0F, 1.0F);
            direction = normalized(rotated(direction, correction));
        }
    }

    if (length_of(direction) < 0.00001F) {
        direction = held;
    }
    direction = normalized(direction);

    // --- where it points -----------------------------------------------------
    const float distance = assisted             ? assisted_distance
                           : pointer_distance   ? *pointer_distance
                                                : config_.stick_aim_distance;
    const Vec3 origin = muzzle_origin(inputs.actor_position, direction, config_.muzzle);

    snapshot_ = AimingSnapshot{
        .direction = direction,
        .origin = origin,
        .aim_point = {inputs.actor_position.x + direction.x * distance,
                      inputs.actor_position.y,
                      inputs.actor_position.z + direction.y * distance},
        .distance = distance,
        .assisted_target = assisted,
        .pointer_source = pointer_source,
        .aiming = aiming,
        .turning = turning,
    };
    return snapshot_;
}

} // namespace ic2d
