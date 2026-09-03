#pragma once

#include "ic2d/identity.hpp"
#include "ic2d/types.hpp"

#include <cstddef>
#include <vector>

namespace ic2d {

class JobSystem;

// Local crowd separation for ground actors. NavGrid owns static topology and
// NavAgentSystem owns routes; neither knows that other agents exist, so a
// hundred Runners converging on one target stack into a single point. This
// module is that missing term and nothing else: it reads copied positions and
// desired directions and returns adjusted directions.
//
// Cost is bounded by a uniform spatial hash sized to the separation radius, so
// ten thousand actors stay linear rather than quadratic. Results depend only on
// the input order, so a replay of the same tick produces the same steering.
//
// Every neighbour within the radius contributes. An earlier version capped the
// count per actor to bound the work, but the cells are visited in a fixed order
// and a dense crowd exhausts any such budget on the first cells reached, so
// each actor felt only the neighbours on one side and was pushed steadily
// toward the other. A crowd converging on a target wound onto one side of it
// instead of surrounding it. Density already bounds the work here, because
// separation is itself what stops actors from stacking without limit.
struct CrowdSeparationSettings {
    // Actors closer than this push each other apart. Roughly two body widths.
    float radius{26.0F};
    // Weight of the separation push relative to the unit desired direction.
    float strength{1.35F};

    // Padding an actor holds even while pursuing. Inside it the push rises
    // sharply toward contact instead of continuing the gentle linear falloff.
    //
    // Linear falloff alone cannot hold a gap: the pursuit term is unit length,
    // so actors settle wherever the push happens to match it, which is well
    // inside a body width. Kinematic attackers make that worse because
    // collision response never pushes them apart either, so separation is the
    // only thing holding any gap at all. A firm close-range term sets where
    // that equilibrium lands; the gentle term still governs the approach, so a
    // crowd does not jitter at the edge of the radius. Zero restores the plain
    // linear falloff.
    float personal_space{0.0F};
    // How much harder the push presses at contact than at personal space.
    float contact_strength{8.0F};
};

struct CrowdAgent {
    EntityUuid actor;
    Vec2 position{};
    // Unit-length pursuit direction, or zero when the actor is holding still.
    Vec2 desired_direction{};
};

struct CrowdSteer {
    EntityUuid actor;
    // Unit-length steering direction, or zero when the actor should not move.
    Vec2 direction{};
    // True when separation contributed, so a holding actor can still shuffle
    // out of an overlap instead of standing inside another actor.
    bool separated{false};
};

// Steering an actor reads shared position data and writes only that actor's
// own result, so the work divides cleanly across threads. Passing a job system
// spreads it; passing nothing runs it inline. Either way the answer is the
// same, because ranges are fixed and no actor observes another's result.
[[nodiscard]] std::vector<CrowdSteer> resolve_crowd_separation(
    const std::vector<CrowdAgent>& agents,
    const CrowdSeparationSettings& settings = {},
    JobSystem* jobs = nullptr
);

} // namespace ic2d
