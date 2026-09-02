#pragma once

#include "ic2d/types.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace ic2d {

enum class DebugChannel {
    collision_shapes, // Solid ground footprints and non-sensor physics bodies.
    trigger_volumes,  // Trigger areas and sensor bodies.
    elevation_map,    // Authored +Y elevation shaded by height.
    world_grid,       // Projected ground grid and walkable bounds.
    navigation_grid,  // Dense 2.5D walkable/blocked cell snapshot.
    navigation_path,  // Copied A-star reference path and endpoints.
    stats_overlay,    // Compact viewport HUD; detailed data stays in the editor.
    lights,           // Reserved until a lighting module exists.
};

inline constexpr std::size_t debug_channel_count = 8;

[[nodiscard]] std::string_view debug_channel_name(DebugChannel channel) noexcept;

// A channel with no engine module behind it is unavailable and never draws, so
// tools can list it honestly instead of pretending it renders nothing.
[[nodiscard]] bool debug_channel_implemented(DebugChannel channel) noexcept;

// Shades one authored elevation sample so an elevation map reads as a gradient
// from low to high instead of one flat tint.
[[nodiscard]] ColorRgba8 debug_elevation_tint(float elevation, float maximum_elevation) noexcept;

// One master switch plus per-channel selection. With the master switch off the
// development build presents exactly what the shipping runtime presents.
class DebugVisuals final {
public:
    DebugVisuals() noexcept;

    [[nodiscard]] bool enabled() const noexcept;
    void set_enabled(bool enabled) noexcept;
    void toggle() noexcept;

    // Selection is remembered independently of the master switch, so toggling
    // the master switch back on restores the channels that were chosen before.
    [[nodiscard]] bool channel_selected(DebugChannel channel) const noexcept;
    void set_channel_selected(DebugChannel channel, bool selected) noexcept;

    [[nodiscard]] bool draws(DebugChannel channel) const noexcept;
    [[nodiscard]] std::size_t drawn_channel_count() const noexcept;

private:
    std::array<bool, debug_channel_count> selected_{};
    bool enabled_{true};
};

} // namespace ic2d
