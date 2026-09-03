#pragma once

#include "ic2d/identity.hpp"
#include "ic2d/nav_pathfinding.hpp"
#include "ic2d/types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace ic2d {

struct NavAgentSettings {
    // An unchanged target is searched at most once during this many fixed
    // ticks. Target-cell changes, activation, and route loss can request an
    // earlier search; unchanged failures use the same bounded interval.
    std::uint32_t repath_interval_ticks{30};
    float waypoint_tolerance{4.0F};
    // How many waypoints ahead may be tested for a clear line before the agent
    // gives up and follows the next cell centre. Zero restores plain centre by
    // centre following. The window bounds the per-tick cost: a route is only
    // ever smoothed locally, which is all that is visible anyway.
    std::uint32_t smoothing_lookahead_cells{6};
};

struct NavAgentRequest {
    EntityUuid actor{};
    EntityUuid target{};
    Vec2 actor_position{};
    Vec2 target_position{};
    bool active{true};
};

struct NavAgentMotion {
    EntityUuid actor{};
    Vec2 movement_direction{};
    NavPathStatus path_status{NavPathStatus::unreachable};
    bool repathed{false};
    std::size_t waypoint_index{0};
    std::size_t path_cell_count{0};
};

struct NavAgentStateSnapshot {
    EntityUuid actor{};
    EntityUuid target{};
    bool active{false};
    NavPathStatus path_status{NavPathStatus::unreachable};
    std::optional<NavCell> current_cell;
    std::optional<NavCell> goal_cell;
    std::vector<NavCell> path;
    std::size_t waypoint_index{0};
    std::uint64_t next_repath_tick{1};
    std::uint64_t search_count{0};
    std::uint64_t waypoint_advance_count{0};
    Vec2 movement_direction{};
    float path_distance{0.0F};
    std::size_t expanded_cell_count{0};
};

struct NavAgentSnapshot {
    std::uint64_t tick{0};
    std::uint64_t total_search_count{0};
    std::uint32_t repath_interval_ticks{0};
    float waypoint_tolerance{0.0F};
    // Canonical actor UUID order, independent of registration/request order.
    std::vector<NavAgentStateSnapshot> actors;
};

// Owns deterministic A* route state and cell-centre following for registered
// actors. It neither decides whether an actor should pursue nor applies
// movement: callers submit copied pursuit facts and pass the returned normalized
// direction to their collision-owning runtime module.
class NavAgentSystem final {
public:
    explicit NavAgentSystem(NavAgentSettings settings = {});
    ~NavAgentSystem();

    NavAgentSystem(const NavAgentSystem&) = delete;
    NavAgentSystem& operator=(const NavAgentSystem&) = delete;
    NavAgentSystem(NavAgentSystem&&) noexcept;
    NavAgentSystem& operator=(NavAgentSystem&&) noexcept;

    // Agents survive reset and may only be registered before tick one.
    [[nodiscard]] bool register_agent(EntityUuid actor) noexcept;

    // Requires exactly one unique request per registered actor at each
    // one-based sequential fixed tick. The grid is borrowed only for this call.
    [[nodiscard]] std::vector<NavAgentMotion>
    fixed_update(std::uint64_t tick, const NavGrid& grid,
                 const std::vector<NavAgentRequest>& requests);

    [[nodiscard]] NavAgentSnapshot snapshot() const;
    void reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
