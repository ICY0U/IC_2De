#include "ic2d/debug_visuals.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ic2d {
namespace {

[[nodiscard]] std::size_t index_of(const DebugChannel channel) noexcept {
    return static_cast<std::size_t>(channel);
}

[[nodiscard]] std::uint8_t channel_byte(const float unit_value) noexcept {
    const float clamped = std::clamp(unit_value, 0.0F, 1.0F);
    return static_cast<std::uint8_t>(clamped * 255.0F + 0.5F);
}

} // namespace

std::string_view debug_channel_name(const DebugChannel channel) noexcept {
    switch (channel) {
    case DebugChannel::collision_shapes:
        return "Collision shapes";
    case DebugChannel::trigger_volumes:
        return "Trigger volumes";
    case DebugChannel::elevation_map:
        return "Elevation map";
    case DebugChannel::world_grid:
        return "World grid";
    case DebugChannel::navigation_grid:
        return "Navigation grid";
    case DebugChannel::navigation_path:
        return "Navigation path";
    case DebugChannel::stats_overlay:
        return "Compact HUD";
    case DebugChannel::lights:
        return "Lights";
    }
    return "Unknown";
}

bool debug_channel_implemented(const DebugChannel channel) noexcept {
    return channel != DebugChannel::lights;
}

ColorRgba8 debug_elevation_tint(
    const float elevation,
    const float maximum_elevation
) noexcept {
    const float span = maximum_elevation > 0.0F ? maximum_elevation : 1.0F;
    const float raw_ratio = elevation / span;
    const float ratio = std::isfinite(raw_ratio) ? std::clamp(raw_ratio, 0.0F, 1.0F) : 0.0F;
    return ColorRgba8{
        channel_byte(0.15F + 0.80F * ratio),
        channel_byte(0.85F - 0.55F * ratio),
        channel_byte(0.95F - 0.75F * ratio),
        130,
    };
}

DebugVisuals::DebugVisuals() noexcept {
    for (std::size_t index = 0; index < debug_channel_count; ++index) {
        const auto channel = static_cast<DebugChannel>(index);
        selected_[index] = debug_channel_implemented(channel) &&
                           channel != DebugChannel::navigation_grid &&
                           channel != DebugChannel::navigation_path;
    }
}

bool DebugVisuals::enabled() const noexcept { return enabled_; }
void DebugVisuals::set_enabled(const bool enabled) noexcept { enabled_ = enabled; }
void DebugVisuals::toggle() noexcept { enabled_ = !enabled_; }

bool DebugVisuals::channel_selected(const DebugChannel channel) const noexcept {
    return selected_[index_of(channel)];
}

void DebugVisuals::set_channel_selected(
    const DebugChannel channel,
    const bool selected
) noexcept {
    selected_[index_of(channel)] = selected && debug_channel_implemented(channel);
}

bool DebugVisuals::draws(const DebugChannel channel) const noexcept {
    return enabled_ && selected_[index_of(channel)] && debug_channel_implemented(channel);
}

std::size_t DebugVisuals::drawn_channel_count() const noexcept {
    std::size_t count = 0;
    for (std::size_t index = 0; index < debug_channel_count; ++index) {
        count += draws(static_cast<DebugChannel>(index)) ? 1U : 0U;
    }
    return count;
}

} // namespace ic2d
