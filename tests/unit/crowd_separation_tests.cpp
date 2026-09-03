#include <doctest/doctest.h>

#include "ic2d/crowd_separation.hpp"

#include <cmath>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] bool near(const float left, const float right) {
    return std::abs(left - right) < 0.001F;
}

[[nodiscard]] bool unit_length(const ic2d::Vec2 value) {
    return near(std::sqrt(value.x * value.x + value.y * value.y), 1.0F);
}

TEST_CASE("distant actors keep their desired direction") {
    const std::vector<ic2d::CrowdAgent> agents{
        {.actor = {1}, .position = {0.0F, 0.0F}, .desired_direction = {1.0F, 0.0F}},
        {.actor = {2}, .position = {500.0F, 0.0F}, .desired_direction = {-1.0F, 0.0F}},
    };
    const std::vector<ic2d::CrowdSteer> steers = ic2d::resolve_crowd_separation(agents);
    CHECK_MESSAGE((steers.size() == 2), "Every agent receives one steer.");
    CHECK_MESSAGE((!steers[0].separated && !steers[1].separated),
                  "Actors beyond the radius do not separate.");
    CHECK_MESSAGE((near(steers[0].direction.x, 1.0F)), "The first desired direction survives.");
    CHECK_MESSAGE((near(steers[1].direction.x, -1.0F)), "The second desired direction survives.");
}

TEST_CASE("overlapping actors push apart along their offset") {
    // Both want to walk east; the trailing one is nearly on top of the leader.
    const std::vector<ic2d::CrowdAgent> agents{
        {.actor = {1}, .position = {0.0F, 0.0F}, .desired_direction = {1.0F, 0.0F}},
        {.actor = {2}, .position = {0.0F, 4.0F}, .desired_direction = {1.0F, 0.0F}},
    };
    const std::vector<ic2d::CrowdSteer> steers = ic2d::resolve_crowd_separation(agents);
    CHECK_MESSAGE((steers[0].separated && steers[1].separated), "Close actors separate.");
    CHECK_MESSAGE((steers[0].direction.y < 0.0F), "The leading actor is pushed to negative Z.");
    CHECK_MESSAGE((steers[1].direction.y > 0.0F), "The trailing actor is pushed to positive Z.");
    CHECK_MESSAGE((unit_length(steers[0].direction) && unit_length(steers[1].direction)),
                  "Blended steering stays unit length.");
}

TEST_CASE("coincident actors still receive a direction") {
    const std::vector<ic2d::CrowdAgent> agents{
        {.actor = {1}, .position = {10.0F, 10.0F}, .desired_direction = {}},
        {.actor = {2}, .position = {10.0F, 10.0F}, .desired_direction = {}},
    };
    const std::vector<ic2d::CrowdSteer> steers = ic2d::resolve_crowd_separation(agents);
    CHECK_MESSAGE((steers[0].separated && steers[1].separated), "Coincident actors separate.");
    CHECK_MESSAGE((unit_length(steers[0].direction) && unit_length(steers[1].direction)),
                  "Coincident actors leave with a usable direction.");
}

TEST_CASE("holding actors separate without a desired direction") {
    const std::vector<ic2d::CrowdAgent> agents{
        {.actor = {1}, .position = {0.0F, 0.0F}, .desired_direction = {}},
        {.actor = {2}, .position = {6.0F, 0.0F}, .desired_direction = {}},
    };
    const std::vector<ic2d::CrowdSteer> steers = ic2d::resolve_crowd_separation(agents);
    CHECK_MESSAGE((steers[0].direction.x < 0.0F), "A stopped actor backs away from its neighbor.");
    CHECK_MESSAGE((steers[1].direction.x > 0.0F), "Its neighbor backs away in turn.");
}

TEST_CASE("repeated resolution is deterministic") {
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
    CHECK_MESSAGE((identical), "The same input tick resolves to the same steering.");
}

} // namespace

// Personal space is what decides where a pursuing crowd settles. Linear
// falloff alone lets an actor press to within a fraction of a body width,
// because the pursuit term is unit length and nothing else pushes back.
TEST_CASE("personal space overrides pursuit at close range") {
    // Two actors walking straight at each other, one body width apart.
    const std::vector<ic2d::CrowdAgent> closing{
        {.actor = {1}, .position = {0.0F, 0.0F}, .desired_direction = {1.0F, 0.0F}},
        {.actor = {2}, .position = {20.0F, 0.0F}, .desired_direction = {-1.0F, 0.0F}},
    };

    const std::vector<ic2d::CrowdSteer> gentle = ic2d::resolve_crowd_separation(
        closing, {.radius = 34.0F, .strength = 1.5F, .personal_space = 0.0F});
    CHECK_MESSAGE((gentle.front().direction.x > 0.0F),
                  "With linear falloff alone a pursuing actor still closes the gap.");

    const std::vector<ic2d::CrowdSteer> padded = ic2d::resolve_crowd_separation(
        closing,
        {.radius = 34.0F, .strength = 1.5F, .personal_space = 26.0F, .contact_strength = 8.0F});
    CHECK_MESSAGE((padded.front().direction.x < 0.0F),
                  "Inside personal space the push must beat pursuit and open the gap.");
    CHECK_MESSAGE((padded.back().direction.x > 0.0F),
                  "Both actors must be pushed away from each other, not one into the other.");
    CHECK_MESSAGE((unit_length(padded.front().direction) && unit_length(padded.back().direction)),
                  "A firm push must still resolve to a unit direction.");
    CHECK_MESSAGE((padded.front().separated && padded.back().separated),
                  "Both actors must report that separation contributed.");

    // Outside personal space the gentle term still governs, so a crowd does
    // not snap apart at the edge of the radius.
    const std::vector<ic2d::CrowdAgent> approaching{
        {.actor = {1}, .position = {0.0F, 0.0F}, .desired_direction = {1.0F, 0.0F}},
        {.actor = {2}, .position = {30.0F, 0.0F}, .desired_direction = {-1.0F, 0.0F}},
    };
    const std::vector<ic2d::CrowdSteer> approach = ic2d::resolve_crowd_separation(
        approaching,
        {.radius = 34.0F, .strength = 1.5F, .personal_space = 26.0F, .contact_strength = 8.0F});
    CHECK_MESSAGE((approach.front().direction.x > 0.0F),
                  "Beyond personal space actors must still be free to close in.");
}
