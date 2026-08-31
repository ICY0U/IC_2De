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
               ic2d::idle_locomotion(move_northwest) == idle_northwest,
           "Idle and move variants must preserve all eight facings.");
    expect(ic2d::locomotion_facing({0.0F, 0.0F}) == idle_south &&
               ic2d::locomotion_facing({std::numeric_limits<float>::quiet_NaN(), 1.0F}) ==
                   idle_south,
           "Invalid or zero directions must fall back safely.");
}

} // namespace

int main() {
    test_cardinal_and_diagonal_sectors();
    test_sector_threshold_and_state_conversion();
    if (failures == 0) {
        std::cout << "Eight-way locomotion tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
