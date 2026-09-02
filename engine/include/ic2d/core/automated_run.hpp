#pragma once

namespace ic2d {

// What an automated run observed while it played. Every field is set by the
// runtime loop; nothing here depends on a window, a GPU, or raylib.
struct AutomatedRunObservations {
    bool automated_movement{false};
    bool collision_observed{false};
    bool elevation_observed{false};
    bool physics_contact_observed{false};
    bool trigger_observed{false};
    bool dynamic_prop_moved{false};
    bool animation_event_observed{false};
    bool animation_identity_observed{false};
    bool diagonal_animation_observed{false};
    bool texture_lifetime_valid{true};
    bool smoke_capture_failed{false};
};

// The process exit codes an automated run reports. Packaging, CTest, and the
// replay scripts branch on these, so they are a contract rather than an
// implementation detail.
enum class AutomatedRunExit {
    passed = 0,
    texture_lifetime_failed = 6,
    ground_map_incomplete = 7,
    smoke_capture_failed = 8,
    physics_incomplete = 9,
    animation_incomplete = 10,
    locomotion_incomplete = 11,
};

// Applies the automated-run verdict in a fixed order and logs one line per
// decision. A run without automated movement only reports resource checks,
// because nothing drove the character far enough to observe gameplay state.
[[nodiscard]] AutomatedRunExit evaluate_automated_run(
    const AutomatedRunObservations& observations
);

[[nodiscard]] int exit_code(AutomatedRunExit result) noexcept;

} // namespace ic2d
