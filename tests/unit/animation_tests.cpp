#include <doctest/doctest.h>

#include "ic2d/animation.hpp"

#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

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

TEST_CASE("loop timing and events") {
    ic2d::AnimationPlayer player{{clip("walk", ic2d::AnimationLoopMode::loop)}, "walk"};
    CHECK_MESSAGE((player.sample().frame_index == 0), "Playback must start on frame zero.");
    CHECK_MESSAGE((player.advance(1).empty()), "A partial frame must not emit events.");
    const auto events = player.advance(1);
    CHECK_MESSAGE((player.sample().frame_index == 1), "Exact duration must enter the next frame.");
    CHECK_MESSAGE(
        (events.size() == 1 && events.front().name == "step" && events.front().frame_index == 1),
        "Entering an authored event frame must emit one owned event.");
    static_cast<void>(player.advance(4));
    CHECK_MESSAGE((player.sample().frame_index == 0), "Loop playback must wrap to frame zero.");
}

TEST_CASE("once holds the final frame") {
    ic2d::AnimationPlayer player{{clip("hit", ic2d::AnimationLoopMode::once, 1)}, "hit"};
    static_cast<void>(player.advance(20));
    const ic2d::AnimationSample sample = player.sample();
    CHECK_MESSAGE((sample.frame_index == 2 && sample.finished),
                  "Once playback must finish while holding its final frame.");
    CHECK_MESSAGE((player.advance(20).empty()), "Finished playback must remain stable.");
}

TEST_CASE("sample retains authored horizontal flip") {
    ic2d::AnimationClip flipped = clip("west", ic2d::AnimationLoopMode::loop, 1);
    flipped.frames.front().flip_x = true;
    flipped.frames.front().presentation_scale = 1.75F;
    ic2d::AnimationPlayer player{{std::move(flipped)}, "west"};
    CHECK_MESSAGE((player.sample().flip_x),
                  "Animation samples must retain authored horizontal presentation flips.");
    CHECK_MESSAGE((player.sample().presentation_scale == 1.75F),
                  "Animation samples must retain authored per-frame presentation scale.");
    static_cast<void>(player.advance(1));
    CHECK_MESSAGE((!player.sample().flip_x),
                  "Horizontal flipping must remain a per-frame presentation property.");
    CHECK_MESSAGE((player.sample().presentation_scale == 1.0F),
                  "Presentation scaling must remain a per-frame property.");
}

TEST_CASE("ping pong handles large advances") {
    ic2d::AnimationPlayer player{{clip("idle", ic2d::AnimationLoopMode::ping_pong, 1)}, "idle"};
    static_cast<void>(player.advance(4));
    CHECK_MESSAGE((player.sample().frame_index == 0),
                  "Ping-pong playback must traverse 0, 1, 2, 1, 0.");
    static_cast<void>(player.advance(5));
    CHECK_MESSAGE((player.sample().frame_index == 1),
                  "Large advances must preserve deterministic ping-pong direction.");
}

TEST_CASE("play pause and reset") {
    std::vector<ic2d::AnimationClip> clips;
    clips.push_back(clip("idle", ic2d::AnimationLoopMode::loop, 1));
    clips.push_back(clip("move", ic2d::AnimationLoopMode::loop, 1));
    ic2d::AnimationPlayer player{std::move(clips), "idle"};

    CHECK_MESSAGE((player.play("move")), "Selecting a different clip must report a change.");
    static_cast<void>(player.advance(1));
    CHECK_MESSAGE((player.sample().clip_id == "move" && player.sample().frame_index == 1),
                  "The selected clip must advance independently.");
    CHECK_MESSAGE((!player.play("move")),
                  "Selecting the active clip without restart must preserve time.");
    player.set_paused(true);
    static_cast<void>(player.advance(20));
    CHECK_MESSAGE((player.sample().frame_index == 1 && player.sample().paused),
                  "Paused playback must not consume ticks.");
    player.reset();
    CHECK_MESSAGE((player.sample().clip_id == "idle" && player.sample().frame_index == 0 &&
                   !player.sample().paused),
                  "Reset must restore and unpause the authored initial clip.");

    bool rejected = false;
    try {
        static_cast<void>(player.play("missing"));
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    CHECK_MESSAGE((rejected), "Selecting an unknown clip must fail explicitly.");
}

TEST_CASE("direction changes preserve cycle phase") {
    std::vector<ic2d::AnimationClip> clips;
    clips.push_back(clip("walk-south", ic2d::AnimationLoopMode::loop, 2));
    clips.push_back(clip("walk-east", ic2d::AnimationLoopMode::loop, 1));
    ic2d::AnimationPlayer player{std::move(clips), "walk-south"};

    static_cast<void>(player.advance(3));
    CHECK_MESSAGE((player.sample().frame_index == 1),
                  "The source gait must reach the middle of its cycle.");
    CHECK_MESSAGE((player.play("walk-east", ic2d::AnimationTransitionMode::preserve_cycle_phase)),
                  "Changing locomotion direction must select the new clip.");
    CHECK_MESSAGE((player.sample().clip_id == "walk-east" && player.sample().frame_index == 1),
                  "A directional transition must retain normalized gait phase.");

    CHECK_MESSAGE((player.play("walk-south")),
                  "The legacy transition path must still select a different clip.");
    CHECK_MESSAGE((player.sample().frame_index == 0),
                  "The legacy transition path must continue to restart playback.");
}

TEST_CASE("invalid definitions fail at construction") {
    bool rejected = false;
    try {
        static_cast<void>(ic2d::AnimationPlayer{{ic2d::AnimationClip{.id = "empty"}}, "empty"});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK_MESSAGE((rejected), "Clips without frames must fail before playback.");

    rejected = false;
    try {
        static_cast<void>(
            ic2d::AnimationPlayer{{clip("known", ic2d::AnimationLoopMode::loop)}, "missing"});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK_MESSAGE((rejected), "An unknown initial clip must fail before playback.");
}

} // namespace
