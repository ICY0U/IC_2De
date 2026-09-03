#include "ic2d/locomotion.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_cardinal_and_diagonal_sectors() {
    using enum ic2d::LocomotionState;
    expect(ic2d::locomotion_facing({0.0F, 1.0F}) == idle_south, "Positive Z must face south.");
    expect(ic2d::locomotion_facing({0.0F, -1.0F}) == idle_north, "Negative Z must face north.");
    expect(ic2d::locomotion_facing({-1.0F, 0.0F}) == idle_west, "Negative X must face west.");
    expect(ic2d::locomotion_facing({1.0F, 0.0F}) == idle_east, "Positive X must face east.");
    expect(ic2d::locomotion_facing({-1.0F, 1.0F}) == idle_southwest,
           "Negative X and positive Z must face southwest.");
    expect(ic2d::locomotion_facing({-1.0F, -1.0F}) == idle_northwest,
           "Negative X and Z must face northwest.");
    expect(ic2d::locomotion_facing({1.0F, -1.0F}) == idle_northeast,
           "Positive X and negative Z must face northeast.");
    expect(ic2d::locomotion_facing({1.0F, 1.0F}) == idle_southeast,
           "Positive X and Z must face southeast.");
}

void test_sector_threshold_and_state_conversion() {
    using enum ic2d::LocomotionState;
    expect(ic2d::locomotion_facing({1.0F, 0.4F}) == idle_east,
           "Vectors inside the 22.5-degree cardinal cone must remain cardinal.");
    expect(ic2d::locomotion_facing({1.0F, 0.5F}) == idle_southeast,
           "Vectors outside the cardinal cone must select a diagonal.");
    expect(ic2d::moving_locomotion(idle_northwest) == move_northwest &&
               ic2d::dodging_locomotion(move_northwest) == dodge_northwest &&
               ic2d::idle_locomotion(dodge_northwest) == idle_northwest &&
               ic2d::is_dodging_locomotion(dodge_northwest) &&
               !ic2d::is_dodging_locomotion(move_northwest),
           "Idle, move, and dodge variants must preserve all eight facings.");
    expect(ic2d::locomotion_facing({0.0F, 0.0F}) == idle_south &&
               ic2d::locomotion_facing({std::numeric_limits<float>::quiet_NaN(), 1.0F}) ==
                   idle_south,
           "Invalid or zero directions must fall back safely.");
}

void test_action_states_preserve_facing_and_reduce_to_authored_views() {
    using enum ic2d::LocomotionState;
    expect(ic2d::seated_locomotion(idle_south) == seated_south &&
               ic2d::seated_locomotion(idle_north) == seated_north &&
               ic2d::idle_locomotion(seated_north) == idle_north &&
               ic2d::is_seated_locomotion(seated_south) &&
               !ic2d::is_dodging_locomotion(seated_south),
           "Seated states must retain north/south facing without entering dodge logic.");
    expect(ic2d::shooting_locomotion(idle_south) == shoot_south &&
               ic2d::shooting_locomotion(idle_southwest) == shoot_west &&
               ic2d::shooting_locomotion(idle_northwest) == shoot_west &&
               ic2d::shooting_locomotion(idle_north) == shoot_north &&
               ic2d::shooting_locomotion(idle_northeast) == shoot_east &&
               ic2d::shooting_locomotion(idle_southeast) == shoot_east &&
               ic2d::idle_locomotion(shoot_east) == idle_east &&
               ic2d::is_shooting_locomotion(shoot_north),
           "Eight-way aim must reduce predictably to the four authored shooting views.");
}

void test_aim_facing_is_independent_from_translation() {
    using enum ic2d::LocomotionState;
    expect(ic2d::locomotion_state(idle_north, {1.0F, 0.0F}, false) == idle_east,
           "An idle actor must turn toward a non-zero aim direction.");
    expect(ic2d::locomotion_state(idle_south, {-1.0F, 0.0F}, true) == move_west,
           "A moving actor must animate in its independent aim-facing direction.");
    expect(ic2d::locomotion_state(idle_northwest, {}, true) == move_northwest,
           "Releasing aim must preserve the retained facing while movement continues.");
}

void test_aim_facing_does_not_flicker_at_a_sector_boundary() {
    using enum ic2d::LocomotionState;
    expect(ic2d::locomotion_state(idle_east, {1.0F, 0.45F}, true) == move_east,
           "Small mouse motion across a facing boundary must retain the previous sector.");
}

} // namespace

int main() {
    test_cardinal_and_diagonal_sectors();
    test_sector_threshold_and_state_conversion();
    test_action_states_preserve_facing_and_reduce_to_authored_views();
    test_aim_facing_is_independent_from_translation();
    test_aim_facing_does_not_flicker_at_a_sector_boundary();
    if (failures == 0) {
        std::cout << "Eight-way locomotion tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
