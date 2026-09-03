#pragma once

#include <cstddef>
#include <cstdint>

namespace ic2d {

// What a smoke run requires of the play it drove.
//
// Every field defaults to "no requirement", so a run that sets none of them
// asserts nothing and an ordinary launch is unaffected. These were previously
// fields on ApplicationConfig, which meant the shipping runtime's configuration
// carried around two dozen test thresholds it could never use.
struct AutomatedExpectations {
    std::uint64_t minimum_projectile_spawns{0};
    std::uint64_t minimum_projectile_impacts{0};
    std::uint64_t minimum_target_deaths{0};

    // Restarts the running scene once a death has been observed, then requires
    // another death afterwards. Reviving an actor has to restore everything it
    // needs to be shot again, not merely put its sprite back, and that is not
    // observable from any single-run assertion.
    bool validate_restart_recovery{false};

    std::uint64_t minimum_terminal_animation_completions{0};
    std::uint64_t minimum_hurt_animation_completions{0};
    std::uint64_t minimum_death_animation_completions{0};
    std::uint64_t minimum_explosion_animation_completions{0};

    // Projectile lethality must complete death without the proximity-only
    // explosion, and attack-range entry must do the exact inverse. Requiring
    // one presentation is not enough on its own to prove the two are separate:
    // the absence of the other has to be required as well.
    bool require_zero_death_animation_completions{false};
    bool require_zero_explosion_animation_completions{false};

    // Crowd actors hold no rigid body, so their killability rests on the
    // scene's own actor index rather than the physics broadphase.
    std::uint64_t minimum_crowd_actor_retirements{0};

    std::uint64_t minimum_dodge_starts{0};
    float expected_dodge_distance{0.0F};

    std::uint64_t minimum_enemy_acquisitions{0};
    std::uint64_t minimum_enemy_attacks{0};
    float minimum_enemy_distance{0.0F};
    float minimum_player_damage{0.0F};
    bool require_zero_player_damage{false};

    std::size_t minimum_navigation_agents{0};
    std::uint64_t minimum_navigation_searches{0};
    std::uint64_t minimum_navigation_waypoint_advances{0};
};

// What the run actually observed, reduced to plain scalars.
//
// The application gathers these from the Combat, Health, EnemyIntent,
// ProjectileSimulation and NavAgent snapshots at shutdown. Keeping them as
// scalars rather than the snapshots themselves is what lets this verdict live
// in Core and be tested without a scene, a window or a GPU.
struct AutomatedTally {
    std::uint64_t projectile_spawns{0};
    std::uint64_t projectile_impacts{0};
    std::uint64_t target_deaths{0};
    bool restart_recovery_performed{false};
    std::uint64_t retired_actor_count{0};
    std::uint64_t retired_crowd_actor_count{0};

    std::uint64_t completed_terminal_animations{0};
    std::uint64_t completed_hurt_animations{0};
    std::uint64_t completed_death_animations{0};
    std::uint64_t completed_explosion_animations{0};

    std::uint64_t dodge_started_count{0};
    bool dodge_active{false};
    bool dodge_invulnerability_observed{false};
    float dodge_distance_travelled{0.0F};
    bool dodge_movement_blocked{false};

    std::uint64_t enemy_acquisitions{0};
    std::uint64_t enemy_attacks{0};
    float enemy_distance_travelled{0.0F};
    float damage_applied_to_player{0.0F};

    std::size_t navigation_agent_count{0};
    std::uint64_t navigation_search_count{0};
    std::uint64_t navigation_waypoint_advances{0};
};

// Applies every expectation in a fixed order and logs one line per decision.
//
// Returns 0 when the run satisfied all of them, otherwise the exit code for the
// first expectation it failed. Those codes are a contract: CTest, the packaging
// scripts and tools/verify-replay.ps1 branch on them, so they are stable and
// deliberately not renumbered.
[[nodiscard]] int evaluate_automated_expectations(const AutomatedExpectations& expectations,
                                                  const AutomatedTally& tally);

} // namespace ic2d
