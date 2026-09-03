#pragma once

#include "ic2d/debug_visuals.hpp"
#include "ic2d/ground_map.hpp"
#include "ic2d/nav_grid.hpp"
#include "ic2d/nav_pathfinding.hpp"
#include "ic2d/physics2d.hpp"
#include "ic2d/projectiles.hpp"
#include "ic2d/projection25d.hpp"
#include "ic2d/render2d.hpp"
#include "ic2d/types.hpp"
#include "ic2d/world.hpp"

#include <cstdint>
#include <vector>

namespace ic2d {

// Turning simulation state into render submissions.
//
// These were part of application.cpp, which meant the rules for what the world
// looks like could only be read alongside the frame loop that drives it.
// Nothing here touches raylib or opens a window: each function takes a snapshot
// and a camera and fills a RenderQueue2D, so what gets drawn can be examined
// without a GPU.
//
// The debug submissions are compiled unconditionally. They were previously
// behind IC2DE_ENABLE_DEVELOPMENT_TOOLS only because an unused static function
// warns under /W4; as module functions nothing is unused, and DebugVisuals
// decides at run time whether a channel draws at all.

// Projects an axis-aligned world rectangle into a submitted ground quad.
[[nodiscard]] GroundQuadSubmission2D project_ground_quad(std::uint64_t stable_id,
                                                         const RectXZ& bounds, float elevation,
                                                         ColorRgba8 tint,
                                                         const Camera25DState& camera);

// Projects a physics body's footprint, which unlike a ground tile may be
// rotated, into a submitted quad.
[[nodiscard]] GroundQuadSubmission2D project_physics_footprint(const PhysicsFootprint& footprint,
                                                               ColorRgba8 tint,
                                                               const Camera25DState& camera);

void submit_world_ground(RenderQueue2D& queue, const GroundMapDefinition& ground,
                         const Camera25DState& camera);

void submit_scene_sprites(RenderQueue2D& queue, const std::vector<RenderItem2D>& render_items,
                          const Camera25DState& camera);

void submit_projectile_sprites(RenderQueue2D& queue, const ProjectileSimulationSnapshot& snapshot,
                               const Camera25DState& camera, float interpolation_alpha);

void submit_navigation_grid(RenderQueue2D& queue, const NavGridSnapshot& navigation,
                            const Camera25DState& camera, const DebugVisuals& debug_visuals);

void submit_navigation_path(RenderQueue2D& queue, const NavGridSnapshot& navigation,
                            const NavPathResult& path, const Camera25DState& camera,
                            const DebugVisuals& debug_visuals);

void submit_debug_ground(RenderQueue2D& queue, const GroundMapDefinition& ground,
                         const Camera25DState& camera, const DebugVisuals& debug_visuals,
                         const std::vector<PhysicsFootprint>& footprints);

} // namespace ic2d
