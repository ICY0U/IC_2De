#include "smoke_scenarios.hpp"

#include <algorithm>

namespace ic2de_game {
namespace {

// Adding a smoke run is one entry here. It was previously a field on
// ApplicationConfig, a branch of a two-hundred-line if-else chain in main(), a
// line of --help text and a hand-written add_test in CMakeLists.txt, with
// nothing connecting the four.
const SmokeScenario registry[]{
    {
        .name = "window",
        .description = "Capture a frame and close after 120 fixed ticks.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.max_fixed_ticks = 120;
                config.capture_tick = 60;
                config.capture_path = "build/runtime-smoke.png";
            },
    },
    {
        .name = "movement",
        .description = "Move diagonally for 300 ticks to verify XYZ projection and camera.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.automated_movement = true;
                config.max_fixed_ticks = 300;
                config.capture_tick = 285;
                config.capture_path = "build/runtime-camera-smoke.png";
            },
    },
    {
        .name = "left",
        .description = "Move left and capture the west-facing animation.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.automated_movement = true;
                config.automated_movement_direction = {-1.0F, 0.0F};
                config.validate_automated_route = false;
                config.max_fixed_ticks = 60;
                config.capture_tick = 45;
                config.capture_path = "build/runtime-left-smoke.png";
            },
    },
    {
        .name = "crosshair",
        .description = "Move north, aim east, and capture the editor crosshair.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.automated_movement = true;
                config.automated_movement_direction = {0.0F, -1.0F};
                config.automated_aim = true;
                config.automated_aim_direction = {1.0F, 0.0F};
                config.validate_automated_route = false;
                config.max_fixed_ticks = 75;
                config.capture_tick = 60;
                config.capture_path = "build/runtime-crosshair-smoke.png";
            },
    },
    {
        .name = "projectile",
        .description = "Fire east and capture fixed-tick projectile travel.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.automated_aim = true;
                config.automated_aim_direction = {1.0F, 0.0F};
                config.automated_fire_hold_ticks = 1;
                config.max_fixed_ticks = 24;
                config.capture_tick = 12;
                config.capture_path = "build/runtime-projectile-smoke.png";
            },
    },
    {
        .name = "held-fire",
        .description = "Hold fire through three cooldown-ready ticks.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.automated_aim = true;
                config.automated_aim_direction = {1.0F, 0.0F};
                config.automated_fire_hold_ticks = 18;
                config.expectations.minimum_projectile_spawns = 3;
                config.max_fixed_ticks = 24;
                config.capture_tick = 20;
                config.capture_path = "build/runtime-held-fire-smoke.png";
            },
    },
    {
        .name = "run-and-gun",
        .description = "Move north while aiming and firing east.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.automated_movement = true;
                config.automated_movement_direction = {0.0F, -1.0F};
                config.automated_aim = true;
                config.automated_aim_direction = {1.0F, 0.0F};
                config.automated_fire_hold_ticks = 18;
                config.expectations.minimum_projectile_spawns = 3;
                config.validate_automated_route = false;
                config.max_fixed_ticks = 24;
                config.capture_tick = 20;
                config.capture_path = "build/runtime-run-and-gun-smoke.png";
            },
    },
    {
        .name = "projectile-impact",
        .description = "Fire at the crate and require one impact.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.automated_aim = true;
                config.automated_aim_direction = {0.70710678F, 0.70710678F};
                config.automated_fire_hold_ticks = 1;
                config.expectations.minimum_projectile_spawns = 1;
                config.expectations.minimum_projectile_impacts = 1;
                config.max_fixed_ticks = 24;
                config.capture_tick = 10;
                config.capture_path = "build/runtime-projectile-impact-smoke.png";
            },
    },
    {
        .name = "target-death",
        .description = "Fire three deterministic hits into the NPC target.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.automated_movement = true;
                config.automated_movement_direction = {0.98359346F, 0.18039931F};
                config.automated_aim = true;
                // Automated aim is camera-relative. This is the authored camera-space
                // direction from the player to the Fuse Tyrant's X/Z position.
                config.automated_aim_direction = {0.98359346F, 0.18039931F};
                config.automated_fire_hold_ticks = 18;
                config.expectations.minimum_projectile_spawns = 3;
                config.expectations.minimum_projectile_impacts = 3;
                config.expectations.minimum_target_deaths = 1;
                config.expectations.minimum_terminal_animation_completions = 1;
                config.expectations.minimum_death_animation_completions = 1;
                config.expectations.require_zero_explosion_animation_completions = true;
                config.validate_automated_route = false;
                // The target dies around tick 66. Leave enough deterministic time
                // for the collapse one-shot to finish before presentation retires.
                config.max_fixed_ticks = 180;
                config.capture_tick = 105;
                config.capture_path = "build/runtime-target-death-smoke.png";
            },
    },
    {
        .name = "target-hurt",
        .description = "Fire one hit and require hurt without a terminal state.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.automated_movement = true;
                config.automated_movement_direction = {0.98359346F, 0.18039931F};
                config.automated_aim = true;
                config.automated_aim_direction = {0.98359346F, 0.18039931F};
                config.automated_fire_hold_ticks = 1;
                config.expectations.minimum_projectile_spawns = 1;
                config.expectations.minimum_projectile_impacts = 1;
                config.expectations.minimum_hurt_animation_completions = 1;
                config.expectations.require_zero_death_animation_completions = true;
                config.expectations.require_zero_explosion_animation_completions = true;
                config.validate_automated_route = false;
                config.max_fixed_ticks = 120;
                config.capture_tick = 62;
                config.capture_path = "build/runtime-target-hurt-smoke.png";
            },
    },
    {
        .name = "dodge",
        .description = "Verify one exact-distance directional dodge and its active window.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.automated_dodge_tick = 1;
                config.expectations.minimum_dodge_starts = 1;
                config.expectations.expected_dodge_distance = 78.0F;
                config.max_fixed_ticks = 18;
                config.capture_tick = 6;
                config.capture_path = "build/runtime-dodge-smoke.png";
            },
    },
    {
        .name = "restart-recovery",
        .description = "Restart after a kill and require another kill.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                // The same combat setup as the replay smoke, restarted once a
                // target has died. Reviving an actor has to restore everything it
                // needs to be shot again, not only its presentation.
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.automated_aim = true;
                config.automated_aim_direction = {0.98359346F, 0.18039931F};
                config.automated_fire_hold_ticks = 18;
                config.expectations.validate_restart_recovery = true;
                config.validate_automated_route = false;
                config.max_fixed_ticks = 240;
                config.capture_tick = 230;
                config.capture_path = "build/runtime-restart-recovery-smoke.png";
            },
    },
    {
        .name = "gameplay-replay",
        .description = "Replay combat, dodge, and attacker state with a digest.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.automated_aim = true;
                config.automated_aim_direction = {0.98359346F, 0.18039931F};
                config.automated_fire_hold_ticks = 18;
                config.automated_dodge_tick = 1;
                config.expectations.minimum_projectile_spawns = 3;
                config.expectations.minimum_projectile_impacts = 3;
                config.expectations.minimum_target_deaths = 1;
                config.expectations.minimum_dodge_starts = 1;
                config.expectations.expected_dodge_distance = 78.0F;
                config.expectations.minimum_enemy_acquisitions = 1;
                config.expectations.minimum_enemy_attacks = 1;
                config.expectations.minimum_enemy_distance = 80.0F;
                config.expectations.minimum_player_damage = 12.0F;
                config.report_gameplay_state_digest = true;
                config.validate_automated_route = false;
                config.max_fixed_ticks = 180;
                config.capture_tick = 150;
                config.capture_path = "build/runtime-gameplay-digest-smoke.png";
            },
    },
    {
        .name = "moving-attacker",
        .description = "Require deterministic acquire, pursuit, and player damage.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.expectations.minimum_enemy_acquisitions = 1;
                config.expectations.minimum_enemy_attacks = 1;
                config.expectations.minimum_enemy_distance = 100.0F;
                config.expectations.minimum_player_damage = 12.0F;
                config.expectations.minimum_explosion_animation_completions = 1;
                config.expectations.require_zero_death_animation_completions = true;
                config.validate_automated_route = false;
                config.max_fixed_ticks = 240;
                config.capture_tick = 138;
                config.capture_path = "build/runtime-moving-attacker-smoke.png";
            },
    },
    {
        .name = "nav-grid",
        .description = "Display the read-only 2.5D hard-blocked navigation grid.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.start_with_navigation_grid_debug = true;
                config.validate_automated_route = false;
                config.max_fixed_ticks = 30;
                config.capture_tick = 15;
                config.capture_path = "build/runtime-nav-grid-smoke.png";
            },
    },
    {
        .name = "nav-path",
        .description = "Display the copied deterministic A-star reference path.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.start_with_navigation_path_debug = true;
                config.validate_automated_route = false;
                config.max_fixed_ticks = 30;
                config.capture_tick = 15;
                config.capture_path = "build/runtime-nav-path-smoke.png";
            },
    },
    {
        .name = "runner-path",
        .description = "Follow a bounded-repath route toward the player.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.start_with_navigation_path_debug = true;
                config.expectations.minimum_navigation_searches = 3;
                config.expectations.minimum_navigation_waypoint_advances = 2;
                config.validate_automated_route = false;
                config.max_fixed_ticks = 90;
                config.capture_tick = 60;
                config.capture_path = "build/runtime-runner-path-smoke.png";
            },
    },
    {
        .name = "enemy-stress",
        .description = "Run 50 real navigation/AI/physics/render Runners.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.start_with_navigation_path_debug = true;
                config.initial_editor_enemy_stress_count = 50;
                config.expectations.minimum_navigation_agents = 50;
                config.expectations.minimum_navigation_searches = 100;
                config.expectations.minimum_navigation_waypoint_advances = 50;
                config.expectations.require_zero_player_damage = true;
                config.validate_automated_route = false;
                config.max_fixed_ticks = 90;
                config.capture_tick = 60;
                config.capture_path = "build/runtime-enemy-stress-smoke.png";
            },
    },
    {
        .name = "crowd-kill",
        .description = "Require crowd Stalker projectile-death sequences.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                // Fires into a spawned crowd and requires that a body-less actor
                // is hit, gameplay-retired, and allowed to finish its authored
                // death presentation without an unrelated explosion.
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = false;
                config.initial_editor_enemy_stress_count = 400;
                config.automated_aim = true;
                config.automated_aim_direction = {1.0F, 0.0F};
                config.automated_fire_hold_ticks = 600;
                config.validate_automated_route = false;
                config.expectations.minimum_crowd_actor_retirements = 1;
                config.expectations.minimum_terminal_animation_completions = 1;
                config.expectations.minimum_death_animation_completions = 1;
                config.expectations.require_zero_explosion_animation_completions = true;
                config.max_fixed_ticks = 600;
                config.capture_tick = 240;
                config.capture_path = "build/runtime-crowd-kill-smoke.png";
            },
    },
    {
        .name = "editor-hot-swap",
        .description = "Capture and close after a live editor texture replacement.",
        .apply =
            [](ic2d::ApplicationConfig& config) {
                config.start_with_editor = true;
                config.enable_editor_texture_hot_reload = true;
                config.close_after_editor_texture_hot_reload = true;
                config.capture_path = "build/editor-hot-swap-smoke.png";
            },
    },
};

} // namespace

std::span<const SmokeScenario> smoke_scenarios() noexcept { return registry; }

const SmokeScenario* find_smoke_scenario(const std::string_view name) noexcept {
    const auto found = std::ranges::find(registry, name, &SmokeScenario::name);
    return found == std::ranges::end(registry) ? nullptr : &*found;
}

} // namespace ic2de_game
