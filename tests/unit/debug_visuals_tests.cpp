#include "ic2d/debug_visuals.hpp"

#include <cstddef>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_master_switch_hides_every_channel() {
    ic2d::DebugVisuals visuals;
    expect(visuals.enabled() && visuals.drawn_channel_count() > 0,
           "Development debug visuals must start enabled so the dev build looks unchanged.");
    expect(visuals.draws(ic2d::DebugChannel::collision_shapes) &&
               visuals.draws(ic2d::DebugChannel::world_grid),
           "Implemented channels must be selected by default.");

    visuals.toggle();
    expect(!visuals.enabled() && visuals.drawn_channel_count() == 0,
           "The master switch must hide every debug channel at once.");
    expect(visuals.channel_selected(ic2d::DebugChannel::collision_shapes),
           "Hiding debug visuals must not forget which channels were chosen.");

    visuals.toggle();
    expect(visuals.draws(ic2d::DebugChannel::collision_shapes),
           "Restoring the master switch must restore the previous channel selection.");
}

void test_channels_are_independent() {
    ic2d::DebugVisuals visuals;
    const std::size_t before = visuals.drawn_channel_count();
    visuals.set_channel_selected(ic2d::DebugChannel::trigger_volumes, false);
    expect(!visuals.draws(ic2d::DebugChannel::trigger_volumes),
           "A deselected channel must stop drawing.");
    expect(visuals.draws(ic2d::DebugChannel::collision_shapes),
           "Deselecting one channel must not affect another.");
    expect(visuals.drawn_channel_count() + 1 == before,
           "The drawn channel count must follow individual selection.");
}

void test_unimplemented_channels_never_draw() {
    ic2d::DebugVisuals visuals;
    expect(!ic2d::debug_channel_implemented(ic2d::DebugChannel::lights),
           "The lights channel has no engine module yet and must report as unavailable.");
    expect(!visuals.channel_selected(ic2d::DebugChannel::lights),
           "An unavailable channel must not be selected by default.");
    visuals.set_channel_selected(ic2d::DebugChannel::lights, true);
    expect(!visuals.channel_selected(ic2d::DebugChannel::lights) &&
               !visuals.draws(ic2d::DebugChannel::lights),
           "Selecting an unavailable channel must not pretend it renders.");
}

void test_every_channel_is_named() {
    for (std::size_t index = 0; index < ic2d::debug_channel_count; ++index) {
        const auto channel = static_cast<ic2d::DebugChannel>(index);
        expect(ic2d::debug_channel_name(channel) != "Unknown",
               "Every debug channel must have a display name for tools.");
    }
}

void test_elevation_tint_ramps_with_height() {
    const ic2d::ColorRgba8 low = ic2d::debug_elevation_tint(0.0F, 24.0F);
    const ic2d::ColorRgba8 high = ic2d::debug_elevation_tint(24.0F, 24.0F);
    const ic2d::ColorRgba8 middle = ic2d::debug_elevation_tint(12.0F, 24.0F);
    expect(low.red < middle.red && middle.red < high.red,
           "Elevation shading must warm up as height increases.");
    expect(low.blue > middle.blue && middle.blue > high.blue,
           "Elevation shading must cool down as height decreases.");
    expect(low.alpha == high.alpha && low.alpha > 0,
           "Elevation shading must stay translucent at every height.");

    const ic2d::ColorRgba8 above = ic2d::debug_elevation_tint(96.0F, 24.0F);
    const ic2d::ColorRgba8 below = ic2d::debug_elevation_tint(-40.0F, 24.0F);
    expect(above.red == high.red && above.blue == high.blue,
           "Elevation above the authored maximum must clamp to the top of the ramp.");
    expect(below.red == low.red && below.blue == low.blue,
           "Elevation below zero must clamp to the bottom of the ramp.");
}

void test_elevation_tint_survives_degenerate_input() {
    const ic2d::ColorRgba8 zero_span = ic2d::debug_elevation_tint(10.0F, 0.0F);
    expect(zero_span.alpha > 0, "A flat scene must still produce a usable elevation tint.");

    const float infinity = std::numeric_limits<float>::infinity();
    const ic2d::ColorRgba8 not_a_number = ic2d::debug_elevation_tint(infinity, infinity);
    const ic2d::ColorRgba8 base = ic2d::debug_elevation_tint(0.0F, 1.0F);
    expect(not_a_number.red == base.red && not_a_number.green == base.green &&
               not_a_number.blue == base.blue,
           "A non-finite elevation ratio must fall back to the bottom of the ramp.");
}

} // namespace

int main() {
    test_master_switch_hides_every_channel();
    test_channels_are_independent();
    test_unimplemented_channels_never_draw();
    test_every_channel_is_named();
    test_elevation_tint_ramps_with_height();
    test_elevation_tint_survives_degenerate_input();

    if (failures == 0) {
        std::cout << "Debug visuals tests passed.\n";
        return 0;
    }
    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
