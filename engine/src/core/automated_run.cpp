#include "ic2d/core/automated_run.hpp"

#include "ic2d/core/log.hpp"

namespace ic2d {

AutomatedRunExit evaluate_automated_run(const AutomatedRunObservations& observations) {
    // Gameplay checks only mean something when something drove the character.
    if (observations.automated_movement) {
        if (!observations.collision_observed || !observations.elevation_observed) {
            log(LogLevel::error,
                "Automated ground-map validation missed collision or elevation state.");
            return AutomatedRunExit::ground_map_incomplete;
        }
        log(LogLevel::info, "Automated ground collision and elevation validation passed.");

        if (!observations.physics_contact_observed || !observations.trigger_observed ||
            !observations.dynamic_prop_moved) {
            log(LogLevel::error, "Automated Physics2D validation missed a contact, trigger, or "
                                 "dynamic prop movement.");
            return AutomatedRunExit::physics_incomplete;
        }
        log(LogLevel::info,
            "Automated Physics2D contact, trigger, and dynamic prop validation passed.");

        if (!observations.animation_event_observed || !observations.animation_identity_observed) {
            log(LogLevel::error,
                "Automated animation validation observed no stable-identity frame event.");
            return AutomatedRunExit::animation_incomplete;
        }
        if (!observations.diagonal_animation_observed) {
            log(LogLevel::error,
                "Automated locomotion validation observed no diagonal player clip.");
            return AutomatedRunExit::locomotion_incomplete;
        }
        log(LogLevel::info, "Automated locomotion animation and frame-event validation passed.");
    }

    // Resource and capture checks apply to every run, automated or not.
    if (!observations.texture_lifetime_valid) {
        log(LogLevel::error, "Texture lifetime validation failed during shutdown.");
        return AutomatedRunExit::texture_lifetime_failed;
    }
    if (observations.smoke_capture_failed) {
        return AutomatedRunExit::smoke_capture_failed;
    }

    log(LogLevel::info, "Texture lifetime validation passed.");
    log(LogLevel::info, "Application shut down cleanly.");
    return AutomatedRunExit::passed;
}

int exit_code(const AutomatedRunExit result) noexcept { return static_cast<int>(result); }

} // namespace ic2d
