#include "ic2d/input.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_button_transitions() {
    ic2d::InputTracker tracker;
    const auto pressed = tracker.update(ic2d::InputSample{.pause = true});
    expect(pressed.pause.down, "Pressed action must be down.");
    expect(pressed.pause.pressed, "First down sample must produce a pressed edge.");
    expect(!pressed.pause.released, "Pressed action must not also be released.");

    const auto held = tracker.update(ic2d::InputSample{.pause = true});
    expect(held.pause.down, "Held action must remain down.");
    expect(!held.pause.pressed, "Held action must not repeat the pressed edge.");

    const auto released = tracker.update(ic2d::InputSample{});
    expect(!released.pause.down, "Released action must not be down.");
    expect(released.pause.released, "First up sample must produce a released edge.");
}

void test_axis_is_normalized() {
    ic2d::InputTracker tracker;
    const auto positive = tracker.update(ic2d::InputSample{.move_horizontal = 2.0F});
    const auto negative = tracker.update(ic2d::InputSample{.move_horizontal = -3.0F, .move_depth = 0.0F});
    const auto diagonal = tracker.update(ic2d::InputSample{.move_horizontal = 1.0F, .move_depth = 1.0F});
    expect(std::abs(positive.move_horizontal - 1.0F) < 0.0001F,
           "Positive action axis must be clamped to one.");
    expect(std::abs(negative.move_horizontal + 1.0F) < 0.0001F,
           "Negative action axis must be clamped to minus one.");
    expect(std::abs(std::sqrt(diagonal.move_horizontal * diagonal.move_horizontal +
                             diagonal.move_depth * diagonal.move_depth) - 1.0F) < 0.0001F,
           "Diagonal movement must be normalized to prevent a speed boost.");
}

void test_reset_clears_transition_history() {
    ic2d::InputTracker tracker;
    static_cast<void>(tracker.update(ic2d::InputSample{.reset = true}));
    tracker.reset();
    const auto frame = tracker.update(ic2d::InputSample{.reset = true});
    expect(frame.reset.pressed, "Reset tracker must forget previous held actions.");
}

void test_render_mode_transition() {
    ic2d::InputTracker tracker;
    const auto first = tracker.update(ic2d::InputSample{.cycle_render_pacing = true});
    const auto held = tracker.update(ic2d::InputSample{.cycle_render_pacing = true});
    expect(first.cycle_render_pacing.pressed, "Render pacing action must emit one pressed edge.");
    expect(!held.cycle_render_pacing.pressed, "Held render pacing action must not cycle repeatedly.");
}

void test_post_process_toggle_transition() {
    ic2d::InputTracker tracker;
    const auto first = tracker.update(ic2d::InputSample{.toggle_post_process = true});
    const auto held = tracker.update(ic2d::InputSample{.toggle_post_process = true});
    const auto released = tracker.update(ic2d::InputSample{});
    expect(first.toggle_post_process.pressed,
           "Post-process action must emit one pressed edge.");
    expect(!held.toggle_post_process.pressed,
           "Held post-process action must not repeat the pressed edge.");
    expect(released.toggle_post_process.released,
           "Post-process action must emit a release edge.");
}

} // namespace

int main() {
    test_button_transitions();
    test_axis_is_normalized();
    test_reset_clears_transition_history();
    test_render_mode_transition();
    test_post_process_toggle_transition();

    if (failures == 0) {
        std::cout << "All input tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
