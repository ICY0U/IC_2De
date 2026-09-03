#include "ic2d/scene_submission.hpp"

#include <algorithm>
#include <cmath>

namespace ic2d {

[[nodiscard]] GroundQuadSubmission2D
project_ground_quad(const std::uint64_t stable_id, const RectXZ& bounds, const float elevation,
                    const ColorRgba8 tint, const Camera25DState& camera) {
    const float maximum_x = bounds.x + bounds.width;
    const float maximum_z = bounds.z + bounds.depth;
    return {
        .stable_id = stable_id,
        .points = {{
            project_world_point({bounds.x, elevation, bounds.z}, camera).position,
            project_world_point({maximum_x, elevation, bounds.z}, camera).position,
            project_world_point({maximum_x, elevation, maximum_z}, camera).position,
            project_world_point({bounds.x, elevation, maximum_z}, camera).position,
        }},
        .tint = tint,
    };
}

[[nodiscard]] GroundQuadSubmission2D project_physics_footprint(const PhysicsFootprint& footprint,
                                                               const ColorRgba8 tint,
                                                               const Camera25DState& camera) {
    const float cosine = std::cos(footprint.rotation_radians);
    const float sine = std::sin(footprint.rotation_radians);
    const std::array<Vec2, 4> local_points{{
        {-footprint.half_extents.x, -footprint.half_extents.y},
        {footprint.half_extents.x, -footprint.half_extents.y},
        {footprint.half_extents.x, footprint.half_extents.y},
        {-footprint.half_extents.x, footprint.half_extents.y},
    }};
    GroundQuadSubmission2D projected{
        .stable_id = 50'000ULL + (static_cast<std::uint64_t>(footprint.body.generation) << 32U) +
                     footprint.body.slot,
        .tint = tint,
    };
    for (std::size_t index = 0; index < local_points.size(); ++index) {
        const Vec2 local = local_points[index];
        const float world_x = footprint.center.x + cosine * local.x - sine * local.y;
        const float world_z = footprint.center.y + sine * local.x + cosine * local.y;
        projected.points[index] = project_world_point({world_x, 0.35F, world_z}, camera).position;
    }
    return projected;
}

// Authored ground: the walkable plane and every elevation surface. Ground
// quads render in submission order, so gameplay surfaces go down first.
void submit_world_ground(RenderQueue2D& queue, const GroundMapDefinition& ground,
                         const Camera25DState& camera) {
    queue.submit_ground(
        project_ground_quad(1, ground.walkable_bounds, 0.0F, {22, 61, 57, 218}, camera));
    std::uint64_t ground_id = 2;
    for (const GroundArea& area : ground.areas) {
        if (area.kind == GroundAreaKind::elevation) {
            queue.submit_ground(project_ground_quad(ground_id++, area.bounds, area.elevation,
                                                    {48, 103, 78, 238}, camera));
        }
    }
}

void submit_scene_sprites(RenderQueue2D& queue, const std::vector<RenderItem2D>& render_items,
                          const Camera25DState& camera) {
    for (const RenderItem2D& item : render_items) {
        // A sprite with no depth span is one billboard at one depth. A spanned
        // sprite is a surface running away from the camera: it is submitted as
        // overlapping slices so that an actor standing beside its middle sorts
        // in front of the near half and behind the far half. Slices share the
        // entity's stable id because their depths already order them, and the
        // id only ever breaks ties between different entities.
        const DepthSlicePlan plan =
            plan_depth_slices(item.transform.position.z, item.sprite.depth_span,
                              item.sprite.size.y * item.transform.scale.y, camera.pitch_degrees);
        for (int slice = 0; slice < plan.count; ++slice) {
            const Vec3 position{
                item.transform.position.x,
                item.transform.position.y,
                plan.first_center_z + plan.step * static_cast<float>(slice),
            };
            const ProjectedPoint25D projected = project_world_point(position, camera);
            queue.submit(SpriteSubmission2D{
                .stable_id = item.entity.value,
                .texture = item.sprite.texture,
                .source = item.sprite.source,
                .flip_x = item.sprite.flip_x,
                .position = projected.position,
                .size = item.sprite.size,
                .scale = {item.transform.scale.x, item.transform.scale.y},
                .normalized_origin = item.sprite.normalized_origin,
                .rotation_degrees = item.sprite.rotation_degrees,
                .sort_depth = projected.depth,
                .tint = item.sprite.tint,
                .layer = item.sprite.layer,
            });
        }
    }
}

void submit_projectile_sprites(RenderQueue2D& queue, const ProjectileSimulationSnapshot& snapshot,
                               const Camera25DState& camera, const float interpolation_alpha) {
    constexpr std::uint64_t projectile_stable_id_base = std::uint64_t{1} << 63U;
    for (const ProjectileStateSnapshot& projectile : snapshot.active) {
        const Vec3 position{
            projectile.previous_position.x +
                (projectile.position.x - projectile.previous_position.x) * interpolation_alpha,
            projectile.previous_position.y +
                (projectile.position.y - projectile.previous_position.y) * interpolation_alpha,
            projectile.previous_position.z +
                (projectile.position.z - projectile.previous_position.z) * interpolation_alpha,
        };
        const ProjectedPoint25D projected = project_world_point(position, camera);
        const ProjectedPoint25D projected_previous =
            project_world_point(projectile.previous_position, camera);
        const float rotation = std::atan2(projected.position.y - projected_previous.position.y,
                                          projected.position.x - projected_previous.position.x) *
                               (180.0F / 3.14159265358979323846F);
        const float screen_delta_x = projected.position.x - projected_previous.position.x;
        const float screen_delta_y = projected.position.y - projected_previous.position.y;
        const float screen_length =
            std::sqrt(screen_delta_x * screen_delta_x + screen_delta_y * screen_delta_y);
        const float inverse_screen_length = screen_length > 0.0001F ? 1.0F / screen_length : 0.0F;
        const Vec2 trail_position{
            projected.position.x - screen_delta_x * inverse_screen_length * 3.0F,
            projected.position.y - screen_delta_y * inverse_screen_length * 3.0F,
        };
        const std::uint64_t stable_id = projectile_stable_id_base + projectile.projectile_id * 4U;
        queue.submit(SpriteSubmission2D{
            .stable_id = stable_id,
            .position = trail_position,
            .size = {std::clamp(screen_length + 2.0F, 7.0F, 14.0F), 5.0F},
            .normalized_origin = {0.5F, 0.5F},
            .rotation_degrees = rotation,
            .sort_depth = projected.depth,
            .tint = {13, 30, 29, 190},
            .layer = 50,
        });
        queue.submit(SpriteSubmission2D{
            .stable_id = stable_id + 1U,
            .position = projected.position,
            .size = {8.0F, 3.0F},
            .normalized_origin = {0.5F, 0.5F},
            .rotation_degrees = rotation,
            .sort_depth = projected.depth,
            .tint = {248, 194, 89, 255},
            .layer = 50,
        });
        queue.submit(SpriteSubmission2D{
            .stable_id = stable_id + 2U,
            .position = projected.position,
            .size = {5.0F, 1.0F},
            .normalized_origin = {0.5F, 0.5F},
            .rotation_degrees = rotation,
            .sort_depth = projected.depth,
            .tint = {195, 255, 240, 255},
            .layer = 50,
        });
        if (projectile.weapon == WeaponKind::needle_pistol &&
            projectile.lifetime_ticks_remaining + 1U == needle_pistol.projectile_lifetime_ticks) {
            queue.submit(SpriteSubmission2D{
                .stable_id = stable_id + 3U,
                .position = projected_previous.position,
                .size = {6.0F, 6.0F},
                .normalized_origin = {0.5F, 0.5F},
                .rotation_degrees = 45.0F,
                .sort_depth = projected_previous.depth,
                .tint = {248, 194, 89, 235},
                .layer = 51,
            });
        }
    }
}

// Development channels draw over authored ground using their own id range, so
// gameplay submission never has to thread an id counter through debug code.
void submit_navigation_grid(RenderQueue2D& queue, const NavGridSnapshot& navigation,
                            const Camera25DState& camera, const DebugVisuals& debug_visuals) {
    if (!debug_visuals.draws(DebugChannel::navigation_grid)) {
        return;
    }
    constexpr std::uint64_t navigation_id_base = 20'000;
    constexpr float inset = 0.55F;
    const float maximum_x = navigation.bounds.x + navigation.bounds.width;
    const float maximum_z = navigation.bounds.z + navigation.bounds.depth;
    for (std::size_t index = 0; index < navigation.cells.size(); ++index) {
        const NavGridCell& cell = navigation.cells[index];
        const float left =
            std::max(navigation.bounds.x, cell.center.x - navigation.cell_size * 0.5F) + inset;
        const float near_z =
            std::max(navigation.bounds.z, cell.center.y - navigation.cell_size * 0.5F) + inset;
        const float right =
            std::min(maximum_x, cell.center.x + navigation.cell_size * 0.5F) - inset;
        const float far_z =
            std::min(maximum_z, cell.center.y + navigation.cell_size * 0.5F) - inset;
        if (!(right > left) || !(far_z > near_z)) {
            continue;
        }
        const ColorRgba8 tint =
            cell.walkable ? ColorRgba8{74, 222, 190, 65} : ColorRgba8{224, 74, 86, 150};
        queue.submit_ground(project_ground_quad(navigation_id_base + index,
                                                {left, near_z, right - left, far_z - near_z},
                                                cell.elevation + 0.20F, tint, camera));
    }
}

void submit_navigation_path(RenderQueue2D& queue, const NavGridSnapshot& navigation,
                            const NavPathResult& path, const Camera25DState& camera,
                            const DebugVisuals& debug_visuals) {
    if (!debug_visuals.draws(DebugChannel::navigation_path) ||
        path.status != NavPathStatus::found) {
        return;
    }
    constexpr std::uint64_t path_id_base = 30'000;
    constexpr float half_size_scale = 0.28F;
    for (std::size_t index = 0; index < path.cells.size(); ++index) {
        const NavCell path_cell = path.cells[index];
        if (path_cell.column < 0 || path_cell.row < 0 || path_cell.column >= navigation.columns ||
            path_cell.row >= navigation.rows) {
            continue;
        }
        const std::size_t cell_index =
            static_cast<std::size_t>(path_cell.row) * static_cast<std::size_t>(navigation.columns) +
            static_cast<std::size_t>(path_cell.column);
        const NavGridCell& cell = navigation.cells[cell_index];
        const float half_size = navigation.cell_size * half_size_scale;
        const bool start = index == 0;
        const bool goal = index + 1 == path.cells.size();
        const ColorRgba8 tint = start  ? ColorRgba8{74, 222, 190, 235}
                                : goal ? ColorRgba8{224, 90, 205, 235}
                                       : ColorRgba8{248, 194, 89, 205};
        queue.submit_ground(
            project_ground_quad(path_id_base + index,
                                {cell.center.x - half_size, cell.center.y - half_size,
                                 half_size * 2.0F, half_size * 2.0F},
                                cell.elevation + 0.35F, tint, camera));
    }
}

void submit_debug_ground(RenderQueue2D& queue, const GroundMapDefinition& ground,
                         const Camera25DState& camera, const DebugVisuals& debug_visuals,
                         const std::vector<PhysicsFootprint>& footprints) {
    constexpr std::uint64_t debug_ground_id_base = 10'000;
    std::uint64_t ground_id = debug_ground_id_base;
    float maximum_authored_elevation = 0.0F;
    for (const GroundArea& area : ground.areas) {
        maximum_authored_elevation = std::max(maximum_authored_elevation, area.elevation);
    }

    for (const GroundArea& area : ground.areas) {
        if (area.kind == GroundAreaKind::elevation) {
            if (debug_visuals.draws(DebugChannel::elevation_map)) {
                queue.submit_ground(project_ground_quad(
                    ground_id++, area.bounds, area.elevation + 0.12F,
                    debug_elevation_tint(area.elevation, maximum_authored_elevation), camera));
            }
            continue;
        }
        const bool solid_area = area.kind == GroundAreaKind::solid;
        if (!debug_visuals.draws(solid_area ? DebugChannel::collision_shapes
                                            : DebugChannel::trigger_volumes)) {
            continue;
        }
        const ColorRgba8 debug_tint =
            solid_area ? ColorRgba8{128, 58, 49, 155} : ColorRgba8{49, 164, 184, 155};
        queue.submit_ground(
            project_ground_quad(ground_id++, area.bounds, 0.15F, debug_tint, camera));
    }

    for (const PhysicsFootprint& footprint : footprints) {
        if (!debug_visuals.draws(footprint.sensor ? DebugChannel::trigger_volumes
                                                  : DebugChannel::collision_shapes)) {
            continue;
        }
        const ColorRgba8 tint =
            footprint.sensor                                        ? ColorRgba8{55, 204, 222, 80}
            : footprint.motion == PhysicsMotionType::dynamic_body   ? ColorRgba8{248, 194, 89, 115}
            : footprint.motion == PhysicsMotionType::kinematic_body ? ColorRgba8{74, 222, 190, 95}
                                                                    : ColorRgba8{170, 112, 220, 70};
        queue.submit_ground(project_physics_footprint(footprint, tint, camera));
    }
}

} // namespace ic2d
