#include "ic2d/animation.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] ic2d::AnimationClip clip(std::string id, const ic2d::AnimationLoopMode loop_mode,
                                       const std::uint32_t duration = 2) {
    return {
        .id = std::move(id),
        .loop_mode = loop_mode,
        .frames =
            {
                {.source = {0.0F, 0.0F, 16.0F, 16.0F}, .duration_ticks = duration},
                {.source = {16.0F, 0.0F, 16.0F, 16.0F},
                 .duration_ticks = duration,
                 .events = {"step"}},
                {.source = {32.0F, 0.0F, 16.0F, 16.0F}, .duration_ticks = duration},
            },
    };
}

void test_loop_timing_and_events() {
    ic2d::AnimationPlayer player{{clip("walk", ic2d::AnimationLoopMode::loop)}, "walk"};
    expect(player.sample().frame_index == 0, "Playback must start on frame zero.");
    expect(player.advance(1).empty(), "A partial frame must not emit events.");
    const auto events = player.advance(1);
    expect(player.sample().frame_index == 1, "Exact duration must enter the next frame.");
    expect(events.size() == 1 && events.front().name == "step" && events.front().frame_index == 1,
           "Entering an authored event frame must emit one owned event.");
    static_cast<void>(player.advance(4));
    expect(player.sample().frame_index == 0, "Loop playback must wrap to frame zero.");
}

void test_once_holds_the_final_frame() {
    ic2d::AnimationPlayer player{{clip("hit", ic2d::AnimationLoopMode::once, 1)}, "hit"};
    static_cast<void>(player.advance(20));
    const ic2d::AnimationSample sample = player.sample();
    expect(sample.frame_index == 2 && sample.finished,
           "Once playback must finish while holding its final frame.");
    expect(player.advance(20).empty(), "Finished playback must remain stable.");
}

void test_sample_retains_authored_horizontal_flip() {
    ic2d::AnimationClip flipped = clip("west", ic2d::AnimationLoopMode::loop, 1);
    flipped.frames.front().flip_x = true;
    ic2d::AnimationPlayer player{{std::move(flipped)}, "west"};
    expect(player.sample().flip_x,
           "Animation samples must retain authored horizontal presentation flips.");
    static_cast<void>(player.advance(1));
    expect(!player.sample().flip_x,
           "Horizontal flipping must remain a per-frame presentation property.");
}

void test_ping_pong_handles_large_advances() {
    ic2d::AnimationPlayer player{{clip("idle", ic2d::AnimationLoopMode::ping_pong, 1)}, "idle"};
    static_cast<void>(player.advance(4));
    expect(player.sample().frame_index == 0, "Ping-pong playback must traverse 0, 1, 2, 1, 0.");
    static_cast<void>(player.advance(5));
    expect(player.sample().frame_index == 1,
           "Large advances must preserve deterministic ping-pong direction.");
}

void test_play_pause_and_reset() {
    std::vector<ic2d::AnimationClip> clips;
    clips.push_back(clip("idle", ic2d::AnimationLoopMode::loop, 1));
    clips.push_back(clip("move", ic2d::AnimationLoopMode::loop, 1));
    ic2d::AnimationPlayer player{std::move(clips), "idle"};

    expect(player.play("move"), "Selecting a different clip must report a change.");
    static_cast<void>(player.advance(1));
    expect(player.sample().clip_id == "move" && player.sample().frame_index == 1,
           "The selected clip must advance independently.");
    expect(!player.play("move"), "Selecting the active clip without restart must preserve time.");
    player.set_paused(true);
    static_cast<void>(player.advance(20));
    expect(player.sample().frame_index == 1 && player.sample().paused,
           "Paused playback must not consume ticks.");
    player.reset();
    expect(player.sample().clip_id == "idle" && player.sample().frame_index == 0 &&
               !player.sample().paused,
           "Reset must restore and unpause the authored initial clip.");

    bool rejected = false;
    try {
        static_cast<void>(player.play("missing"));
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    expect(rejected, "Selecting an unknown clip must fail explicitly.");
}

void test_direction_changes_preserve_cycle_phase() {
    std::vector<ic2d::AnimationClip> clips;
    clips.push_back(clip("walk-south", ic2d::AnimationLoopMode::loop, 2));
    clips.push_back(clip("walk-east", ic2d::AnimationLoopMode::loop, 1));
    ic2d::AnimationPlayer player{std::move(clips), "walk-south"};

    static_cast<void>(player.advance(3));
    expect(player.sample().frame_index == 1, "The source gait must reach the middle of its cycle.");
    expect(player.play("walk-east", ic2d::AnimationTransitionMode::preserve_cycle_phase),
           "Changing locomotion direction must select the new clip.");
    expect(player.sample().clip_id == "walk-east" && player.sample().frame_index == 1,
           "A directional transition must retain normalized gait phase.");

    expect(player.play("walk-south"),
           "The legacy transition path must still select a different clip.");
    expect(player.sample().frame_index == 0,
           "The legacy transition path must continue to restart playback.");
}

void test_invalid_definitions_fail_at_construction() {
    bool rejected = false;
    try {
        static_cast<void>(ic2d::AnimationPlayer{{ic2d::AnimationClip{.id = "empty"}}, "empty"});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "Clips without frames must fail before playback.");

    rejected = false;
    try {
        static_cast<void>(
            ic2d::AnimationPlayer{{clip("known", ic2d::AnimationLoopMode::loop)}, "missing"});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "An unknown initial clip must fail before playback.");
}

} // namespace

int main() {
    test_loop_timing_and_events();
    test_once_holds_the_final_frame();
    test_sample_retains_authored_horizontal_flip();
    test_ping_pong_handles_large_advances();
    test_play_pause_and_reset();
    test_direction_changes_preserve_cycle_phase();
    test_invalid_definitions_fail_at_construction();

    if (failures == 0) {
        std::cout << "Animation tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
