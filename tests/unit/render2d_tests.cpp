#include "ic2d/render2d.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_submission_requires_begin() {
    ic2d::RenderQueue2D queue;
    bool threw = false;
    try {
        queue.submit({.stable_id = 1});
    } catch (const std::logic_error&) {
        threw = true;
    }
    expect(threw, "Submitting outside a frame must fail.");

    threw = false;
    try {
        queue.submit_ground(ic2d::GroundQuadSubmission2D{});
    } catch (const std::logic_error&) {
        threw = true;
    }
    expect(threw, "Submitting ground outside a frame must fail.");
}

void test_ground_quads_are_immutable_and_ordered() {
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
    expect(frame.ground_quads().size() == 2, "Finished frame must retain every ground submission.");
    expect(frame.ground_quads().size() == 2 && frame.ground_quads()[0].stable_id == 2 &&
               frame.ground_quads()[1].stable_id == 1,
           "Ground surfaces must retain authored submission order.");
}

void test_frame_orders_layer_then_depth_then_id() {
    ic2d::RenderQueue2D queue;
    queue.begin({});
    queue.submit({.stable_id = 30, .position = {0.0F, 10.0F}, .sort_depth = 40.0F, .layer = 1});
    queue.submit({.stable_id = 20, .position = {0.0F, 80.0F}, .sort_depth = 20.0F, .layer = 1});
    queue.submit({.stable_id = 10, .position = {0.0F, 100.0F}, .sort_depth = 100.0F, .layer = -1});
    const auto frame = queue.finish();
    const auto sprites = frame.sprites();

    expect(sprites.size() == 3, "Finished frame must contain every submission.");
    expect(sprites.size() == 3 && sprites[0].stable_id == 10 && sprites[1].stable_id == 20 &&
               sprites[2].stable_id == 30,
           "Frame must sort by layer, then projected depth, then stable ID.");
}

void test_non_finite_sort_depth_is_rejected() {
    ic2d::RenderQueue2D queue;
    queue.begin({});
    bool threw = false;
    try {
        queue.submit({.stable_id = 1, .sort_depth = std::nanf("")});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "Non-finite sprite depth must be rejected before sorting.");
}

void test_finished_frame_is_independent_of_queue_reuse() {
    ic2d::RenderQueue2D queue;
    queue.begin({.center = {100.0F, 200.0F}, .zoom = 2.0F});
    queue.submit({.stable_id = 1});
    const auto first_frame = queue.finish();

    queue.begin({});
    queue.submit({.stable_id = 2});
    const auto second_frame = queue.finish();

    expect(first_frame.sprites().size() == 1 && first_frame.sprites()[0].stable_id == 1,
           "Reusing the queue must not mutate a finished frame.");
    expect(second_frame.sprites().size() == 1 && second_frame.sprites()[0].stable_id == 2,
           "Reused queue must build the next frame independently.");
    expect(std::abs(first_frame.camera().zoom - 2.0F) < 0.001F,
           "Finished frame must retain its camera description.");
}

void test_invalid_camera_is_rejected() {
    ic2d::RenderQueue2D queue;
    bool threw = false;
    try {
        queue.begin({.zoom = 0.0F});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "Zero camera zoom must be rejected.");
}

} // namespace

int run_projection25d_tests();

void test_offscreen_sprites_are_culled_before_ordering() {
    // Sorting a frame and then discarding most of it in the renderer pays the
    // ordering cost for sprites that never appear, so the queue rejects them
    // as they arrive and reports what it dropped.
    ic2d::RenderQueue2D queue;
    queue.begin({.center = {0.0F, 0.0F}, .rotation_degrees = 0.0F, .zoom = 1.0F}, 100, 100);
    queue.submit({.stable_id = 1, .position = {0.0F, 0.0F}, .size = {10.0F, 10.0F}});
    queue.submit({.stable_id = 2, .position = {900.0F, 0.0F}, .size = {10.0F, 10.0F}});
    queue.submit({.stable_id = 3, .position = {0.0F, -900.0F}, .size = {10.0F, 10.0F}});
    const ic2d::RenderFrame2D frame = queue.finish();
    expect(frame.submitted_sprites() == 3, "Every submission is counted.");
    expect(frame.culled_sprites() == 2, "Both offscreen sprites are culled.");
    expect(frame.sprites().size() == 1, "Only the visible sprite reaches the frame.");
    expect(frame.sprites()[0].stable_id == 1, "The surviving sprite is the visible one.");

    // A caller that only cares about ordering opts out with no viewport.
    ic2d::RenderQueue2D unbounded;
    unbounded.begin({.center = {0.0F, 0.0F}, .rotation_degrees = 0.0F, .zoom = 1.0F});
    unbounded.submit({.stable_id = 9, .position = {9000.0F, 9000.0F}, .size = {10.0F, 10.0F}});
    const ic2d::RenderFrame2D everything = unbounded.finish();
    expect(everything.sprites().size() == 1 && everything.culled_sprites() == 0,
           "A zero-sized viewport culls nothing.");
}

int main() {
    test_submission_requires_begin();
    test_ground_quads_are_immutable_and_ordered();
    test_frame_orders_layer_then_depth_then_id();
    test_non_finite_sort_depth_is_rejected();
    test_finished_frame_is_independent_of_queue_reuse();
    test_invalid_camera_is_rejected();
    test_offscreen_sprites_are_culled_before_ordering();
    failures += run_projection25d_tests();

    if (failures == 0) {
        std::cout << "All Render2D tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
