#pragma once

#include "ic2d/types.hpp"

#include <cstddef>

namespace ic2d {

enum class LocomotionState {
    idle_south,
    idle_southwest,
    idle_west,
    idle_northwest,
    idle_north,
    idle_northeast,
    idle_east,
    idle_southeast,
    move_south,
    move_southwest,
    move_west,
    move_northwest,
    move_north,
    move_northeast,
    move_east,
    move_southeast,
    dodge_south,
    dodge_southwest,
    dodge_west,
    dodge_northwest,
    dodge_north,
    dodge_northeast,
    dodge_east,
    dodge_southeast,
};

inline constexpr std::size_t locomotion_facing_count = 8;
inline constexpr std::size_t locomotion_core_state_count = 16;
inline constexpr std::size_t locomotion_state_count = 24;

// Quantizes an X/Z movement vector into eight equal 45-degree sectors. Zero or
// non-finite input safely returns south. Returned states are always idle-facing.
[[nodiscard]] LocomotionState locomotion_facing(Vec2 direction) noexcept;
[[nodiscard]] LocomotionState idle_locomotion(LocomotionState state) noexcept;
[[nodiscard]] LocomotionState moving_locomotion(LocomotionState state) noexcept;
[[nodiscard]] LocomotionState dodging_locomotion(LocomotionState state) noexcept;
[[nodiscard]] bool is_dodging_locomotion(LocomotionState state) noexcept;

// Selects idle or moving presentation from an independent facing direction.
// A zero/invalid direction preserves the previous facing.
[[nodiscard]] LocomotionState locomotion_state(
    LocomotionState previous_facing,
    Vec2 facing_direction,
    bool moving
) noexcept;

} // namespace ic2d
