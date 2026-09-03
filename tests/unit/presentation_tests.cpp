#include <doctest/doctest.h>

#include "ic2d/presentation.hpp"

#include <cmath>
#include <string_view>

namespace {

[[nodiscard]] bool near(const float left, const float right, const float epsilon = 0.001F) {
    return std::abs(left - right) <= epsilon;
}

TEST_CASE("exact integer scale") {
    const auto viewport = ic2d::compute_canvas_viewport(1280, 720, 640, 360);
    CHECK_MESSAGE((near(viewport.scale, 2.0F)), "720p output must use a two-times scale.");
    CHECK_MESSAGE((near(viewport.x, 0.0F) && near(viewport.y, 0.0F)),
                  "Matching aspect ratio must not add letterboxing.");
    CHECK_MESSAGE((near(viewport.width, 1280.0F) && near(viewport.height, 720.0F)),
                  "Scaled canvas must fill a matching 720p output.");
}

TEST_CASE("fractional upscale fills a resized window") {
    const auto viewport = ic2d::compute_canvas_viewport(1600, 900, 640, 360);
    CHECK_MESSAGE((near(viewport.scale, 2.5F)),
                  "A resized matching-aspect window must scale the game continuously.");
    CHECK_MESSAGE((near(viewport.x, 0.0F) && near(viewport.y, 0.0F) &&
                   near(viewport.width, 1600.0F) && near(viewport.height, 900.0F)),
                  "The game canvas must fill a resized matching-aspect window.");
}

TEST_CASE("aspect ratio letterbox") {
    const auto viewport = ic2d::compute_canvas_viewport(1366, 768, 640, 360);
    CHECK_MESSAGE((near(viewport.scale, 768.0F / 360.0F)),
                  "A resized output must use all available space while preserving aspect ratio.");
    CHECK_MESSAGE(
        (near(viewport.x, (1366.0F - 640.0F * viewport.scale) * 0.5F) && near(viewport.y, 0.0F)),
        "Continuous scaling must center the canvas in its letterbox.");
}

TEST_CASE("fractional downscale") {
    const auto viewport = ic2d::compute_canvas_viewport(320, 180, 640, 360);
    CHECK_MESSAGE((near(viewport.scale, 0.5F)),
                  "Outputs smaller than the canvas must downscale safely.");
    CHECK_MESSAGE((near(viewport.width, 320.0F) && near(viewport.height, 180.0F)),
                  "Downscaled canvas must remain inside the output.");
}

TEST_CASE("invalid dimensions") {
    const auto viewport = ic2d::compute_canvas_viewport(0, 720, 640, 360);
    CHECK_MESSAGE((near(viewport.scale, 0.0F)),
                  "Invalid dimensions must produce an empty viewport.");
}

} // namespace
