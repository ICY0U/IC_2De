#include <doctest/doctest.h>

#include "ic2d/input.hpp"

#include <cmath>
#include <string_view>

namespace {

TEST_CASE("button transitions") {
    ic2d::InputTracker tracker;
    const auto pressed = tracker.update(ic2d::InputSample{.pause = true});
    CHECK_MESSAGE((pressed.pause.down), "Pressed action must be down.");
    CHECK_MESSAGE((pressed.pause.pressed), "First down sample must produce a pressed edge.");
    CHECK_MESSAGE((!pressed.pause.released), "Pressed action must not also be released.");

    const auto held = tracker.update(ic2d::InputSample{.pause = true});
    CHECK_MESSAGE((held.pause.down), "Held action must remain down.");
    CHECK_MESSAGE((!held.pause.pressed), "Held action must not repeat the pressed edge.");

    const auto released = tracker.update(ic2d::InputSample{});
    CHECK_MESSAGE((!released.pause.down), "Released action must not be down.");
    CHECK_MESSAGE((released.pause.released), "First up sample must produce a released edge.");
}

TEST_CASE("axis is normalized") {
    ic2d::InputTracker tracker;
    const auto positive = tracker.update(ic2d::InputSample{.move_horizontal = 2.0F});
    const auto negative =
        tracker.update(ic2d::InputSample{.move_horizontal = -3.0F, .move_depth = 0.0F});
    const auto diagonal =
        tracker.update(ic2d::InputSample{.move_horizontal = 1.0F, .move_depth = 1.0F});
    CHECK_MESSAGE((std::abs(positive.move_horizontal - 1.0F) < 0.0001F),
                  "Positive action axis must be clamped to one.");
    CHECK_MESSAGE((std::abs(negative.move_horizontal + 1.0F) < 0.0001F),
                  "Negative action axis must be clamped to minus one.");
    CHECK_MESSAGE((std::abs(std::sqrt(diagonal.move_horizontal * diagonal.move_horizontal +
                                      diagonal.move_depth * diagonal.move_depth) -
                            1.0F) < 0.0001F),
                  "Diagonal movement must be normalized to prevent a speed boost.");
}

TEST_CASE("reset clears transition history") {
    ic2d::InputTracker tracker;
    static_cast<void>(tracker.update(ic2d::InputSample{.reset = true}));
    tracker.reset();
    const auto frame = tracker.update(ic2d::InputSample{.reset = true});
    CHECK_MESSAGE((frame.reset.pressed), "Reset tracker must forget previous held actions.");
}

TEST_CASE("render mode transition") {
    ic2d::InputTracker tracker;
    const auto first = tracker.update(ic2d::InputSample{.cycle_render_pacing = true});
    const auto held = tracker.update(ic2d::InputSample{.cycle_render_pacing = true});
    CHECK_MESSAGE((first.cycle_render_pacing.pressed),
                  "Render pacing action must emit one pressed edge.");
    CHECK_MESSAGE((!held.cycle_render_pacing.pressed),
                  "Held render pacing action must not cycle repeatedly.");
}

TEST_CASE("post process toggle transition") {
    ic2d::InputTracker tracker;
    const auto first = tracker.update(ic2d::InputSample{.toggle_post_process = true});
    const auto held = tracker.update(ic2d::InputSample{.toggle_post_process = true});
    const auto released = tracker.update(ic2d::InputSample{});
    CHECK_MESSAGE((first.toggle_post_process.pressed),
                  "Post-process action must emit one pressed edge.");
    CHECK_MESSAGE((!held.toggle_post_process.pressed),
                  "Held post-process action must not repeat the pressed edge.");
    CHECK_MESSAGE((released.toggle_post_process.released),
                  "Post-process action must emit a release edge.");
}

TEST_CASE("gameplay actions have independent edges") {
    for (const ic2d::GameplayAction selected : ic2d::gameplay_actions) {
        ic2d::InputTracker tracker;
        ic2d::InputSample sample;
        sample.gameplay.set(selected, true);

        const auto pressed = tracker.update(sample);
        const auto held = tracker.update(sample);
        const auto released = tracker.update(ic2d::InputSample{});

        for (const ic2d::GameplayAction observed : ic2d::gameplay_actions) {
            const bool is_selected = observed == selected;
            CHECK_MESSAGE((pressed.gameplay.action(observed).down == is_selected),
                          "Only the selected gameplay action may be down.");
            CHECK_MESSAGE((pressed.gameplay.action(observed).pressed == is_selected),
                          "Only the selected gameplay action may emit a pressed edge.");
        }
        CHECK_MESSAGE((!held.gameplay.action(selected).pressed),
                      "A held gameplay action must not repeat its pressed edge.");
        CHECK_MESSAGE((released.gameplay.action(selected).released),
                      "A gameplay action must emit one release edge.");
    }
}

TEST_CASE("aim state is bounded without losing pointer state") {
    ic2d::InputTracker tracker;
    ic2d::InputSample sample;
    sample.gameplay.aim = {
        .horizontal = 3.0F,
        .depth = 4.0F,
        .pointer_screen_x = 321.0F,
        .pointer_screen_y = 123.0F,
        .pointer_active = true,
    };

    const auto frame = tracker.update(sample);
    CHECK_MESSAGE((std::abs(frame.gameplay.aim.horizontal - 0.6F) < 0.0001F),
                  "Aim X must be normalized when the adapter exceeds unit length.");
    CHECK_MESSAGE((std::abs(frame.gameplay.aim.depth - 0.8F) < 0.0001F),
                  "Aim depth must be normalized when the adapter exceeds unit length.");
    CHECK_MESSAGE((frame.gameplay.aim.pointer_screen_x == 321.0F &&
                   frame.gameplay.aim.pointer_screen_y == 123.0F),
                  "Input tracking must preserve pointer coordinates for later world projection.");
    CHECK_MESSAGE((frame.gameplay.aim.pointer_active),
                  "Input tracking must preserve the adapter's active aim source.");
}

TEST_CASE("reset clears gameplay transition history") {
    ic2d::InputTracker tracker;
    ic2d::InputSample sample;
    sample.gameplay.set(ic2d::GameplayAction::dodge, true);
    static_cast<void>(tracker.update(sample));

    tracker.reset();
    const auto frame = tracker.update(sample);
    CHECK_MESSAGE((frame.gameplay.action(ic2d::GameplayAction::dodge).pressed),
                  "Reset must forget held gameplay actions as well as development actions.");
}

} // namespace
