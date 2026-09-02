#pragma once

#include "ic2d/nav_grid.hpp"
#include "ic2d/types.hpp"

#include <cstddef>
#include <vector>

namespace ic2d {

// One shared steering field over a NavGrid, built outward from a single goal.
//
// Per-agent A* answers "how does this one actor reach that goal", and costs a
// search per actor. A crowd converging on one goal asks the same question
// thousands of times and throws away almost all of the shared work. A flow
// field inverts that: one pass over the grid stores, for every cell, the
// direction of the cheapest route to the goal, after which an actor's steering
// is a lookup rather than a search. Cost becomes a function of the map instead
// of the crowd, which is what makes tens of thousands of actors affordable.
//
// The field borrows nothing from the grid after rebuild() returns, and answers
// only questions about the goal it was last built for. Cells that cannot reach
// the goal keep a zero direction, so callers can distinguish "no route" from
// "already arrived" and apply their own policy.
class FlowField final {
public:
    // Rebuilds for a new goal, reusing existing storage when the grid's
    // dimensions are unchanged. False means the goal is out of bounds or
    // blocked, and the field is left empty.
    //
    // Regions that cannot reach the goal are not left blank. Their cells take a
    // straight-line potential toward the goal instead of a routed one, so a
    // crowd walled off from its target presses on whichever face of the barrier
    // it is already nearest rather than all funnelling to one point of it.
    //
    // goal_position is the target's exact location, used for the straight-line
    // potential. Falling back to the goal cell's centre would bias it by up to
    // half a cell, and a crowd pressing on a symmetric barrier turns even that
    // much into one preferred face: actors squeezed round a corner cannot climb
    // back, so the bias ratchets the whole crowd onto one side.
    [[nodiscard]] bool rebuild(const NavGrid& grid, NavCell goal, Vec2 goal_position);

    [[nodiscard]] bool built() const noexcept;
    [[nodiscard]] NavCell goal() const noexcept;
    // Cells that received a route, which after seeding is every walkable cell
    // whose region contains at least one of them.
    [[nodiscard]] std::size_t reachable_cell_count() const noexcept;

    // Unit direction toward the goal, or zero outside the field, on a blocked
    // cell and at the goal itself.
    //
    // The four cells nearest the sample are blended by their share of it. A
    // per-cell answer gives every actor in a cell one of eight directions,
    // which funnels a crowd into visible single-file lanes along the cell
    // boundaries; blending removes the seams and lets the crowd spread.
    [[nodiscard]] Vec2 direction_at(Vec2 world_position) const noexcept;

    // Route length in world units, or infinity where there is no route.
    [[nodiscard]] float cost_at(Vec2 world_position) const noexcept;

private:
    [[nodiscard]] bool contains(NavCell cell) const noexcept;
    [[nodiscard]] std::size_t offset(NavCell cell) const noexcept;

    RectXZ bounds_{};
    float cell_size_{0.0F};
    std::int32_t columns_{0};
    std::int32_t rows_{0};
    NavCell goal_{};
    bool built_{false};
    std::size_t reachable_cell_count_{0};
    std::vector<float> cost_;
    std::vector<Vec2> direction_;
};

} // namespace ic2d
