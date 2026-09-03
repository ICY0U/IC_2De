#include "ic2d/core/automated_expectations.hpp"

#include "ic2d/core/log.hpp"

#include <cmath>
#include <string>

namespace ic2d {
namespace {

// Every threshold reports the same way: what was observed, what was required.
// Writing that out per check is what made this block long enough to hide in.
void report_shortfall(const std::string& subject, const std::string& observed,
                      const std::string& expected, const std::string& noun) {
    log(LogLevel::error, subject + " observed only " + observed + " " + noun +
                             "; expected at least " + expected + ".");
}

} // namespace

int evaluate_automated_expectations(const AutomatedExpectations& expectations,
                                    const AutomatedTally& tally) {
    if (tally.projectile_spawns < expectations.minimum_projectile_spawns) {
        report_shortfall("Automated held-fire validation", std::to_string(tally.projectile_spawns),
                         std::to_string(expectations.minimum_projectile_spawns), "projectile(s)");
        return 12;
    }
    if (expectations.minimum_projectile_spawns > 0) {
        log(LogLevel::info, "Automated held-fire validation passed with " +
                                std::to_string(tally.projectile_spawns) + " projectile spawns.");
    }

    if (tally.projectile_impacts < expectations.minimum_projectile_impacts) {
        report_shortfall("Automated projectile-impact validation",
                         std::to_string(tally.projectile_impacts),
                         std::to_string(expectations.minimum_projectile_impacts), "impact(s)");
        return 13;
    }
    if (expectations.minimum_projectile_impacts > 0) {
        log(LogLevel::info, "Automated projectile-impact validation passed with " +
                                std::to_string(tally.projectile_impacts) + " resolved impact(s).");
    }

    if (tally.target_deaths < expectations.minimum_target_deaths) {
        report_shortfall("Automated target-health validation", std::to_string(tally.target_deaths),
                         std::to_string(expectations.minimum_target_deaths), "death(s)");
        return 14;
    }

    if (expectations.validate_restart_recovery) {
        if (!tally.restart_recovery_performed) {
            log(LogLevel::error,
                "Restart-recovery validation never observed a death to restart from.");
            return 26;
        }
        if (tally.target_deaths == 0) {
            log(LogLevel::error,
                "Restart-recovery validation observed no death after the restart: a revived "
                "actor was not shootable again.");
            return 26;
        }
        log(LogLevel::info, "Restart-recovery validation passed with " +
                                std::to_string(tally.target_deaths) +
                                " death(s) after restarting the running scene.");
    }

    if (expectations.minimum_target_deaths > 0) {
        if (tally.retired_actor_count < tally.target_deaths) {
            log(LogLevel::error,
                "Automated target-health validation emitted death without retiring its actor.");
            return 15;
        }
        log(LogLevel::info, "Automated target-health validation passed with " +
                                std::to_string(tally.target_deaths) +
                                " deterministic death(s) and matching scene retirement(s).");
    }

    if (tally.completed_terminal_animations < expectations.minimum_terminal_animation_completions) {
        report_shortfall("Automated target-health validation",
                         std::to_string(tally.completed_terminal_animations),
                         std::to_string(expectations.minimum_terminal_animation_completions),
                         "terminal animation sequence(s)");
        return 31;
    }
    if (expectations.minimum_terminal_animation_completions > 0) {
        log(LogLevel::info, "Automated terminal-presentation validation passed with " +
                                std::to_string(tally.completed_terminal_animations) +
                                " completed terminal one-shot(s).");
    }

    if (tally.completed_hurt_animations < expectations.minimum_hurt_animation_completions) {
        report_shortfall(
            "Automated hit-reaction validation", std::to_string(tally.completed_hurt_animations),
            std::to_string(expectations.minimum_hurt_animation_completions), "hurt animation(s)");
        return 36;
    }
    if (tally.completed_death_animations < expectations.minimum_death_animation_completions) {
        report_shortfall("Automated death-presentation validation",
                         std::to_string(tally.completed_death_animations),
                         std::to_string(expectations.minimum_death_animation_completions),
                         "death animation(s)");
        return 32;
    }
    if (tally.completed_explosion_animations <
        expectations.minimum_explosion_animation_completions) {
        report_shortfall("Automated proximity-explosion validation",
                         std::to_string(tally.completed_explosion_animations),
                         std::to_string(expectations.minimum_explosion_animation_completions),
                         "explosion animation(s)");
        return 33;
    }
    if (expectations.require_zero_death_animation_completions &&
        tally.completed_death_animations != 0) {
        log(LogLevel::error, "Automated proximity-explosion validation unexpectedly completed " +
                                 std::to_string(tally.completed_death_animations) +
                                 " death animation(s).");
        return 34;
    }
    if (expectations.require_zero_explosion_animation_completions &&
        tally.completed_explosion_animations != 0) {
        log(LogLevel::error, "Automated projectile-death validation unexpectedly completed " +
                                 std::to_string(tally.completed_explosion_animations) +
                                 " explosion animation(s).");
        return 35;
    }
    if (expectations.minimum_hurt_animation_completions > 0 ||
        expectations.minimum_death_animation_completions > 0 ||
        expectations.minimum_explosion_animation_completions > 0 ||
        expectations.require_zero_death_animation_completions ||
        expectations.require_zero_explosion_animation_completions) {
        log(LogLevel::info, "Separated enemy presentation validation passed with " +
                                std::to_string(tally.completed_hurt_animations) + " hurt, " +
                                std::to_string(tally.completed_death_animations) + " death, and " +
                                std::to_string(tally.completed_explosion_animations) +
                                " explosion completion(s).");
    }

    if (tally.retired_crowd_actor_count < expectations.minimum_crowd_actor_retirements) {
        report_shortfall(
            "Automated crowd-kill validation", std::to_string(tally.retired_crowd_actor_count),
            std::to_string(expectations.minimum_crowd_actor_retirements), "body-less actor(s)");
        return 30;
    }
    if (expectations.minimum_crowd_actor_retirements > 0) {
        log(LogLevel::info, "Automated crowd-kill validation passed with " +
                                std::to_string(tally.retired_crowd_actor_count) +
                                " body-less actor(s) hit, killed and retired.");
    }

    if (tally.dodge_started_count < expectations.minimum_dodge_starts) {
        report_shortfall("Automated dodge validation", std::to_string(tally.dodge_started_count),
                         std::to_string(expectations.minimum_dodge_starts), "start(s)");
        return 16;
    }
    if (expectations.minimum_dodge_starts > 0 && !tally.dodge_invulnerability_observed) {
        log(LogLevel::error,
            "Automated dodge validation never observed the player as invulnerable.");
        return 17;
    }
    if (expectations.minimum_dodge_starts > 0 && tally.dodge_active) {
        log(LogLevel::error,
            "Automated dodge validation ended before the authored dodge duration expired.");
        return 18;
    }
    if (expectations.expected_dodge_distance > 0.0F) {
        constexpr float distance_tolerance = 0.05F;
        const float distance_error =
            std::abs(tally.dodge_distance_travelled - expectations.expected_dodge_distance);
        if (distance_error > distance_tolerance) {
            log(LogLevel::error, "Automated dodge movement travelled " +
                                     std::to_string(tally.dodge_distance_travelled) +
                                     " world units; expected " +
                                     std::to_string(expectations.expected_dodge_distance) + ".");
            return 19;
        }
        if (tally.dodge_movement_blocked) {
            log(LogLevel::error,
                "Automated open-ground dodge unexpectedly reported blocked movement.");
            return 20;
        }
    }
    if (expectations.minimum_dodge_starts > 0) {
        log(LogLevel::info, "Automated dodge validation passed with " +
                                std::to_string(tally.dodge_started_count) +
                                " fixed-tick start(s), an observed invulnerability window, "
                                "completed duration, and " +
                                std::to_string(tally.dodge_distance_travelled) +
                                " world units of collision-resolved travel.");
    }

    if (tally.enemy_acquisitions < expectations.minimum_enemy_acquisitions) {
        report_shortfall("Automated enemy-intent validation",
                         std::to_string(tally.enemy_acquisitions),
                         std::to_string(expectations.minimum_enemy_acquisitions), "acquisition(s)");
        return 22;
    }
    if (tally.enemy_attacks < expectations.minimum_enemy_attacks) {
        report_shortfall("Automated enemy-intent validation", std::to_string(tally.enemy_attacks),
                         std::to_string(expectations.minimum_enemy_attacks), "attack request(s)");
        return 23;
    }
    if (tally.enemy_distance_travelled < expectations.minimum_enemy_distance) {
        report_shortfall("Automated moving-attacker validation",
                         std::to_string(tally.enemy_distance_travelled),
                         std::to_string(expectations.minimum_enemy_distance), "world units");
        return 24;
    }
    if (tally.damage_applied_to_player < expectations.minimum_player_damage) {
        report_shortfall("Automated moving-attacker validation",
                         std::to_string(tally.damage_applied_to_player),
                         std::to_string(expectations.minimum_player_damage), "player damage");
        return 25;
    }
    if (expectations.require_zero_player_damage && tally.damage_applied_to_player != 0.0F) {
        log(LogLevel::error, "Automated harmless-enemy validation applied " +
                                 std::to_string(tally.damage_applied_to_player) +
                                 " player damage; expected exactly zero.");
        return 29;
    }
    if (expectations.require_zero_player_damage) {
        log(LogLevel::info, "Automated harmless-enemy validation passed with zero player damage.");
    }
    if (expectations.minimum_enemy_acquisitions > 0 || expectations.minimum_enemy_attacks > 0 ||
        expectations.minimum_enemy_distance > 0.0F || expectations.minimum_player_damage > 0.0F) {
        log(LogLevel::info, "Automated moving-attacker validation passed with " +
                                std::to_string(tally.enemy_acquisitions) + " acquisition(s), " +
                                std::to_string(tally.enemy_attacks) + " attack request(s), " +
                                std::to_string(tally.enemy_distance_travelled) +
                                " world units of collision-resolved travel, and " +
                                std::to_string(tally.damage_applied_to_player) + " player damage.");
    }

    if (tally.navigation_agent_count < expectations.minimum_navigation_agents) {
        report_shortfall("Automated navigation validation",
                         std::to_string(tally.navigation_agent_count),
                         std::to_string(expectations.minimum_navigation_agents), "agent(s)");
        return 28;
    }
    if (tally.navigation_search_count < expectations.minimum_navigation_searches) {
        report_shortfall("Automated navigation validation",
                         std::to_string(tally.navigation_search_count),
                         std::to_string(expectations.minimum_navigation_searches), "search(es)");
        return 26;
    }
    if (tally.navigation_waypoint_advances < expectations.minimum_navigation_waypoint_advances) {
        report_shortfall(
            "Automated navigation validation", std::to_string(tally.navigation_waypoint_advances),
            std::to_string(expectations.minimum_navigation_waypoint_advances), "waypoint(s)");
        return 27;
    }
    if (expectations.minimum_navigation_searches > 0 ||
        expectations.minimum_navigation_waypoint_advances > 0 ||
        expectations.minimum_navigation_agents > 0) {
        log(LogLevel::info,
            "Automated navigation validation passed with " +
                std::to_string(tally.navigation_agent_count) + " agent(s), " +
                std::to_string(tally.navigation_search_count) + " bounded search(es) and " +
                std::to_string(tally.navigation_waypoint_advances) + " waypoint advance(s).");
    }

    return 0;
}

} // namespace ic2d
