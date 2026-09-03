#include <doctest/doctest.h>

#include "ic2d/core/automated_run.hpp"

#include <initializer_list>
#include <string_view>

namespace {

// A run where everything the automated route is supposed to exercise happened.
[[nodiscard]] ic2d::AutomatedRunObservations complete_run() {
    return {
        .automated_movement = true,
        .collision_observed = true,
        .elevation_observed = true,
        .physics_contact_observed = true,
        .trigger_observed = true,
        .dynamic_prop_moved = true,
        .animation_event_observed = true,
        .animation_identity_observed = true,
        .diagonal_animation_observed = true,
        .texture_lifetime_valid = true,
        .smoke_capture_failed = false,
    };
}

TEST_CASE("complete run passes") {
    CHECK_MESSAGE((ic2d::evaluate_automated_run(complete_run()) == ic2d::AutomatedRunExit::passed),
                  "A run that observed every automated check must pass.");
}

TEST_CASE("interactive run ignores gameplay checks") {
    ic2d::AutomatedRunObservations observations{};
    observations.automated_movement = false;
    CHECK_MESSAGE((ic2d::evaluate_automated_run(observations) == ic2d::AutomatedRunExit::passed),
                  "A run without automated movement must not fail gameplay checks nothing drove.");

    observations.texture_lifetime_valid = false;
    CHECK_MESSAGE((ic2d::evaluate_automated_run(observations) ==
                   ic2d::AutomatedRunExit::texture_lifetime_failed),
                  "Resource checks still apply to an interactive run.");
}

TEST_CASE("ground map gaps are reported") {
    ic2d::AutomatedRunObservations without_collision = complete_run();
    without_collision.collision_observed = false;
    CHECK_MESSAGE((ic2d::evaluate_automated_run(without_collision) ==
                   ic2d::AutomatedRunExit::ground_map_incomplete),
                  "An automated run that never collided must report the ground-map gap.");

    ic2d::AutomatedRunObservations without_elevation = complete_run();
    without_elevation.elevation_observed = false;
    CHECK_MESSAGE((ic2d::evaluate_automated_run(without_elevation) ==
                   ic2d::AutomatedRunExit::ground_map_incomplete),
                  "An automated run that never climbed must report the ground-map gap.");
}

TEST_CASE("physics gaps are reported") {
    for (const auto field : {&ic2d::AutomatedRunObservations::physics_contact_observed,
                             &ic2d::AutomatedRunObservations::trigger_observed,
                             &ic2d::AutomatedRunObservations::dynamic_prop_moved}) {
        ic2d::AutomatedRunObservations observations = complete_run();
        observations.*field = false;
        CHECK_MESSAGE(
            (ic2d::evaluate_automated_run(observations) ==
             ic2d::AutomatedRunExit::physics_incomplete),
            "Any missing contact, trigger, or prop movement must report the physics gap.");
    }
}

TEST_CASE("animation gaps are reported") {
    ic2d::AutomatedRunObservations without_event = complete_run();
    without_event.animation_event_observed = false;
    CHECK_MESSAGE((ic2d::evaluate_automated_run(without_event) ==
                   ic2d::AutomatedRunExit::animation_incomplete),
                  "A run with no animation frame event must report the animation gap.");

    ic2d::AutomatedRunObservations without_identity = complete_run();
    without_identity.animation_identity_observed = false;
    CHECK_MESSAGE((ic2d::evaluate_automated_run(without_identity) ==
                   ic2d::AutomatedRunExit::animation_incomplete),
                  "A frame event without stable identity must report the animation gap.");

    ic2d::AutomatedRunObservations without_diagonal = complete_run();
    without_diagonal.diagonal_animation_observed = false;
    CHECK_MESSAGE((ic2d::evaluate_automated_run(without_diagonal) ==
                   ic2d::AutomatedRunExit::locomotion_incomplete),
                  "A run with no diagonal clip must report the locomotion gap separately.");
}

TEST_CASE("resource and capture failures") {
    ic2d::AutomatedRunObservations leaked = complete_run();
    leaked.texture_lifetime_valid = false;
    CHECK_MESSAGE(
        (ic2d::evaluate_automated_run(leaked) == ic2d::AutomatedRunExit::texture_lifetime_failed),
        "A leaked texture must fail a run that otherwise passed.");

    ic2d::AutomatedRunObservations uncaptured = complete_run();
    uncaptured.smoke_capture_failed = true;
    CHECK_MESSAGE(
        (ic2d::evaluate_automated_run(uncaptured) == ic2d::AutomatedRunExit::smoke_capture_failed),
        "A missing smoke capture must fail the run.");
}

TEST_CASE("gameplay gaps outrank resource gaps") {
    ic2d::AutomatedRunObservations observations = complete_run();
    observations.collision_observed = false;
    observations.texture_lifetime_valid = false;
    observations.smoke_capture_failed = true;
    CHECK_MESSAGE((ic2d::evaluate_automated_run(observations) ==
                   ic2d::AutomatedRunExit::ground_map_incomplete),
                  "The first failed check in order must decide the verdict.");
}

// These numbers are consumed by packaging and the replay scripts.
TEST_CASE("exit codes are stable") {
    CHECK_MESSAGE((ic2d::exit_code(ic2d::AutomatedRunExit::passed) == 0),
                  "A passing run must exit zero.");
    CHECK_MESSAGE((ic2d::exit_code(ic2d::AutomatedRunExit::texture_lifetime_failed) == 6),
                  "Texture lifetime failure must stay exit code 6.");
    CHECK_MESSAGE((ic2d::exit_code(ic2d::AutomatedRunExit::ground_map_incomplete) == 7),
                  "Ground-map failure must stay exit code 7.");
    CHECK_MESSAGE((ic2d::exit_code(ic2d::AutomatedRunExit::smoke_capture_failed) == 8),
                  "Smoke capture failure must stay exit code 8.");
    CHECK_MESSAGE((ic2d::exit_code(ic2d::AutomatedRunExit::physics_incomplete) == 9),
                  "Physics failure must stay exit code 9.");
    CHECK_MESSAGE((ic2d::exit_code(ic2d::AutomatedRunExit::animation_incomplete) == 10),
                  "Animation failure must stay exit code 10.");
    CHECK_MESSAGE((ic2d::exit_code(ic2d::AutomatedRunExit::locomotion_incomplete) == 11),
                  "Locomotion failure must stay exit code 11.");
}

} // namespace
