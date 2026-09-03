#include <doctest/doctest.h>

#include "ic2d/core/automated_expectations.hpp"

namespace {

// A run that satisfies everything the scenarios below ask of it. Each test
// starts from this and spoils exactly one thing, so a failing assertion names
// the single expectation under test rather than whatever happened to be unset.
[[nodiscard]] ic2d::AutomatedTally passing_tally() {
    return {
        .projectile_spawns = 8,
        .projectile_impacts = 5,
        .target_deaths = 2,
        .restart_recovery_performed = true,
        .retired_actor_count = 2,
        .retired_crowd_actor_count = 3,
        .completed_terminal_animations = 2,
        .completed_hurt_animations = 2,
        .completed_death_animations = 1,
        .completed_explosion_animations = 1,
        .dodge_started_count = 1,
        .dodge_active = false,
        .dodge_invulnerability_observed = true,
        .dodge_distance_travelled = 78.0F,
        .dodge_movement_blocked = false,
        .enemy_acquisitions = 4,
        .enemy_attacks = 3,
        .enemy_distance_travelled = 120.0F,
        .damage_applied_to_player = 24.0F,
        .navigation_agent_count = 50,
        .navigation_search_count = 140,
        .navigation_waypoint_advances = 60,
    };
}

} // namespace

TEST_CASE("a run that asserts nothing passes whatever it observed") {
    // The shipping runtime and every ordinary launch take this path: no
    // expectation is set, so no expectation can fail.
    CHECK(ic2d::evaluate_automated_expectations({}, {}) == 0);
    CHECK(ic2d::evaluate_automated_expectations({}, passing_tally()) == 0);
}

TEST_CASE("a fully satisfied set of expectations passes") {
    const ic2d::AutomatedExpectations expectations{
        .minimum_projectile_spawns = 3,
        .minimum_projectile_impacts = 3,
        .minimum_target_deaths = 1,
        .minimum_terminal_animation_completions = 1,
        .minimum_hurt_animation_completions = 1,
        .minimum_crowd_actor_retirements = 1,
        .minimum_dodge_starts = 1,
        .expected_dodge_distance = 78.0F,
        .minimum_enemy_acquisitions = 1,
        .minimum_enemy_attacks = 1,
        .minimum_enemy_distance = 80.0F,
        .minimum_player_damage = 12.0F,
        .minimum_navigation_agents = 50,
        .minimum_navigation_searches = 100,
        .minimum_navigation_waypoint_advances = 50,
    };
    CHECK(ic2d::evaluate_automated_expectations(expectations, passing_tally()) == 0);
}

TEST_CASE("each unmet threshold reports its own exit code") {
    // These codes are a contract: CTest, the packaging scripts and
    // tools/verify-replay.ps1 branch on them, so a renumbering would break
    // tooling silently. Pinning them here is what makes that a test failure.
    struct Case {
        const char* name;
        ic2d::AutomatedExpectations expectations;
        int expected_exit;
    };

    const Case cases[]{
        {"projectile spawns", {.minimum_projectile_spawns = 99}, 12},
        {"projectile impacts", {.minimum_projectile_impacts = 99}, 13},
        {"target deaths", {.minimum_target_deaths = 99}, 14},
        {"terminal animations", {.minimum_terminal_animation_completions = 99}, 31},
        {"hurt animations", {.minimum_hurt_animation_completions = 99}, 36},
        {"death animations", {.minimum_death_animation_completions = 99}, 32},
        {"explosion animations", {.minimum_explosion_animation_completions = 99}, 33},
        {"crowd retirements", {.minimum_crowd_actor_retirements = 99}, 30},
        {"dodge starts", {.minimum_dodge_starts = 99}, 16},
        {"enemy acquisitions", {.minimum_enemy_acquisitions = 99}, 22},
        {"enemy attacks", {.minimum_enemy_attacks = 99}, 23},
        {"enemy distance", {.minimum_enemy_distance = 9999.0F}, 24},
        {"player damage", {.minimum_player_damage = 9999.0F}, 25},
        {"navigation agents", {.minimum_navigation_agents = 999}, 28},
        {"navigation searches", {.minimum_navigation_searches = 9999}, 26},
        {"waypoint advances", {.minimum_navigation_waypoint_advances = 9999}, 27},
    };

    for (const Case& scenario : cases) {
        CAPTURE(scenario.name);
        CHECK(ic2d::evaluate_automated_expectations(scenario.expectations, passing_tally()) ==
              scenario.expected_exit);
    }
}

TEST_CASE("restart recovery requires both a restart and a death after it") {
    const ic2d::AutomatedExpectations expectations{.validate_restart_recovery = true};

    ic2d::AutomatedTally never_restarted = passing_tally();
    never_restarted.restart_recovery_performed = false;
    CHECK(ic2d::evaluate_automated_expectations(expectations, never_restarted) == 26);

    // A revived actor that cannot be shot again is the failure this exists to
    // catch, and it is only visible after the restart.
    ic2d::AutomatedTally no_death_after = passing_tally();
    no_death_after.target_deaths = 0;
    CHECK(ic2d::evaluate_automated_expectations(expectations, no_death_after) == 26);

    CHECK(ic2d::evaluate_automated_expectations(expectations, passing_tally()) == 0);
}

TEST_CASE("a death must retire the actor it killed") {
    const ic2d::AutomatedExpectations expectations{.minimum_target_deaths = 1};
    ic2d::AutomatedTally unretired = passing_tally();
    unretired.retired_actor_count = 1;
    unretired.target_deaths = 2;
    CHECK(ic2d::evaluate_automated_expectations(expectations, unretired) == 15);
}

TEST_CASE("the two terminal presentations are required to stay separate") {
    // Requiring one presentation does not prove the paths are distinct. The
    // absence of the other has to be required too, which is what these codes
    // report.
    ic2d::AutomatedTally exploded = passing_tally();
    exploded.completed_death_animations = 1;
    CHECK(ic2d::evaluate_automated_expectations({.require_zero_death_animation_completions = true},
                                                exploded) == 34);

    ic2d::AutomatedTally died = passing_tally();
    died.completed_explosion_animations = 1;
    CHECK(ic2d::evaluate_automated_expectations(
              {.require_zero_explosion_animation_completions = true}, died) == 35);

    ic2d::AutomatedTally neither = passing_tally();
    neither.completed_death_animations = 0;
    neither.completed_explosion_animations = 0;
    CHECK(ic2d::evaluate_automated_expectations(
              {.require_zero_death_animation_completions = true,
               .require_zero_explosion_animation_completions = true},
              neither) == 0);
}

TEST_CASE("a dodge must show invulnerability and finish its duration") {
    const ic2d::AutomatedExpectations expectations{.minimum_dodge_starts = 1};

    ic2d::AutomatedTally never_invulnerable = passing_tally();
    never_invulnerable.dodge_invulnerability_observed = false;
    CHECK(ic2d::evaluate_automated_expectations(expectations, never_invulnerable) == 17);

    // Ending mid-dodge means the run stopped before the authored window closed,
    // so whatever it measured about the dodge is incomplete.
    ic2d::AutomatedTally still_dodging = passing_tally();
    still_dodging.dodge_active = true;
    CHECK(ic2d::evaluate_automated_expectations(expectations, still_dodging) == 18);
}

TEST_CASE("dodge distance is checked against a tolerance in both directions") {
    const ic2d::AutomatedExpectations expectations{.expected_dodge_distance = 78.0F};

    ic2d::AutomatedTally within = passing_tally();
    within.dodge_distance_travelled = 78.04F;
    CHECK(ic2d::evaluate_automated_expectations(expectations, within) == 0);

    // Too far is as wrong as too short: the distance is authored, not a floor.
    ic2d::AutomatedTally too_far = passing_tally();
    too_far.dodge_distance_travelled = 90.0F;
    CHECK(ic2d::evaluate_automated_expectations(expectations, too_far) == 19);

    ic2d::AutomatedTally too_short = passing_tally();
    too_short.dodge_distance_travelled = 60.0F;
    CHECK(ic2d::evaluate_automated_expectations(expectations, too_short) == 19);

    // Open ground is the point of the measurement; a blocked dodge would have
    // travelled a distance the authored value does not describe.
    ic2d::AutomatedTally blocked = passing_tally();
    blocked.dodge_movement_blocked = true;
    CHECK(ic2d::evaluate_automated_expectations(expectations, blocked) == 20);
}

TEST_CASE("harmless enemies must apply exactly zero player damage") {
    const ic2d::AutomatedExpectations expectations{.require_zero_player_damage = true};
    CHECK(ic2d::evaluate_automated_expectations(expectations, passing_tally()) == 29);

    ic2d::AutomatedTally harmless = passing_tally();
    harmless.damage_applied_to_player = 0.0F;
    CHECK(ic2d::evaluate_automated_expectations(expectations, harmless) == 0);
}

TEST_CASE("the first unmet expectation is the one reported") {
    // The order is fixed so that a run failing several checks always reports
    // the same code, rather than one that depends on evaluation order.
    const ic2d::AutomatedExpectations everything_fails{
        .minimum_projectile_spawns = 99,
        .minimum_projectile_impacts = 99,
        .minimum_target_deaths = 99,
    };
    CHECK(ic2d::evaluate_automated_expectations(everything_fails, passing_tally()) == 12);
}
