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
    const auto world_x_in_camera =
        ic2d::world_ground_direction_to_camera({1.0F, 0.0F}, yawed);
    projection_expect(near(world_x_in_camera.x, 0.0F) && near(world_x_in_camera.y, 1.0F),
                      "World +X aim must become camera-forward at ninety-degree yaw.");
    const auto round_trip = ic2d::world_ground_direction_to_camera(
        {camera_right.x, camera_right.z}, yawed);
    projection_expect(near(round_trip.x, 1.0F) && near(round_trip.y, 0.0F),
                      "World/camera ground direction transforms must round-trip.");

    const auto movement = ic2d::camera_ground_movement({0.0F, 1.0F}, yawed);
    projection_expect(near(movement.world_direction.x, 1.0F) &&
                          near(movement.world_direction.y, 0.0F),
                      "Simulation movement must remain camera-rotated in world X/Z.");
    projection_expect(near(movement.presentation_direction.x, 0.0F) &&
                          near(movement.presentation_direction.y, 1.0F),
                      "Animation facing must retain the screen-relative direction the player pressed.");

    projection_expect(!ic2d::valid(ic2d::Camera25DState{.pitch_degrees = 0.0F}),
                      "A side-on camera with no ground projection must be rejected.");

    const ic2d::Vec2 canvas_origin{
        400.0F + focus.position.x,
        250.0F + focus.position.y,
    };
    const auto pointer_right = ic2d::canvas_ground_direction(
        camera.focus, {canvas_origin.x + 100.0F, canvas_origin.y}, 800, 500, camera);
    projection_expect(near(pointer_right.x, 1.0F) && near(pointer_right.y, 0.0F),
                      "A pointer right of the actor must aim along world +X at zero yaw.");

    const auto pointer_forward = ic2d::canvas_ground_direction(
        camera.focus, {canvas_origin.x, canvas_origin.y + 100.0F}, 800, 500, camera);
    projection_expect(near(pointer_forward.x, 0.0F) && near(pointer_forward.y, 1.0F),
                      "Ground unprojection must compensate for pitch when aiming down-screen.");

    const auto yawed_forward = ic2d::canvas_ground_direction(
        yawed.focus, {400.0F, 350.0F}, 800, 500, yawed);
    projection_expect(near(yawed_forward.x, 1.0F) && near(yawed_forward.y, 0.0F),
                      "Canvas aim must rotate from camera space into world X/Z.");

    const auto no_direction = ic2d::canvas_ground_direction(
        camera.focus, canvas_origin, 800, 500, camera);
    projection_expect(near(no_direction.x, 0.0F) && near(no_direction.y, 0.0F),
                      "A pointer exactly on the actor must not invent a direction.");
    const auto invalid_canvas = ic2d::canvas_ground_direction(
        camera.focus, canvas_origin, 0, 500, camera);
    projection_expect(near(invalid_canvas.x, 0.0F) && near(invalid_canvas.y, 0.0F),
                      "Invalid canvas dimensions must return a safe zero direction.");

    // A wall that occupies one depth stays one billboard.
    const auto flat = ic2d::plan_depth_slices(100.0F, 0.0F, 46.0F, 50.0F);
    projection_expect(flat.count == 1 && near(flat.first_center_z, 100.0F),
                      "A sprite without a depth span must plan exactly one slice.");

    // Sixty percent of 46 screen units is 27.6; at 50 degrees that is 36.03
    // world units of depth per slice, so a 400-unit wall needs 12.
    const auto wall = ic2d::plan_depth_slices(0.0F, 400.0F, 46.0F, 50.0F);
    projection_expect(wall.count == 12,
                      "Slice count must be the fewest whose projections still overlap.");
    projection_expect(near(wall.step, 400.0F / 12.0F),
                      "Slices must divide the span evenly.");
    projection_expect(near(wall.first_center_z, -200.0F + (400.0F / 12.0F) * 0.5F),
                      "The first slice must be centred inside the near end of the span.");
    const float last_center =
        wall.first_center_z + wall.step * static_cast<float>(wall.count - 1);
    projection_expect(near(last_center, 200.0F - (400.0F / 12.0F) * 0.5F),
                      "The last slice must be centred inside the far end of the span.");

    // The overlap rule is what makes the run read as one surface: each slice
    // must project shorter than the sprite it draws.
    const float projected_step = wall.step * std::sin(50.0F * 3.14159265F / 180.0F);
    projection_expect(projected_step < 46.0F,
                      "Neighbouring slices must overlap on screen, not leave a gap.");

    // A shallower camera advances less screen space per unit of depth, so the
    // same wall needs fewer slices, never more.
    const auto shallow = ic2d::plan_depth_slices(0.0F, 400.0F, 46.0F, 20.0F);
    projection_expect(shallow.count <= wall.count,
                      "A shallower pitch must not increase the slice count.");

    const auto capped = ic2d::plan_depth_slices(0.0F, 1'000'000.0F, 4.0F, 90.0F);
    projection_expect(capped.count <= 512,
                      "An extreme span must stay within the submission cap.");

    const auto invalid = ic2d::plan_depth_slices(7.0F, 400.0F, 0.0F, 50.0F);
    projection_expect(invalid.count == 1 && near(invalid.first_center_z, 7.0F),
                      "An unusable sprite height must fall back to a single slice.");

    return projection_failures;
}
