#include <doctest/doctest.h>

#include "ic2d/debug_visuals.hpp"

#include <cstddef>
#include <limits>
#include <string_view>

namespace {

TEST_CASE("master switch hides every channel") {
    ic2d::DebugVisuals visuals;
    CHECK_MESSAGE((visuals.enabled() && visuals.drawn_channel_count() > 0),
                  "Development debug visuals must start enabled so the dev build looks unchanged.");
    CHECK_MESSAGE((visuals.draws(ic2d::DebugChannel::collision_shapes) &&
                   visuals.draws(ic2d::DebugChannel::world_grid)),
                  "Implemented channels must be selected by default.");

    visuals.toggle();
    CHECK_MESSAGE((!visuals.enabled() && visuals.drawn_channel_count() == 0),
                  "The master switch must hide every debug channel at once.");
    CHECK_MESSAGE((visuals.channel_selected(ic2d::DebugChannel::collision_shapes)),
                  "Hiding debug visuals must not forget which channels were chosen.");

    visuals.toggle();
    CHECK_MESSAGE((visuals.draws(ic2d::DebugChannel::collision_shapes)),
                  "Restoring the master switch must restore the previous channel selection.");
}

TEST_CASE("channels are independent") {
    ic2d::DebugVisuals visuals;
    const std::size_t before = visuals.drawn_channel_count();
    visuals.set_channel_selected(ic2d::DebugChannel::trigger_volumes, false);
    CHECK_MESSAGE((!visuals.draws(ic2d::DebugChannel::trigger_volumes)),
                  "A deselected channel must stop drawing.");
    CHECK_MESSAGE((visuals.draws(ic2d::DebugChannel::collision_shapes)),
                  "Deselecting one channel must not affect another.");
    CHECK_MESSAGE((visuals.drawn_channel_count() + 1 == before),
                  "The drawn channel count must follow individual selection.");
}

TEST_CASE("unimplemented channels never draw") {
    ic2d::DebugVisuals visuals;
    CHECK_MESSAGE((!ic2d::debug_channel_implemented(ic2d::DebugChannel::lights)),
                  "The lights channel has no engine module yet and must report as unavailable.");
    CHECK_MESSAGE((!visuals.channel_selected(ic2d::DebugChannel::lights)),
                  "An unavailable channel must not be selected by default.");
    visuals.set_channel_selected(ic2d::DebugChannel::lights, true);
    CHECK_MESSAGE((!visuals.channel_selected(ic2d::DebugChannel::lights) &&
                   !visuals.draws(ic2d::DebugChannel::lights)),
                  "Selecting an unavailable channel must not pretend it renders.");
}

TEST_CASE("navigation grid is available but opt in") {
    ic2d::DebugVisuals visuals;
    CHECK_MESSAGE((ic2d::debug_channel_implemented(ic2d::DebugChannel::navigation_grid)),
                  "The navigation grid must be an honest implemented debug channel.");
    CHECK_MESSAGE((!visuals.channel_selected(ic2d::DebugChannel::navigation_grid)),
                  "The dense navigation overlay must not obscure ordinary editor startup.");
    visuals.set_channel_selected(ic2d::DebugChannel::navigation_grid, true);
    CHECK_MESSAGE((visuals.draws(ic2d::DebugChannel::navigation_grid)),
                  "The editor must be able to enable the read-only navigation overlay.");
}

TEST_CASE("navigation path is available but opt in") {
    ic2d::DebugVisuals visuals;
    CHECK_MESSAGE((ic2d::debug_channel_implemented(ic2d::DebugChannel::navigation_path)),
                  "The copied A-star path must be an honest implemented debug channel.");
    CHECK_MESSAGE((!visuals.channel_selected(ic2d::DebugChannel::navigation_path)),
                  "The navigation path must not appear until a developer requests it.");
    visuals.set_channel_selected(ic2d::DebugChannel::navigation_path, true);
    CHECK_MESSAGE((visuals.draws(ic2d::DebugChannel::navigation_path)),
                  "The editor must be able to enable the focused path overlay.");
    CHECK_MESSAGE((ic2d::debug_channel_name(ic2d::DebugChannel::stats_overlay) == "Compact HUD"),
                  "The viewport text channel must describe its compact presentation honestly.");
}

TEST_CASE("every channel is named") {
    for (std::size_t index = 0; index < ic2d::debug_channel_count; ++index) {
        const auto channel = static_cast<ic2d::DebugChannel>(index);
        CHECK_MESSAGE((ic2d::debug_channel_name(channel) != "Unknown"),
                      "Every debug channel must have a display name for tools.");
    }
}

TEST_CASE("elevation tint ramps with height") {
    const ic2d::ColorRgba8 low = ic2d::debug_elevation_tint(0.0F, 24.0F);
    const ic2d::ColorRgba8 high = ic2d::debug_elevation_tint(24.0F, 24.0F);
    const ic2d::ColorRgba8 middle = ic2d::debug_elevation_tint(12.0F, 24.0F);
    CHECK_MESSAGE((low.red < middle.red && middle.red < high.red),
                  "Elevation shading must warm up as height increases.");
    CHECK_MESSAGE((low.blue > middle.blue && middle.blue > high.blue),
                  "Elevation shading must cool down as height decreases.");
    CHECK_MESSAGE((low.alpha == high.alpha && low.alpha > 0),
                  "Elevation shading must stay translucent at every height.");

    const ic2d::ColorRgba8 above = ic2d::debug_elevation_tint(96.0F, 24.0F);
    const ic2d::ColorRgba8 below = ic2d::debug_elevation_tint(-40.0F, 24.0F);
    CHECK_MESSAGE((above.red == high.red && above.blue == high.blue),
                  "Elevation above the authored maximum must clamp to the top of the ramp.");
    CHECK_MESSAGE((below.red == low.red && below.blue == low.blue),
                  "Elevation below zero must clamp to the bottom of the ramp.");
}

TEST_CASE("elevation tint survives degenerate input") {
    const ic2d::ColorRgba8 zero_span = ic2d::debug_elevation_tint(10.0F, 0.0F);
    CHECK_MESSAGE((zero_span.alpha > 0),
                  "A flat scene must still produce a usable elevation tint.");

    const float infinity = std::numeric_limits<float>::infinity();
    const ic2d::ColorRgba8 not_a_number = ic2d::debug_elevation_tint(infinity, infinity);
    const ic2d::ColorRgba8 base = ic2d::debug_elevation_tint(0.0F, 1.0F);
    CHECK_MESSAGE((not_a_number.red == base.red && not_a_number.green == base.green &&
                   not_a_number.blue == base.blue),
                  "A non-finite elevation ratio must fall back to the bottom of the ramp.");
}

} // namespace
