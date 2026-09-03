#include <doctest/doctest.h>

#include "ic2d/render2d.hpp"

#include <cmath>
#include <stdexcept>
#include <string_view>

namespace {

TEST_CASE("submission requires begin") {
    ic2d::RenderQueue2D queue;
    bool threw = false;
    try {
        queue.submit({.stable_id = 1});
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK_MESSAGE((threw), "Submitting outside a frame must fail.");

    threw = false;
    try {
        queue.submit_ground(ic2d::GroundQuadSubmission2D{});
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK_MESSAGE((threw), "Submitting ground outside a frame must fail.");
}

TEST_CASE("ground quads are immutable and ordered") {
    ic2d::RenderQueue2D queue;
    queue.begin({});
    queue.submit_ground(ic2d::GroundQuadSubmission2D{
        .stable_id = 2,
        .points = {{{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}}},
    });
    queue.submit_ground(ic2d::GroundQuadSubmission2D{
        .stable_id = 1,
        .points = {{{20.0F, 20.0F}, {30.0F, 20.0F}, {30.0F, 30.0F}, {20.0F, 30.0F}}},
    });
    const auto frame = queue.finish();
    CHECK_MESSAGE((frame.ground_quads().size() == 2),
                  "Finished frame must retain every ground submission.");
    CHECK_MESSAGE((frame.ground_quads().size() == 2 && frame.ground_quads()[0].stable_id == 2 &&
                   frame.ground_quads()[1].stable_id == 1),
                  "Ground surfaces must retain authored submission order.");
}

TEST_CASE("frame orders layer then depth then id") {
    ic2d::RenderQueue2D queue;
    queue.begin({});
    queue.submit({.stable_id = 30, .position = {0.0F, 10.0F}, .sort_depth = 40.0F, .layer = 1});
    queue.submit({.stable_id = 20, .position = {0.0F, 80.0F}, .sort_depth = 20.0F, .layer = 1});
    queue.submit({.stable_id = 10, .position = {0.0F, 100.0F}, .sort_depth = 100.0F, .layer = -1});
    const auto frame = queue.finish();
    const auto sprites = frame.sprites();

    CHECK_MESSAGE((sprites.size() == 3), "Finished frame must contain every submission.");
    CHECK_MESSAGE((sprites.size() == 3 && sprites[0].stable_id == 10 &&
                   sprites[1].stable_id == 20 && sprites[2].stable_id == 30),
                  "Frame must sort by layer, then projected depth, then stable ID.");
}

TEST_CASE("non finite sort depth is rejected") {
    ic2d::RenderQueue2D queue;
    queue.begin({});
    bool threw = false;
    try {
        queue.submit({.stable_id = 1, .sort_depth = std::nanf("")});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK_MESSAGE((threw), "Non-finite sprite depth must be rejected before sorting.");
}

TEST_CASE("finished frame is independent of queue reuse") {
    ic2d::RenderQueue2D queue;
    queue.begin({.center = {100.0F, 200.0F}, .zoom = 2.0F});
    queue.submit({.stable_id = 1});
    const auto first_frame = queue.finish();

    queue.begin({});
    queue.submit({.stable_id = 2});
    const auto second_frame = queue.finish();

    CHECK_MESSAGE((first_frame.sprites().size() == 1 && first_frame.sprites()[0].stable_id == 1),
                  "Reusing the queue must not mutate a finished frame.");
    CHECK_MESSAGE((second_frame.sprites().size() == 1 && second_frame.sprites()[0].stable_id == 2),
                  "Reused queue must build the next frame independently.");
    CHECK_MESSAGE((std::abs(first_frame.camera().zoom - 2.0F) < 0.001F),
                  "Finished frame must retain its camera description.");
}

TEST_CASE("invalid camera is rejected") {
    ic2d::RenderQueue2D queue;
    bool threw = false;
    try {
        queue.begin({.zoom = 0.0F});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK_MESSAGE((threw), "Zero camera zoom must be rejected.");
}

} // namespace

int run_projection25d_tests();

TEST_CASE("offscreen sprites are culled before ordering") {
    // Sorting a frame and then discarding most of it in the renderer pays the
    // ordering cost for sprites that never appear, so the queue rejects them
    // as they arrive and reports what it dropped.
    ic2d::RenderQueue2D queue;
    queue.begin({.center = {0.0F, 0.0F}, .rotation_degrees = 0.0F, .zoom = 1.0F}, 100, 100);
    queue.submit({.stable_id = 1, .position = {0.0F, 0.0F}, .size = {10.0F, 10.0F}});
    queue.submit({.stable_id = 2, .position = {900.0F, 0.0F}, .size = {10.0F, 10.0F}});
    queue.submit({.stable_id = 3, .position = {0.0F, -900.0F}, .size = {10.0F, 10.0F}});
    const ic2d::RenderFrame2D frame = queue.finish();
    CHECK_MESSAGE((frame.submitted_sprites() == 3), "Every submission is counted.");
    CHECK_MESSAGE((frame.culled_sprites() == 2), "Both offscreen sprites are culled.");
    CHECK_MESSAGE((frame.sprites().size() == 1), "Only the visible sprite reaches the frame.");
    CHECK_MESSAGE((frame.sprites()[0].stable_id == 1), "The surviving sprite is the visible one.");

    // A caller that only cares about ordering opts out with no viewport.
    ic2d::RenderQueue2D unbounded;
    unbounded.begin({.center = {0.0F, 0.0F}, .rotation_degrees = 0.0F, .zoom = 1.0F});
    unbounded.submit({.stable_id = 9, .position = {9000.0F, 9000.0F}, .size = {10.0F, 10.0F}});
    const ic2d::RenderFrame2D everything = unbounded.finish();
    CHECK_MESSAGE((everything.sprites().size() == 1 && everything.culled_sprites() == 0),
                  "A zero-sized viewport culls nothing.");
}
