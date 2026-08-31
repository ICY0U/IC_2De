#include "ic2d/presentation.hpp"

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
[[nodiscard]] bool near(const float left, const float right, const float epsilon = 0.001F) {
    return std::abs(left - right) <= epsilon;
}

void test_exact_integer_scale() {
    const auto viewport = ic2d::compute_canvas_viewport(1280, 720, 640, 360);
    expect(near(viewport.scale, 2.0F), "720p output must use a two-times scale.");
    expect(near(viewport.x, 0.0F) && near(viewport.y, 0.0F),
           "Matching aspect ratio must not add letterboxing.");
    expect(near(viewport.width, 1280.0F) && near(viewport.height, 720.0F),
           "Scaled canvas must fill a matching 720p output.");
}

void test_integer_letterbox() {
    const auto viewport = ic2d::compute_canvas_viewport(1366, 768, 640, 360);
    expect(near(viewport.scale, 2.0F), "Non-integer fit must round down to an integer scale.");
    expect(near(viewport.x, 43.0F) && near(viewport.y, 24.0F),
           "Integer scaling must center the canvas in its letterbox.");
}

void test_fractional_downscale() {
    const auto viewport = ic2d::compute_canvas_viewport(320, 180, 640, 360);
    expect(near(viewport.scale, 0.5F), "Outputs smaller than the canvas must downscale safely.");
    expect(near(viewport.width, 320.0F) && near(viewport.height, 180.0F),
           "Downscaled canvas must remain inside the output.");
}

void test_invalid_dimensions() {
    const auto viewport = ic2d::compute_canvas_viewport(0, 720, 640, 360);
    expect(near(viewport.scale, 0.0F), "Invalid dimensions must produce an empty viewport.");
}

} // namespace

int main() {
    test_exact_integer_scale();
    test_integer_letterbox();
    test_fractional_downscale();
    test_invalid_dimensions();

    if (failures == 0) {
        std::cout << "All presentation tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
