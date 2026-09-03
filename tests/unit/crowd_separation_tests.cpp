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

// Personal space is what decides where a pursuing crowd settles. Linear
// falloff alone lets an actor press to within a fraction of a body width,
// because the pursuit term is unit length and nothing else pushes back.
void personal_space_overrides_pursuit_at_close_range() {
    // Two actors walking straight at each other, one body width apart.
    const std::vector<ic2d::CrowdAgent> closing{
        {.actor = {1}, .position = {0.0F, 0.0F}, .desired_direction = {1.0F, 0.0F}},
        {.actor = {2}, .position = {20.0F, 0.0F}, .desired_direction = {-1.0F, 0.0F}},
    };

    const std::vector<ic2d::CrowdSteer> gentle = ic2d::resolve_crowd_separation(
        closing, {.radius = 34.0F, .strength = 1.5F, .personal_space = 0.0F});
    expect(gentle.front().direction.x > 0.0F,
           "With linear falloff alone a pursuing actor still closes the gap.");

    const std::vector<ic2d::CrowdSteer> padded = ic2d::resolve_crowd_separation(
        closing,
        {.radius = 34.0F, .strength = 1.5F, .personal_space = 26.0F,
         .contact_strength = 8.0F});
    expect(padded.front().direction.x < 0.0F,
           "Inside personal space the push must beat pursuit and open the gap.");
    expect(padded.back().direction.x > 0.0F,
           "Both actors must be pushed away from each other, not one into the other.");
    expect(unit_length(padded.front().direction) && unit_length(padded.back().direction),
           "A firm push must still resolve to a unit direction.");
    expect(padded.front().separated && padded.back().separated,
           "Both actors must report that separation contributed.");

    // Outside personal space the gentle term still governs, so a crowd does
    // not snap apart at the edge of the radius.
    const std::vector<ic2d::CrowdAgent> approaching{
        {.actor = {1}, .position = {0.0F, 0.0F}, .desired_direction = {1.0F, 0.0F}},
        {.actor = {2}, .position = {30.0F, 0.0F}, .desired_direction = {-1.0F, 0.0F}},
    };
    const std::vector<ic2d::CrowdSteer> approach = ic2d::resolve_crowd_separation(
        approaching,
        {.radius = 34.0F, .strength = 1.5F, .personal_space = 26.0F,
         .contact_strength = 8.0F});
    expect(approach.front().direction.x > 0.0F,
           "Beyond personal space actors must still be free to close in.");
}

int main() {
    distant_actors_keep_their_desired_direction();
    overlapping_actors_push_apart_along_their_offset();
    coincident_actors_still_receive_a_direction();
    holding_actors_separate_without_a_desired_direction();
    repeated_resolution_is_deterministic();
    personal_space_overrides_pursuit_at_close_range();
    if (failures > 0) {
        std::cerr << failures << " crowd separation check(s) failed.\n";
        return 1;
    }
    std::cout << "Crowd separation checks passed.\n";
    return 0;
}
