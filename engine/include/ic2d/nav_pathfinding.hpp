#pragma once

#include "ic2d/nav_grid.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace ic2d {

enum class NavPathStatus {
    found,
    start_out_of_bounds,
    goal_out_of_bounds,
    start_blocked,
    goal_blocked,
    unreachable,
};

[[nodiscard]] std::string_view nav_path_status_name(NavPathStatus status) noexcept;

struct NavPathResult {
    NavPathStatus status{NavPathStatus::unreachable};
    // Start and goal are both present when status is found.
    std::vector<NavCell> cells;
    float total_distance{0.0F};
    std::size_t expanded_cell_count{0};
};

// Deterministic eight-way A* over NavGrid's public movement contract. Results
// are copied and retain no pointer into the grid. Equal candidates use a stable
// heuristic/row/column tie-break; hard blocks are never assigned a high cost.
[[nodiscard]] NavPathResult find_nav_path(const NavGrid& grid, NavCell start, NavCell goal);

// How firmly an actor is kept off nearby solid ground.
struct NavClearanceSettings {
    // Blocked ground nearer than this pushes back. Roughly one body width plus
    // the margin a reader would call "not scraping the wall".
    float radius{26.0F};
    // Weight of the push relative to the unit desired direction.
    float strength{0.85F};
};

// Bends a desired direction away from solid ground close to the actor.
//
// The grid decides which cells an agent may occupy and the search routes
// between them, but neither says how close to a wall an actor may pass. Smoothed
// routes in particular cut corners deliberately, which without this term means
// walking into the corner and scraping along it until the collision response
// happens to free the actor. This is the missing clearance term and nothing
// else: it reads copied topology and returns an adjusted unit direction.
//
// A zero desired direction is still adjusted, so an actor standing in a corner
// is pushed clear rather than left in it. When nothing is near, the desired
// direction is returned unchanged.
//
// The adjustment is continuous in distance and never opposes a non-zero desired
// direction. Both matter: a term that answers very differently either side of
// its radius, or that can point an actor back the way it came, leaves an actor
// whose route ends at a wall oscillating on the spot rather than settling
// against it.
[[nodiscard]] Vec2 nav_avoid_obstacles(const NavGrid& grid, Vec2 position, Vec2 desired,
                                       NavClearanceSettings settings = {});

// True when an agent may walk the straight line between two world points.
//
// A route is a chain of cell centres, and following it centre by centre makes an
// actor walk a staircase: it must arrive near the middle of every cell it passes
// through before it may turn. Line of sight is what removes that. An agent that
// can see a later waypoint walks straight to it, so an open room is crossed in a
// straight line and a corner is rounded rather than squared.
//
// The walk enters every cell the segment touches, so a diagonal that clips the
// corner of a block is rejected exactly as the search's no-corner-cutting rule
// rejects it. Elevation is checked between consecutive cells with the same
// max-step rule the grid enforces for neighbours. Endpoints outside the grid or
// on unwalkable cells are not visible.
[[nodiscard]] bool nav_line_of_sight(const NavGrid& grid, Vec2 from_world, Vec2 to_world);

// World bounds are converted through NavGrid's half-open cell policy before
// the same cell search runs.
[[nodiscard]] NavPathResult find_nav_path_world(const NavGrid& grid, Vec2 start_world,
                                                Vec2 goal_world);

} // namespace ic2d
