#include <doctest/doctest.h>

#include "ic2d/editor_camera.hpp"

#include <cmath>
#include <limits>

namespace {

[[nodiscard]] bool near(const float left, const float right, const float tolerance = 0.001F) {
    return std::abs(left - right) < tolerance;
}

[[nodiscard]] ic2d::Camera25DState gameplay_camera() {
    return {
        .focus = {100.0F, 0.0F, 40.0F},
        .yaw_degrees = 0.0F,
        .pitch_degrees = 50.0F,
        .pixels_per_world_unit = 2.0F,
        .zoom = 1.0F,
    };
}

} // namespace

TEST_CASE("panning detaches the view and attaching resumes following") {
    // These steps share one camera on purpose: each asserts about the state the
    // previous one left behind, which is the whole point of the sequence.
    const ic2d::Camera25DState gameplay = gameplay_camera();

    // Attached, the editor must be invisible: opening the panel cannot move the
    // image the game is presenting.
    ic2d::EditorCamera camera;
    CHECK_MESSAGE((!camera.detached()), "A new editor camera must follow the gameplay camera.");
    const ic2d::Camera25DState attached = camera.resolve(gameplay);
    CHECK_MESSAGE((near(attached.focus.x, gameplay.focus.x) &&
                   near(attached.focus.z, gameplay.focus.z) && near(attached.zoom, gameplay.zoom)),
                  "An attached editor camera must resolve to the gameplay camera exactly.");

    // Detaching must not jump: the first pan starts from what was on screen.
    camera.pan({0.0F, 0.0F}, gameplay);
    CHECK_MESSAGE((!camera.detached()), "An empty drag must not detach the view.");

    // Screen X maps to the camera's right axis at the current scale. Two pixels
    // per world unit means a 20 pixel drag is 10 world units, and the world
    // follows the pointer, so the focus moves the other way.
    camera.pan({20.0F, 0.0F}, gameplay);
    CHECK_MESSAGE((camera.detached()), "Panning must detach the view from the gameplay camera.");
    ic2d::Camera25DState view = camera.resolve(gameplay);
    CHECK_MESSAGE((near(view.focus.x, 100.0F - 10.0F)),
                  "A horizontal drag must move the focus against the pointer at scale.");
    CHECK_MESSAGE((near(view.focus.z, 40.0F)),
                  "A horizontal drag must not move the focus along depth.");

    // The gameplay camera keeps following the player while the view is
    // detached, and must not drag the detached view with it.
    ic2d::Camera25DState moved = gameplay;
    moved.focus = {900.0F, 0.0F, 900.0F};
    view = camera.resolve(moved);
    CHECK_MESSAGE((near(view.focus.x, 90.0F) && near(view.focus.z, 40.0F)),
                  "A detached view must ignore later gameplay camera movement.");
    CHECK_MESSAGE((near(view.pitch_degrees, moved.pitch_degrees) &&
                   near(view.yaw_degrees, moved.yaw_degrees) &&
                   near(view.pixels_per_world_unit, moved.pixels_per_world_unit)),
                  "A detached view must still take yaw, pitch, and scale from the scene.");

    camera.attach();
    view = camera.resolve(moved);
    CHECK_MESSAGE((!camera.detached() && near(view.focus.x, 900.0F) && near(view.focus.z, 900.0F)),
                  "Attaching must resume following the gameplay camera immediately.");
}

TEST_CASE("a vertical drag undoes the pitch foreshortening") {
    // Screen Y is foreshortened by pitch, so the same pixel count covers more
    // world depth than it does width.
    const ic2d::Camera25DState gameplay = gameplay_camera();
    ic2d::EditorCamera depth_camera;
    depth_camera.pan({0.0F, 20.0F}, gameplay);
    const ic2d::Camera25DState view = depth_camera.resolve(gameplay);
    const float expected_depth =
        40.0F - 20.0F / (2.0F * std::sin(50.0F * 3.14159265358979F / 180.0F));
    CHECK_MESSAGE((near(view.focus.z, expected_depth, 0.01F)),
                  "A vertical drag must undo the pitch foreshortening.");
    CHECK_MESSAGE((near(view.focus.x, 100.0F)),
                  "A vertical drag must not move the focus along width.");
}

TEST_CASE("zoom is multiplicative and saturates at both ends") {
    // Zoom is multiplicative so one notch covers the same proportion at any
    // scale, and is bounded at both ends.
    const ic2d::Camera25DState gameplay = gameplay_camera();
    ic2d::EditorCamera zoom_camera;
    zoom_camera.zoom(1.0F, gameplay);
    CHECK_MESSAGE((near(zoom_camera.resolve(gameplay).zoom, 1.15F)),
                  "One wheel notch must scale zoom by a fixed proportion.");
    zoom_camera.zoom(-1.0F, gameplay);
    CHECK_MESSAGE((near(zoom_camera.resolve(gameplay).zoom, 1.0F)),
                  "Zooming out by the same notch must return to the original scale.");
    zoom_camera.zoom(1000.0F, gameplay);
    CHECK_MESSAGE((near(zoom_camera.resolve(gameplay).zoom, ic2d::EditorCamera::maximum_zoom)),
                  "Zoom must saturate at the maximum rather than run away.");
    zoom_camera.zoom(-10000.0F, gameplay);
    CHECK_MESSAGE((near(zoom_camera.resolve(gameplay).zoom, ic2d::EditorCamera::minimum_zoom)),
                  "Zoom must saturate at the minimum rather than invert.");
}

TEST_CASE("framing centres the ground position without adopting its elevation") {
    const ic2d::Camera25DState gameplay = gameplay_camera();
    ic2d::EditorCamera frame_camera;
    frame_camera.frame({-250.0F, 68.0F, 310.0F}, gameplay);
    const ic2d::Camera25DState view = frame_camera.resolve(gameplay);
    CHECK_MESSAGE((near(view.focus.x, -250.0F) && near(view.focus.z, 310.0F)),
                  "Framing must centre the view on the requested ground position.");
    CHECK_MESSAGE((near(view.focus.y, gameplay.focus.y)),
                  "Framing must not tilt the view by adopting entity elevation.");
    CHECK_MESSAGE((near(view.zoom, gameplay.zoom)), "Framing must not change zoom.");
}

TEST_CASE("rejected input leaves the view exactly as it was") {
    // Rejected input must leave the view exactly as it was, including still
    // attached, so a bad frame cannot strand the editor somewhere unusable.
    const ic2d::Camera25DState gameplay = gameplay_camera();
    ic2d::EditorCamera guarded;
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    guarded.pan({nan, 0.0F}, gameplay);
    guarded.zoom(nan, gameplay);
    guarded.frame({nan, 0.0F, 0.0F}, gameplay);
    ic2d::Camera25DState broken = gameplay;
    broken.pitch_degrees = 0.0F;
    guarded.pan({10.0F, 10.0F}, broken);
    CHECK_MESSAGE((!guarded.detached()),
                  "Invalid input or an unusable camera must not detach the view.");
}

TEST_CASE("panning follows the camera's screen axes through yaw") {
    // A yawed scene must pan along the screen axes a reader is dragging, not
    // along world X and Z.
    ic2d::Camera25DState yawed = gameplay_camera();
    yawed.yaw_degrees = 90.0F;
    ic2d::EditorCamera yawed_camera;
    yawed_camera.pan({20.0F, 0.0F}, yawed);
    const ic2d::Camera25DState view = yawed_camera.resolve(yawed);
    CHECK_MESSAGE((near(view.focus.x, yawed.focus.x) && near(view.focus.z, yawed.focus.z + 10.0F)),
                  "Panning must follow the camera's screen axes through yaw.");
}
