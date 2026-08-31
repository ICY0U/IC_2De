#include "ic2d/projection25d.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

int projection_failures = 0;

void projection_expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++projection_failures;
    }
}

[[nodiscard]] bool near(const float left, const float right) {
    return std::abs(left - right) < 0.001F;
}

} // namespace

int run_projection25d_tests() {
    const ic2d::Camera25DState camera{
        .focus = {10.0F, 5.0F, 20.0F},
        .pitch_degrees = 45.0F,
        .pixels_per_world_unit = 2.0F,
    };

    const auto focus = ic2d::project_world_point(camera.focus, camera);
    projection_expect(near(focus.position.x, 0.0F) && near(focus.position.y, 0.0F),
                      "Camera focus must project to the canvas centre.");

    const auto right = ic2d::project_world_point({20.0F, 5.0F, 20.0F}, camera);
    projection_expect(near(right.position.x, 20.0F) && near(right.depth, 0.0F),
                      "+X must project right without changing depth at zero yaw.");

    const auto forward = ic2d::project_world_point({10.0F, 5.0F, 30.0F}, camera);
    projection_expect(forward.position.y > 0.0F && near(forward.depth, 10.0F),
                      "+Z must project down-screen and increase sort depth.");

    const auto elevated = ic2d::project_world_point({10.0F, 15.0F, 20.0F}, camera);
    projection_expect(elevated.position.y < 0.0F && near(elevated.depth, 0.0F),
                      "+Y elevation must lift a billboard without changing its ground depth.");

    const ic2d::Camera25DState yawed{.yaw_degrees = 90.0F, .pitch_degrees = 45.0F};
    const auto yawed_x = ic2d::project_world_point({10.0F, 0.0F, 0.0F}, yawed);
    projection_expect(std::abs(yawed_x.position.x) < 0.001F && near(yawed_x.depth, 10.0F),
                      "Camera yaw must rotate world X into camera depth.");

    const auto camera_right = ic2d::camera_ground_direction_to_world({1.0F, 0.0F}, yawed);
    const auto camera_forward = ic2d::camera_ground_direction_to_world({0.0F, 1.0F}, yawed);
    projection_expect(near(camera_right.x, 0.0F) && near(camera_right.z, -1.0F),
                      "Camera-right input must rotate onto world X/Z with camera yaw.");
    projection_expect(near(camera_forward.x, 1.0F) && near(camera_forward.z, 0.0F),
                      "Camera-forward input must rotate onto world X/Z with camera yaw.");

    projection_expect(!ic2d::valid(ic2d::Camera25DState{.pitch_degrees = 0.0F}),
                      "A side-on camera with no ground projection must be rejected.");

    return projection_failures;
}
