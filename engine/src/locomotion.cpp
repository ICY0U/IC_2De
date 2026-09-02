#include "ic2d/locomotion.hpp"

#include <cmath>
#include <cstddef>
#include <type_traits>

namespace ic2d {
namespace {

constexpr float cardinal_sector_ratio = 0.41421356237F; // tan(22.5 degrees)
constexpr float facing_retention_dot = 0.86602540378F; // cos(30 degrees)

[[nodiscard]] std::size_t facing_index(const LocomotionState state) noexcept {
    const auto index = static_cast<std::underlying_type_t<LocomotionState>>(state);
    if (index < 0 || static_cast<std::size_t>(index) >= locomotion_state_count) {
        return 0;
    }
    return static_cast<std::size_t>(index) % locomotion_facing_count;
}

[[nodiscard]] Vec2 facing_direction(const LocomotionState state) noexcept {
    constexpr float diagonal = 0.70710678118F;
    constexpr Vec2 directions[locomotion_facing_count]{
        {0.0F, 1.0F},
        {-diagonal, diagonal},
        {-1.0F, 0.0F},
        {-diagonal, -diagonal},
        {0.0F, -1.0F},
        {diagonal, -diagonal},
        {1.0F, 0.0F},
        {diagonal, diagonal},
    };
    return directions[facing_index(state)];
}

} // namespace

LocomotionState locomotion_facing(const Vec2 direction) noexcept {
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y) ||
        (direction.x == 0.0F && direction.y == 0.0F)) {
        return LocomotionState::idle_south;
    }

    const float horizontal = std::abs(direction.x);
    const float vertical = std::abs(direction.y);
    if (horizontal <= vertical * cardinal_sector_ratio) {
        return direction.y < 0.0F ? LocomotionState::idle_north
                                  : LocomotionState::idle_south;
    }
    if (vertical <= horizontal * cardinal_sector_ratio) {
        return direction.x < 0.0F ? LocomotionState::idle_west
                                  : LocomotionState::idle_east;
    }
    if (direction.y < 0.0F) {
        return direction.x < 0.0F ? LocomotionState::idle_northwest
                                  : LocomotionState::idle_northeast;
    }
    return direction.x < 0.0F ? LocomotionState::idle_southwest
                              : LocomotionState::idle_southeast;
}

LocomotionState idle_locomotion(const LocomotionState state) noexcept {
    return static_cast<LocomotionState>(facing_index(state));
}

LocomotionState moving_locomotion(const LocomotionState state) noexcept {
    return static_cast<LocomotionState>(facing_index(state) + locomotion_facing_count);
}

LocomotionState dodging_locomotion(const LocomotionState state) noexcept {
    return static_cast<LocomotionState>(facing_index(state) + locomotion_core_state_count);
}

bool is_dodging_locomotion(const LocomotionState state) noexcept {
    const auto index = static_cast<std::underlying_type_t<LocomotionState>>(state);
    return index >= static_cast<std::underlying_type_t<LocomotionState>>(
                        locomotion_core_state_count) &&
           index < static_cast<std::underlying_type_t<LocomotionState>>(locomotion_state_count);
}

LocomotionState locomotion_state(
    const LocomotionState previous_facing,
    const Vec2 facing_direction,
    const bool moving
) noexcept {
    const bool usable_direction = std::isfinite(facing_direction.x) &&
                                  std::isfinite(facing_direction.y) &&
                                  (facing_direction.x != 0.0F || facing_direction.y != 0.0F);
    LocomotionState facing = idle_locomotion(previous_facing);
    if (usable_direction) {
        const float length = std::sqrt(facing_direction.x * facing_direction.x +
                                       facing_direction.y * facing_direction.y);
        const Vec2 retained_direction = ic2d::facing_direction(facing);
        const float retained_dot =
            (facing_direction.x * retained_direction.x +
             facing_direction.y * retained_direction.y) /
            length;
        // The previous 45-degree sector gets a 7.5-degree retention margin.
        // Mouse noise must leave that wider cone before the facing changes.
        if (retained_dot < facing_retention_dot) {
            facing = locomotion_facing(facing_direction);
        }
    }
    return moving ? moving_locomotion(facing) : idle_locomotion(facing);
}

} // namespace ic2d
