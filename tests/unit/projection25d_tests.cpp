#include <doctest/doctest.h>

#include "ic2d/projection25d.hpp"

#include <cmath>
#include <initializer_list>

namespace {

[[nodiscard]] bool near(const float left, const float right, const float tolerance = 0.001F) {
    return std::abs(left - right) < tolerance;
}

[[nodiscard]] ic2d::Camera25DState test_camera() {
    return {
        .focus = {10.0F, 5.0F, 20.0F},
        .pitch_degrees = 45.0F,
        .pixels_per_world_unit = 2.0F,
    };
}

[[nodiscard]] ic2d::Camera25DState yawed_camera() {
    return {.yaw_degrees = 90.0F, .pitch_degrees = 45.0F};
}

} // namespace

TEST_CASE("world points project into canvas position and sort depth") {
    const ic2d::Camera25DState camera = test_camera();

    const auto focus = ic2d::project_world_point(camera.focus, camera);
    CHECK_MESSAGE((near(focus.position.x, 0.0F) && near(focus.position.y, 0.0F)),
                  "Camera focus must project to the canvas centre.");

    const auto right = ic2d::project_world_point({20.0F, 5.0F, 20.0F}, camera);
    CHECK_MESSAGE((near(right.position.x, 20.0F) && near(right.depth, 0.0F)),
                  "+X must project right without changing depth at zero yaw.");

    const auto forward = ic2d::project_world_point({10.0F, 5.0F, 30.0F}, camera);
    CHECK_MESSAGE((forward.position.y > 0.0F && near(forward.depth, 10.0F)),
                  "+Z must project down-screen and increase sort depth.");

    const auto elevated = ic2d::project_world_point({10.0F, 15.0F, 20.0F}, camera);
    CHECK_MESSAGE((elevated.position.y < 0.0F && near(elevated.depth, 0.0F)),
                  "+Y elevation must lift a billboard without changing its ground depth.");
}

TEST_CASE("camera yaw rotates between world and camera ground space") {
    const ic2d::Camera25DState yawed = yawed_camera();

    const auto yawed_x = ic2d::project_world_point({10.0F, 0.0F, 0.0F}, yawed);
    CHECK_MESSAGE((std::abs(yawed_x.position.x) < 0.001F && near(yawed_x.depth, 10.0F)),
                  "Camera yaw must rotate world X into camera depth.");

    const auto camera_right = ic2d::camera_ground_direction_to_world({1.0F, 0.0F}, yawed);
    const auto camera_forward = ic2d::camera_ground_direction_to_world({0.0F, 1.0F}, yawed);
    CHECK_MESSAGE((near(camera_right.x, 0.0F) && near(camera_right.z, -1.0F)),
                  "Camera-right input must rotate onto world X/Z with camera yaw.");
    CHECK_MESSAGE((near(camera_forward.x, 1.0F) && near(camera_forward.z, 0.0F)),
                  "Camera-forward input must rotate onto world X/Z with camera yaw.");

    const auto world_x_in_camera = ic2d::world_ground_direction_to_camera({1.0F, 0.0F}, yawed);
    CHECK_MESSAGE((near(world_x_in_camera.x, 0.0F) && near(world_x_in_camera.y, 1.0F)),
                  "World +X aim must become camera-forward at ninety-degree yaw.");
    const auto round_trip =
        ic2d::world_ground_direction_to_camera({camera_right.x, camera_right.z}, yawed);
    CHECK_MESSAGE((near(round_trip.x, 1.0F) && near(round_trip.y, 0.0F)),
                  "World/camera ground direction transforms must round-trip.");

    const auto movement = ic2d::camera_ground_movement({0.0F, 1.0F}, yawed);
    CHECK_MESSAGE(
        (near(movement.world_direction.x, 1.0F) && near(movement.world_direction.y, 0.0F)),
        "Simulation movement must remain camera-rotated in world X/Z.");
    CHECK_MESSAGE((near(movement.presentation_direction.x, 0.0F) &&
                   near(movement.presentation_direction.y, 1.0F)),
                  "Animation facing must retain the screen-relative direction the player pressed.");
}

TEST_CASE("a side-on camera with no ground projection is rejected") {
    CHECK_MESSAGE((!ic2d::valid(ic2d::Camera25DState{.pitch_degrees = 0.0F})),
                  "A side-on camera with no ground projection must be rejected.");
}

TEST_CASE("a canvas pointer resolves to a ground direction") {
    const ic2d::Camera25DState camera = test_camera();
    const ic2d::Camera25DState yawed = yawed_camera();
    const auto focus = ic2d::project_world_point(camera.focus, camera);
    const ic2d::Vec2 canvas_origin{
        400.0F + focus.position.x,
        250.0F + focus.position.y,
    };

    const auto pointer_right = ic2d::canvas_ground_direction(
        camera.focus, {canvas_origin.x + 100.0F, canvas_origin.y}, 800, 500, camera);
    CHECK_MESSAGE((near(pointer_right.x, 1.0F) && near(pointer_right.y, 0.0F)),
                  "A pointer right of the actor must aim along world +X at zero yaw.");

    const auto pointer_forward = ic2d::canvas_ground_direction(
        camera.focus, {canvas_origin.x, canvas_origin.y + 100.0F}, 800, 500, camera);
    CHECK_MESSAGE((near(pointer_forward.x, 0.0F) && near(pointer_forward.y, 1.0F)),
                  "Ground unprojection must compensate for pitch when aiming down-screen.");

    const auto yawed_forward =
        ic2d::canvas_ground_direction(yawed.focus, {400.0F, 350.0F}, 800, 500, yawed);
    CHECK_MESSAGE((near(yawed_forward.x, 1.0F) && near(yawed_forward.y, 0.0F)),
                  "Canvas aim must rotate from camera space into world X/Z.");

    const auto no_direction =
        ic2d::canvas_ground_direction(camera.focus, canvas_origin, 800, 500, camera);
    CHECK_MESSAGE((near(no_direction.x, 0.0F) && near(no_direction.y, 0.0F)),
                  "A pointer exactly on the actor must not invent a direction.");
    const auto invalid_canvas =
        ic2d::canvas_ground_direction(camera.focus, canvas_origin, 0, 500, camera);
    CHECK_MESSAGE((near(invalid_canvas.x, 0.0F) && near(invalid_canvas.y, 0.0F)),
                  "Invalid canvas dimensions must return a safe zero direction.");
}

TEST_CASE("depth slicing keeps a deep sprite reading as one surface") {
    // A wall that occupies one depth stays one billboard.
    const auto flat = ic2d::plan_depth_slices(100.0F, 0.0F, 46.0F, 50.0F);
    CHECK_MESSAGE((flat.count == 1 && near(flat.first_center_z, 100.0F)),
                  "A sprite without a depth span must plan exactly one slice.");

    // Sixty percent of 46 screen units is 27.6; at 50 degrees that is 36.03
    // world units of depth per slice, so a 400-unit wall needs 12.
    const auto wall = ic2d::plan_depth_slices(0.0F, 400.0F, 46.0F, 50.0F);
    CHECK_MESSAGE((wall.count == 12),
                  "Slice count must be the fewest whose projections still overlap.");
    CHECK_MESSAGE((near(wall.step, 400.0F / 12.0F)), "Slices must divide the span evenly.");
    CHECK_MESSAGE((near(wall.first_center_z, -200.0F + (400.0F / 12.0F) * 0.5F)),
                  "The first slice must be centred inside the near end of the span.");
    const float last_center = wall.first_center_z + wall.step * static_cast<float>(wall.count - 1);
    CHECK_MESSAGE((near(last_center, 200.0F - (400.0F / 12.0F) * 0.5F)),
                  "The last slice must be centred inside the far end of the span.");

    // The overlap rule is what makes the run read as one surface: each slice
    // must project shorter than the sprite it draws.
    const float projected_step = wall.step * std::sin(50.0F * 3.14159265F / 180.0F);
    CHECK_MESSAGE((projected_step < 46.0F),
                  "Neighbouring slices must overlap on screen, not leave a gap.");

    // A shallower camera advances less screen space per unit of depth, so the
    // same wall needs fewer slices, never more.
    const auto shallow = ic2d::plan_depth_slices(0.0F, 400.0F, 46.0F, 20.0F);
    CHECK_MESSAGE((shallow.count <= wall.count),
                  "A shallower pitch must not increase the slice count.");

    const auto capped = ic2d::plan_depth_slices(0.0F, 1'000'000.0F, 4.0F, 90.0F);
    CHECK_MESSAGE((capped.count <= 512), "An extreme span must stay within the submission cap.");

    const auto invalid = ic2d::plan_depth_slices(7.0F, 400.0F, 0.0F, 50.0F);
    CHECK_MESSAGE((invalid.count == 1 && near(invalid.first_center_z, 7.0F)),
                  "An unusable sprite height must fall back to a single slice.");
}

TEST_CASE("the canvas inverse recovers the world offset that produced it") {
    // The canvas-to-ground inverse is what the editor camera pans with and what
    // the translate gizmo drags with, so it is checked against the forward
    // projection rather than against a hand-computed constant.
    const ic2d::Camera25DState ground{
        .focus = {0.0F, 0.0F, 0.0F},
        .yaw_degrees = 0.0F,
        .pitch_degrees = 50.0F,
        .pixels_per_world_unit = 2.0F,
        .zoom = 1.5F,
    };
    const ic2d::Vec3 origin{12.0F, 0.0F, -30.0F};
    const ic2d::Vec3 moved{origin.x + 40.0F, 0.0F, origin.z - 17.0F};
    const auto from = ic2d::project_world_point(origin, ground);
    const auto to = ic2d::project_world_point(moved, ground);
    const ic2d::Vec3 recovered = ic2d::canvas_ground_offset_to_world(
        {to.position.x - from.position.x, to.position.y - from.position.y}, ground);
    CHECK_MESSAGE((near(recovered.x, 40.0F, 0.01F) && near(recovered.z, -17.0F, 0.01F)),
                  "The canvas inverse must recover the world offset that produced it.");
    CHECK_MESSAGE((near(recovered.y, 0.0F)), "A ground offset must stay on the ground plane.");

    // The same check through yaw, because a rotated scene must drag along the
    // screen axes rather than along world X and Z.
    ic2d::Camera25DState yawed_ground = ground;
    yawed_ground.yaw_degrees = 37.0F;
    const auto yawed_from = ic2d::project_world_point(origin, yawed_ground);
    const auto yawed_to = ic2d::project_world_point(moved, yawed_ground);
    const ic2d::Vec3 yawed_recovered = ic2d::canvas_ground_offset_to_world(
        {yawed_to.position.x - yawed_from.position.x, yawed_to.position.y - yawed_from.position.y},
        yawed_ground);
    CHECK_MESSAGE((near(yawed_recovered.x, 40.0F, 0.01F) && near(yawed_recovered.z, -17.0F, 0.01F)),
                  "The canvas inverse must round-trip through yaw.");

    ic2d::Camera25DState unusable = ground;
    unusable.pitch_degrees = 0.0F;
    const ic2d::Vec3 rejected = ic2d::canvas_ground_offset_to_world({10.0F, 10.0F}, unusable);
    CHECK_MESSAGE((near(rejected.x, 0.0F) && near(rejected.z, 0.0F)),
                  "An unusable camera must return a zero offset, not a guess.");
}

TEST_CASE("a canvas point resolves to the world position that projected to it") {
    // A pointer must resolve to a place, not only a bearing, because aiming
    // needs the range. Checked against the forward projection at two
    // elevations, since screen Y mixes depth and height.
    const ic2d::Camera25DState ground{
        .focus = {40.0F, 6.0F, -12.0F},
        .yaw_degrees = -18.0F,
        .pitch_degrees = 50.0F,
        .pixels_per_world_unit = 1.0F,
        .zoom = 1.25F,
    };
    constexpr int width = 640;
    constexpr int height = 360;
    for (const float elevation : {0.0F, 24.0F}) {
        CAPTURE(elevation);
        const ic2d::Vec3 world{123.0F, elevation, -64.0F};
        const auto projected = ic2d::project_world_point(world, ground);
        const ic2d::Vec2 canvas{
            projected.position.x + static_cast<float>(width) * 0.5F,
            projected.position.y + static_cast<float>(height) * 0.5F,
        };
        const ic2d::Vec3 recovered =
            ic2d::canvas_ground_point(canvas, elevation, width, height, ground);
        CHECK_MESSAGE((near(recovered.x, world.x, 0.01F) && near(recovered.z, world.z, 0.01F)),
                      "A canvas point must resolve back to the world position that "
                      "projected to it.");
        CHECK_MESSAGE((near(recovered.y, elevation)),
                      "A resolved ground point must sit at the requested elevation.");
    }

    const ic2d::Vec3 rejected = ic2d::canvas_ground_point({10.0F, 10.0F}, 0.0F, 0, 360, ground);
    CHECK_MESSAGE((near(rejected.x, 0.0F) && near(rejected.z, 0.0F)),
                  "An unusable canvas must return the origin, not a guess.");
}
