#pragma once

#include "ic2d/combat.hpp"
#include "ic2d/enemy_intent.hpp"
#include "ic2d/health.hpp"
#include "ic2d/nav_agent.hpp"
#include "ic2d/projectiles.hpp"
#include "ic2d/world.hpp"

#include <compare>
#include <cstdint>

namespace ic2d {

inline constexpr std::uint32_t gameplay_state_digest_schema_version = 3;

// One copied fixed-tick boundary. The digest deliberately ignores names,
// sprites, texture handles, and presentation state while retaining stable
// identities, transforms, and authoritative gameplay module snapshots.
struct GameplayStateSnapshot {
    WorldSnapshot world;
    CombatSnapshot combat;
    EnemyIntentSnapshot enemy_intent;
    NavAgentSnapshot navigation;
    ProjectileSimulationSnapshot projectiles;
    HealthSnapshot health;
};

struct GameplayStateDigest {
    std::uint32_t schema_version{gameplay_state_digest_schema_version};
    std::uint64_t value{0};

    auto operator<=>(const GameplayStateDigest&) const = default;
};

// Canonicalizes order-independent snapshot vectors before hashing. Throws
// std::invalid_argument for non-finite values, zero/duplicate identities, or
// subsystem snapshots from different fixed ticks.
[[nodiscard]] GameplayStateDigest gameplay_state_digest(
    const GameplayStateSnapshot& state
);

} // namespace ic2d
