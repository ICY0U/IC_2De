#include "ic2d/locomotion.hpp"

#include <cmath>
#include <cstddef>
#include <type_traits>

namespace ic2d {
namespace {

constexpr float cardinal_sector_ratio = 0.41421356237F; // tan(22.5 degrees)

[[nodiscard]] std::size_t facing_index(const LocomotionState state) noexcept {
    const auto index = static_cast<std::underlying_type_t<LocomotionState>>(state);
    if (index < 0 || static_cast<std::size_t>(index) >= locomotion_state_count) {
        return 0;
    }
    return static_cast<std::size_t>(index) % locomotion_facing_count;
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

} // namespace ic2d
