#include "ic2d/crowd_separation.hpp"

#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] bool near(const float left, const float right) {
    return std::abs(left - right) < 0.001F;
}

[[nodiscard]] bool unit_length(const ic2d::Vec2 value) {
    return near(std::sqrt(value.x * value.x + value.y * value.y), 1.0F);
}

void distant_actors_keep_their_desired_direction() {
    const std::vector<ic2d::CrowdAgent> agents{
        {.actor = {1}, .position = {0.0F, 0.0F}, .desired_direction = {1.0F, 0.0F}},
        {.actor = {2}, .position = {500.0F, 0.0F}, .desired_direction = {-1.0F, 0.0F}},
    };
    const std::vector<ic2d::CrowdSteer> steers = ic2d::resolve_crowd_separation(agents);
    expect(steers.size() == 2, "Every agent receives one steer.");
    expect(!steers[0].separated && !steers[1].separated,
           "Actors beyond the radius do not separate.");
    expect(near(steers[0].direction.x, 1.0F), "The first desired direction survives.");
    expect(near(steers[1].direction.x, -1.0F), "The second desired direction survives.");
}

void overlapping_actors_push_apart_along_their_offset() {
    // Both want to walk east; the trailing one is nearly on top of the leader.
    const std::vector<ic2d::CrowdAgent> agents{
        {.actor = {1}, .position = {0.0F, 0.0F}, .desired_direction = {1.0F, 0.0F}},
        {.actor = {2}, .position = {0.0F, 4.0F}, .desired_direction = {1.0F, 0.0F}},
    };
    const std::vector<ic2d::CrowdSteer> steers = ic2d::resolve_crowd_separation(agents);
    expect(steers[0].separated && steers[1].separated, "Close actors separate.");
    expect(steers[0].direction.y < 0.0F, "The leading actor is pushed to negative Z.");
    expect(steers[1].direction.y > 0.0F, "The trailing actor is pushed to positive Z.");
    expect(unit_length(steers[0].direction) && unit_length(steers[1].direction),
           "Blended steering stays unit length.");
}

void coincident_actors_still_receive_a_direction() {
    const std::vector<ic2d::CrowdAgent> agents{
        {.actor = {1}, .position = {10.0F, 10.0F}, .desired_direction = {}},
        {.actor = {2}, .position = {10.0F, 10.0F}, .desired_direction = {}},
    };
    const std::vector<ic2d::CrowdSteer> steers = ic2d::resolve_crowd_separation(agents);
    expect(steers[0].separated && steers[1].separated, "Coincident actors separate.");
    expect(unit_length(steers[0].direction) && unit_length(steers[1].direction),
           "Coincident actors leave with a usable direction.");
}

void holding_actors_separate_without_a_desired_direction() {
    const std::vector<ic2d::CrowdAgent> agents{
        {.actor = {1}, .position = {0.0F, 0.0F}, .desired_direction = {}},
        {.actor = {2}, .position = {6.0F, 0.0F}, .desired_direction = {}},
    };
    const std::vector<ic2d::CrowdSteer> steers = ic2d::resolve_crowd_separation(agents);
    expect(steers[0].direction.x < 0.0F, "A stopped actor backs away from its neighbor.");
    expect(steers[1].direction.x > 0.0F, "Its neighbor backs away in turn.");
}

void repeated_resolution_is_deterministic() {
    std::vector<ic2d::CrowdAgent> agents;
    for (int index = 0; index < 400; ++index) {
        agents.push_back({
            .actor = {static_cast<std::uint64_t>(index + 1)},
            .position = {static_cast<float>(index % 20) * 5.0F,
                         static_cast<float>(index / 20) * 5.0F},
            .desired_direction = {0.0F, 1.0F},
        });
    }
    const std::vector<ic2d::CrowdSteer> first = ic2d::resolve_crowd_separation(agents);
    const std::vector<ic2d::CrowdSteer> second = ic2d::resolve_crowd_separation(agents);
    bool identical = first.size() == second.size();
    for (std::size_t index = 0; identical && index < first.size(); ++index) {
        identical = first[index].actor == second[index].actor &&
                    first[index].direction.x == second[index].direction.x &&
                    first[index].direction.y == second[index].direction.y;
    }
    expect(identical, "The same input tick resolves to the same steering.");
}

} // namespace

int main() {
    distant_actors_keep_their_desired_direction();
    overlapping_actors_push_apart_along_their_offset();
    coincident_actors_still_receive_a_direction();
    holding_actors_separate_without_a_desired_direction();
    repeated_resolution_is_deterministic();
    if (failures > 0) {
        std::cerr << failures << " crowd separation check(s) failed.\n";
        return 1;
    }
    std::cout << "Crowd separation checks passed.\n";
    return 0;
}
