#include "ic2d/runtime_scene.hpp"

#include "ic2d/jobs.hpp"

#include "ic2d/locomotion.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ic2d {
namespace {

constexpr std::uint32_t player_seated_idle_delay_ticks = 180;
constexpr std::uint32_t player_shoot_presentation_ticks = 9;

struct SegmentBoxHit {
    Vec2 point{};
    Vec2 normal{};
    float fraction{0.0F};
};

// Slab intersection of a segment against an axis-aligned box. A segment that
// begins inside the box reports no hit, matching the broadphase cast this
// stands in for, so a projectile cannot be stopped by something it spawned on.
[[nodiscard]] std::optional<SegmentBoxHit> segment_box_hit(const Vec2& start, const Vec2& end,
                                                           const Vec2& centre,
                                                           const Vec2& half_extents) noexcept {
    const Vec2 delta{end.x - start.x, end.y - start.y};
    const Vec2 minimum{centre.x - half_extents.x, centre.y - half_extents.y};
    const Vec2 maximum{centre.x + half_extents.x, centre.y + half_extents.y};
    float entry = 0.0F;
    float exit = 1.0F;
    int entry_axis = -1;
    float entry_sign = 0.0F;
    const float axis_start[2]{start.x, start.y};
    const float axis_delta[2]{delta.x, delta.y};
    const float axis_minimum[2]{minimum.x, minimum.y};
    const float axis_maximum[2]{maximum.x, maximum.y};
    for (int axis = 0; axis < 2; ++axis) {
        if (std::abs(axis_delta[axis]) < 1e-8F) {
            if (axis_start[axis] < axis_minimum[axis] || axis_start[axis] > axis_maximum[axis]) {
                return std::nullopt;
            }
            continue;
        }
        const float inverse = 1.0F / axis_delta[axis];
        float near_hit = (axis_minimum[axis] - axis_start[axis]) * inverse;
        float far_hit = (axis_maximum[axis] - axis_start[axis]) * inverse;
        float sign = -1.0F;
        if (near_hit > far_hit) {
            std::swap(near_hit, far_hit);
            sign = 1.0F;
        }
        if (near_hit > entry) {
            entry = near_hit;
            entry_axis = axis;
            entry_sign = sign;
        }
        exit = std::min(exit, far_hit);
        if (entry > exit) {
            return std::nullopt;
        }
    }
    if (entry_axis < 0) {
        // The segment began inside the box on every axis it could test.
        return std::nullopt;
    }
    return SegmentBoxHit{
        .point = {start.x + delta.x * entry, start.y + delta.y * entry},
        .normal = entry_axis == 0 ? Vec2{entry_sign, 0.0F} : Vec2{0.0F, entry_sign},
        .fraction = entry,
    };
}

[[nodiscard]] bool finite(const Vec2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] TextureSampling to_texture_sampling(const SceneTextureSampling sampling) noexcept {
    return sampling == SceneTextureSampling::pixel ? TextureSampling::pixel
                                                   : TextureSampling::smooth;
}

[[nodiscard]] bool footprint_fits_ground(const GroundMapDefinition& ground, const Vec2 position,
                                         const Vec2 half_extents) noexcept {
    const RectXZ& bounds = ground.walkable_bounds;
    if (position.x - half_extents.x < bounds.x ||
        position.x + half_extents.x > bounds.x + bounds.width ||
        position.y - half_extents.y < bounds.z ||
        position.y + half_extents.y > bounds.z + bounds.depth) {
        return false;
    }
    for (const GroundArea& area : ground.areas) {
        if (area.kind != GroundAreaKind::solid) {
            continue;
        }
        const bool overlaps = position.x + half_extents.x > area.bounds.x &&
                              position.x - half_extents.x < area.bounds.x + area.bounds.width &&
                              position.y + half_extents.y > area.bounds.z &&
                              position.y - half_extents.y < area.bounds.z + area.bounds.depth;
        if (overlaps) {
            return false;
        }
    }
    return true;
}

} // namespace

[[nodiscard]] std::uint64_t physics_body_key(const PhysicsBodyId body) noexcept {
    return (static_cast<std::uint64_t>(body.generation) << 32U) |
           static_cast<std::uint64_t>(body.slot);
}

struct RuntimeScene::Impl {
    struct BoundEntity {
        EntityId entity{};
        Vec3 offset{};
    };

    // Entities taken out of play by gameplay, such as a used pickup. They are
    // hidden rather than destroyed so reset() can bring them back, and are
    // keyed by stable UUID because a World identity is transient.
    std::unordered_set<std::uint64_t> retired_entities;

    [[nodiscard]] bool is_retired_entity(const EntityId entity) const noexcept {
        if (retired_entities.empty()) {
            return false;
        }
        const std::optional<EntityUuid> uuid = world.uuid(entity);
        return uuid && retired_entities.contains(uuid->value);
    }

    // Whether the entity is drawn this frame: neither retired itself, nor
    // bound to a body that has been taken out of play. Render collection and
    // every editor decoration ask this one question, so what the viewport
    // shows stays exactly what the pointer can reach and the outline can find.
    [[nodiscard]] bool is_presented(const EntityId entity) const noexcept {
        if (is_retired_entity(entity)) {
            return false;
        }
        const auto found = entity_bindings.find(entity.value);
        return found == entity_bindings.end() ||
               body_bindings[found->second.body_index].presentation_active;
    }

    struct BodyBinding {
        std::string authored_id;
        ScenePhysicsRole role{ScenePhysicsRole::generic};
        PhysicsBodyId physics_body{};
        PhysicsBoxDefinition definition{};
        Vec3 start_position{};
        Vec3 previous_position{};
        Vec3 current_position{};
        std::vector<BoundEntity> entities;
        bool active{true};
        bool presentation_active{true};
        bool hold_presentation_until_terminal{false};
        // Runtime crowd copies carry no rigid body. Their movement is already
        // decided by ground sliding and steering, and a kinematic body neither
        // collides with other kinematic bodies nor contributes anything the
        // simulation reads back, so stepping thousands of them is pure cost.
        // Segment casts reach them through the actor index instead.
        bool physics_backed{true};
        // The direction the actor asked to move this tick, as distinct from
        // the one it managed to. Presentation reads this: an actor held by a
        // wall still slides a little along it, and the direction of that
        // scraping says nothing about where the actor is trying to go.
        Vec2 requested_direction{};
    };

    struct EntityBindingLookup {
        std::size_t body_index{0};
        Vec3 offset{};
    };

    struct AnimatedEntity {
        std::string authored_entity_id;
        EntityUuid entity_uuid{};
        EntityId entity{};
        std::optional<std::size_t> body_index;
        std::uint32_t initial_tick_offset{0};
        LocomotionState initial_state{LocomotionState::idle_south};
        LocomotionState facing_state{LocomotionState::idle_south};
        LocomotionState current_state{LocomotionState::idle_south};
        std::array<std::string, locomotion_state_count> state_clips;
        std::optional<LocomotionState> reaction_state;
        bool terminal_sequence{false};
        AnimationPlayer player;
    };

    Impl(SceneDefinition authored_definition, TextureAssets& texture_assets)
        : definition{std::move(authored_definition)}, textures{&texture_assets},
          ground_map{definition.ground()}, physics{definition.simulation().physics} {
        try {
            load_textures();
            index_animation_clips();
            build_ground_physics();
            build_authored_bodies();
            build_entities();
            world.flush();
            build_animations();
            initialize_runtime_uuid();
        } catch (...) {
            release_textures();
            throw;
        }
    }

    ~Impl() { release_textures(); }

    void load_textures() {
        for (const SceneTextureDefinition& texture : definition.textures()) {
            TextureHandle handle{};
            if (texture.kind == SceneTextureKind::checker) {
                handle = textures->create_checker(texture.id, texture.width, texture.height,
                                                  texture.cell_size, texture.first_color,
                                                  texture.second_color,
                                                  to_texture_sampling(texture.sampling));
            } else if (texture.kind == SceneTextureKind::radial) {
                handle = textures->create_radial_gradient(texture.id, texture.width, texture.height,
                                                          texture.first_color, texture.second_color,
                                                          to_texture_sampling(texture.sampling));
            } else {
                handle = textures->acquire(definition.resolve_asset(texture.relative_path),
                                           to_texture_sampling(texture.sampling));
            }
            const auto info = textures->info(handle);
            if (!info || info->fallback) {
                throw std::runtime_error{"Scene texture could not be created: " + texture.id};
            }
            texture_handles.emplace(texture.id, handle);
            owned_textures.push_back(handle);
        }
    }

    void release_textures() noexcept {
        if (textures == nullptr) {
            return;
        }
        for (const TextureHandle handle : owned_textures) {
            textures->release(handle);
        }
        owned_textures.clear();
        texture_handles.clear();
    }

    void index_animation_clips() {
        for (const SceneAnimationClipDefinition& clip : definition.animation_clips()) {
            animation_clip_definitions.emplace(clip.clip.id, &clip);
        }
    }

    void initialize_runtime_uuid() {
        std::uint64_t maximum_uuid = 0;
        for (const EntityBlueprint& entity : world.snapshot().entities) {
            maximum_uuid = std::max(maximum_uuid, entity.uuid.value);
        }
        for (const ScenePrefabDefinition& prefab : definition.prefabs()) {
            maximum_uuid = std::max(maximum_uuid, prefab.uuid.value);
        }
        if (maximum_uuid == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error{"Runtime entity UUID space is exhausted."};
        }
        next_runtime_uuid = maximum_uuid + 1;
    }

    [[nodiscard]] EntityUuid allocate_runtime_uuid() {
        if (next_runtime_uuid == 0) {
            throw std::overflow_error{"Runtime entity UUID space is exhausted."};
        }
        const EntityUuid result{next_runtime_uuid};
        ++next_runtime_uuid;
        return result;
    }

    [[nodiscard]] PhysicsBodyId create_static_footprint(const RectXZ& bounds,
                                                        const std::uint32_t tag,
                                                        const std::uint64_t category_bits,
                                                        const std::uint64_t mask_bits,
                                                        const bool sensor) {
        const PhysicsBodyId body = physics.create_box({
            .motion = PhysicsMotionType::static_body,
            .center = {bounds.x + bounds.width * 0.5F, bounds.z + bounds.depth * 0.5F},
            .half_extents = {bounds.width * 0.5F, bounds.depth * 0.5F},
            .category_bits = category_bits,
            .mask_bits = mask_bits,
            .tag = tag,
            .sensor = sensor,
        });
        ++physics_body_count;
        return body;
    }

    void build_ground_physics() {
        const SceneSimulationDefinition& simulation = definition.simulation();
        std::uint32_t static_tag = 100;
        for (const GroundArea& area : definition.ground().areas) {
            if (area.kind == GroundAreaKind::solid) {
                static_cast<void>(create_static_footprint(area.bounds, static_tag++,
                                                          simulation.ground_category_bits,
                                                          simulation.ground_mask_bits, false));
            } else if (area.kind == GroundAreaKind::trigger) {
                static_cast<void>(create_static_footprint(area.bounds, area.tag,
                                                          simulation.trigger_category_bits,
                                                          simulation.trigger_mask_bits, true));
            }
        }

        const float thickness = simulation.world_boundary_thickness;
        const RectXZ& bounds = definition.ground().walkable_bounds;
        static_cast<void>(create_static_footprint(
            {bounds.x - thickness, bounds.z, thickness, bounds.depth}, static_tag++,
            simulation.ground_category_bits, simulation.ground_mask_bits, false));
        static_cast<void>(create_static_footprint(
            {bounds.x + bounds.width, bounds.z, thickness, bounds.depth}, static_tag++,
            simulation.ground_category_bits, simulation.ground_mask_bits, false));
        static_cast<void>(create_static_footprint(
            {bounds.x, bounds.z - thickness, bounds.width, thickness}, static_tag++,
            simulation.ground_category_bits, simulation.ground_mask_bits, false));
        static_cast<void>(create_static_footprint(
            {bounds.x, bounds.z + bounds.depth, bounds.width, thickness}, static_tag,
            simulation.ground_category_bits, simulation.ground_mask_bits, false));
    }

    void build_authored_bodies() {
        body_bindings.reserve(definition.physics_bodies().size());
        for (const ScenePhysicsBodyDefinition& authored : definition.physics_bodies()) {
            const float elevation = ground_map.elevation_at(authored.box.center);
            BodyBinding binding{
                .authored_id = authored.id,
                .role = authored.role,
                .physics_body = physics.create_box(authored.box),
                .definition = authored.box,
                .start_position = {authored.box.center.x, elevation, authored.box.center.y},
                .previous_position = {authored.box.center.x, elevation, authored.box.center.y},
                .current_position = {authored.box.center.x, elevation, authored.box.center.y},
            };
            ++physics_body_count;
            const std::size_t index = body_bindings.size();
            body_indices.emplace(binding.authored_id, index);
            if (binding.role == ScenePhysicsRole::player) {
                player_index = index;
            } else if (binding.role == ScenePhysicsRole::primary_prop) {
                primary_prop_index = index;
            }
            body_bindings.push_back(std::move(binding));
            register_body_index(body_bindings.size() - 1U);
        }
    }

    void build_entities() {
        for (const SceneEntityDefinition& authored : definition.entities()) {
            TextureHandle texture{};
            if (!authored.sprite.texture_id.empty()) {
                texture = texture_handles.at(authored.sprite.texture_id);
            }
            const EntityId entity = world.queue_spawn({
                .uuid = authored.uuid,
                .name = authored.name,
                .transform = {.position = authored.position},
                .sprite =
                    Sprite2D{
                        .texture = texture,
                        .size = authored.sprite.size,
                        .normalized_origin = authored.sprite.normalized_origin,
                        .tint = authored.sprite.tint,
                        .layer = authored.sprite.layer,
                        .depth_span = authored.sprite.depth_span,
                    },
            });
            entity_ids.emplace(authored.id, entity);
            if (authored.physics_binding.empty()) {
                continue;
            }
            const std::size_t body_index = body_indices.at(authored.physics_binding);
            BodyBinding& body = body_bindings[body_index];
            const Vec3 offset{
                authored.position.x - body.start_position.x,
                authored.position.y - body.start_position.y,
                authored.position.z - body.start_position.z,
            };
            body.entities.push_back({.entity = entity, .offset = offset});
            if (body.role == ScenePhysicsRole::player && !player_uuid) {
                player_uuid = authored.uuid;
            }
            entity_bindings.emplace(entity.value, EntityBindingLookup{body_index, offset});
        }
    }

    void build_animations() {
        for (const auto& [clip_id, clip] : animation_clip_definitions) {
            animation_clip_textures.emplace(clip_id, texture_handles.at(clip->texture_id));
        }

        animated_entities.reserve(definition.animation_bindings().size() +
                                  definition.auto_animations().size());
        for (const SceneAnimationBindingDefinition& binding : definition.animation_bindings()) {
            const auto authored_entity = std::ranges::find(definition.entities(), binding.entity_id,
                                                           &SceneEntityDefinition::id);
            if (authored_entity == definition.entities().end()) {
                throw std::logic_error{"Validated animation entity disappeared."};
            }
            std::vector<AnimationClip> clips;
            std::unordered_set<std::string> included_clips;
            for (const std::string& clip_id : binding.state_clips) {
                if (!clip_id.empty() && included_clips.insert(clip_id).second) {
                    clips.push_back(animation_clip_definitions.at(clip_id)->clip);
                }
            }
            const std::size_t body_index = body_indices.at(authored_entity->physics_binding);
            const std::string& initial_clip =
                binding.state_clips[static_cast<std::size_t>(binding.initial_state)];
            const std::size_t animation_index = animated_entities.size();
            animated_entities.push_back({
                .authored_entity_id = binding.entity_id,
                .entity_uuid = authored_entity->uuid,
                .entity = entity_ids.at(binding.entity_id),
                .body_index = body_index,
                .initial_state = binding.initial_state,
                .facing_state = idle_locomotion(binding.initial_state),
                .current_state = binding.initial_state,
                .state_clips = binding.state_clips,
                .player = AnimationPlayer{std::move(clips), initial_clip},
            });
            if (body_bindings[body_index].role == ScenePhysicsRole::player) {
                // The locomotion-bound entity is the controllable actor. This
                // deliberately overrides any helper sprite (for example the
                // player shadow) authored against the same physics body.
                player_uuid = authored_entity->uuid;
            }
            animation_indices.emplace(animated_entities.back().entity.value, animation_index);
        }

        for (const SceneAutoAnimationDefinition& binding : definition.auto_animations()) {
            const auto authored_entity = std::ranges::find(definition.entities(), binding.entity_id,
                                                           &SceneEntityDefinition::id);
            if (authored_entity == definition.entities().end()) {
                throw std::logic_error{"Validated automatic-animation entity disappeared."};
            }
            const SceneAnimationClipDefinition* clip =
                animation_clip_definitions.at(binding.clip_id);
            AnimationPlayer player{{clip->clip}, binding.clip_id};
            static_cast<void>(player.advance(binding.initial_tick_offset));
            const std::size_t animation_index = animated_entities.size();
            animated_entities.push_back({
                .authored_entity_id = binding.entity_id,
                .entity_uuid = authored_entity->uuid,
                .entity = entity_ids.at(binding.entity_id),
                .body_index = std::nullopt,
                .initial_tick_offset = binding.initial_tick_offset,
                .player = std::move(player),
            });
            animation_indices.emplace(animated_entities.back().entity.value, animation_index);
        }
    }

    [[nodiscard]] AnimationPlayer
    make_locomotion_player(const std::array<std::string, locomotion_state_count>& state_clips,
                           const LocomotionState initial_state) const {
        std::vector<AnimationClip> clips;
        std::unordered_set<std::string> included_clips;
        for (const std::string& clip_id : state_clips) {
            if (!clip_id.empty() && included_clips.insert(clip_id).second) {
                clips.push_back(animation_clip_definitions.at(clip_id)->clip);
            }
        }
        return AnimationPlayer{
            std::move(clips),
            state_clips[static_cast<std::size_t>(initial_state)],
        };
    }

    [[nodiscard]] AnimatedEntity* animated_actor(const EntityUuid actor) noexcept {
        const std::optional<EntityId> entity = world.find(actor);
        if (!entity) {
            return nullptr;
        }
        const auto animation = animation_indices.find(entity->value);
        return animation == animation_indices.end() ? nullptr
                                                    : &animated_entities[animation->second];
    }

    [[nodiscard]] bool play_actor_reaction(const EntityUuid actor, const LocomotionState state,
                                           const bool terminal) {
        AnimatedEntity* const animation = animated_actor(actor);
        if (animation == nullptr || !animation->body_index) {
            return false;
        }
        BodyBinding& body = body_bindings[*animation->body_index];
        if (!body.active || animation->terminal_sequence) {
            return false;
        }
        const std::string& clip_id = animation->state_clips[static_cast<std::size_t>(state)];
        if (clip_id.empty()) {
            return false;
        }
        static_cast<void>(animation->player.play(clip_id, true));
        animation->reaction_state = state;
        animation->terminal_sequence = terminal;
        if (terminal) {
            body.hold_presentation_until_terminal = true;
        }
        return true;
    }

    [[nodiscard]] std::vector<EntityUuid>
    spawn_actor_copies(const ScenePhysicsRole role, const std::vector<Vec2>& ground_positions) {
        if (simulation_started) {
            throw std::logic_error{"Runtime actor copies must be created before fixed tick one."};
        }
        if (ground_positions.empty()) {
            return {};
        }
        const auto source_body = std::ranges::find(body_bindings, role, &BodyBinding::role);
        if (source_body == body_bindings.end() ||
            source_body->definition.motion != PhysicsMotionType::kinematic_body ||
            source_body->entities.empty() || role == ScenePhysicsRole::player ||
            role == ScenePhysicsRole::primary_prop) {
            throw std::invalid_argument{
                "Runtime actor copies require a bound non-player kinematic source role."};
        }

        const std::size_t source_body_index =
            static_cast<std::size_t>(source_body - body_bindings.begin());
        const std::string source_body_id = source_body->authored_id;
        const PhysicsBoxDefinition source_definition = source_body->definition;
        const std::vector<BoundEntity> source_entities = source_body->entities;
        std::set<std::pair<float, float>> unique_positions;
        for (const Vec2 position : ground_positions) {
            if (!finite(position) || !unique_positions.emplace(position.x, position.y).second ||
                !footprint_fits_ground(definition.ground(), position,
                                       source_definition.half_extents)) {
                throw std::invalid_argument{
                    "Runtime actor copy positions must be finite, unique, and fully walkable."};
            }
        }

        struct AnimationTemplate {
            EntityId source_entity{};
            LocomotionState initial_state{LocomotionState::idle_south};
            std::array<std::string, locomotion_state_count> state_clips;
        };
        std::vector<AnimationTemplate> animation_templates;
        for (const AnimatedEntity& animation : animated_entities) {
            if (animation.body_index && *animation.body_index == source_body_index) {
                animation_templates.push_back({
                    .source_entity = animation.entity,
                    .initial_state = animation.initial_state,
                    .state_clips = animation.state_clips,
                });
            }
        }

        const WorldSnapshot source_world = world.snapshot();
        std::unordered_map<std::uint64_t, const EntityBlueprint*> blueprints;
        blueprints.reserve(source_world.entities.size());
        for (const EntityBlueprint& blueprint : source_world.entities) {
            blueprints.emplace(blueprint.uuid.value, &blueprint);
        }

        std::vector<EntityUuid> actors;
        actors.reserve(ground_positions.size());
        for (const Vec2 position : ground_positions) {
            const std::uint64_t copy_sequence = ++runtime_spawn_sequence;
            PhysicsBoxDefinition body_definition = source_definition;
            body_definition.center = position;
            const float elevation = ground_map.elevation_at(position);
            const std::size_t body_index = body_bindings.size();
            BodyBinding body{
                .authored_id = source_body_id + "-runtime-" + std::to_string(copy_sequence),
                .role = role,
                .physics_body = {},
                .definition = body_definition,
                .start_position = {position.x, elevation, position.y},
                .previous_position = {position.x, elevation, position.y},
                .current_position = {position.x, elevation, position.y},
                .physics_backed = false,
            };

            EntityUuid gameplay_actor{};
            EntityUuid first_bound_entity{};
            for (const BoundEntity& source_entity : source_entities) {
                const std::optional<EntityUuid> source_uuid = world.uuid(source_entity.entity);
                if (!source_uuid) {
                    throw std::logic_error{"Runtime actor source entity lost its stable identity."};
                }
                const auto source_blueprint = blueprints.find(source_uuid->value);
                if (source_blueprint == blueprints.end()) {
                    throw std::logic_error{"Runtime actor source entity lost its World blueprint."};
                }

                EntityBlueprint blueprint = *source_blueprint->second;
                const EntityUuid entity_uuid = allocate_runtime_uuid();
                if (!first_bound_entity) {
                    first_bound_entity = entity_uuid;
                }
                blueprint.uuid = entity_uuid;
                blueprint.name += " [Stress " + std::to_string(copy_sequence) + "]";
                blueprint.transform.position = {
                    position.x + source_entity.offset.x,
                    elevation + source_entity.offset.y,
                    position.y + source_entity.offset.z,
                };
                const EntityId entity = world.queue_spawn(std::move(blueprint));
                body.entities.push_back({.entity = entity, .offset = source_entity.offset});
                entity_bindings.emplace(entity.value,
                                        EntityBindingLookup{body_index, source_entity.offset});
                const std::string runtime_entity_id = "runtime-stress-" +
                                                      std::to_string(copy_sequence) + "-" +
                                                      std::to_string(body.entities.size());
                entity_ids.emplace(runtime_entity_id, entity);

                const auto animation_template = std::ranges::find(
                    animation_templates, source_entity.entity, &AnimationTemplate::source_entity);
                if (animation_template == animation_templates.end()) {
                    continue;
                }
                const std::size_t animation_index = animated_entities.size();
                animated_entities.push_back({
                    .authored_entity_id = runtime_entity_id,
                    .entity_uuid = entity_uuid,
                    .entity = entity,
                    .body_index = body_index,
                    .initial_state = animation_template->initial_state,
                    .facing_state = idle_locomotion(animation_template->initial_state),
                    .current_state = animation_template->initial_state,
                    .state_clips = animation_template->state_clips,
                    .player = make_locomotion_player(animation_template->state_clips,
                                                     animation_template->initial_state),
                });
                animation_indices.emplace(entity.value, animation_index);
                gameplay_actor = entity_uuid;
            }
            if (!gameplay_actor) {
                gameplay_actor = first_bound_entity;
            }
            if (!gameplay_actor) {
                throw std::logic_error{"Runtime actor copy produced no stable gameplay identity."};
            }
            body_indices.emplace(body.authored_id, body_index);
            body_bindings.push_back(std::move(body));
            register_body_index(body_bindings.size() - 1U);
            actor_index.dirty = true;
            actors.push_back(gameplay_actor);
        }
        world.flush();
        return actors;
    }

    void advance_animations(RuntimeSceneTickResult& result) {
        for (AnimatedEntity& animation : animated_entities) {
            if (animation.body_index && !animation.reaction_state) {
                const BodyBinding& body = body_bindings[*animation.body_index];
                const Vec2 delta{
                    body.current_position.x - body.previous_position.x,
                    body.current_position.z - body.previous_position.z,
                };
                const bool moving = std::abs(delta.x) > 0.01F || std::abs(delta.y) > 0.01F;
                // Actors face where they were steering, the way the player
                // faces where the camera says rather than where collision let
                // it go. A blocked actor keeps looking at what it is walking
                // into instead of turning to follow the scrape along the wall.
                const Vec2 steered_direction =
                    body.requested_direction.x != 0.0F || body.requested_direction.y != 0.0F
                        ? body.requested_direction
                        : delta;
                const Vec2 facing_direction = body.role == ScenePhysicsRole::player
                                                  ? player_presentation_direction
                                                  : steered_direction;
                LocomotionState state =
                    locomotion_state(animation.facing_state, facing_direction, moving);
                bool force_restart = false;
                if (body.role == ScenePhysicsRole::player) {
                    const LocomotionState facing = idle_locomotion(state);
                    const bool seated_facing = facing == LocomotionState::idle_south ||
                                               facing == LocomotionState::idle_north;
                    if (moving || player_dodging || player_shoot_ticks_remaining > 0 ||
                        !seated_facing) {
                        player_stationary_ticks = 0;
                    } else if (player_stationary_ticks <
                               std::numeric_limits<std::uint32_t>::max()) {
                        ++player_stationary_ticks;
                    }

                    if (player_dodging) {
                        const LocomotionState dodge_state =
                            dodging_locomotion(locomotion_facing(facing_direction));
                        const std::string& dodge_clip =
                            animation.state_clips[static_cast<std::size_t>(dodge_state)];
                        if (!dodge_clip.empty()) {
                            state = dodge_state;
                        }
                    } else if (player_shoot_ticks_remaining > 0) {
                        const LocomotionState shoot_state = shooting_locomotion(facing);
                        const std::string& shoot_clip =
                            animation.state_clips[static_cast<std::size_t>(shoot_state)];
                        if (!shoot_clip.empty()) {
                            state = shoot_state;
                            force_restart = player_shot_restarted;
                        }
                    } else if (player_stationary_ticks >= player_seated_idle_delay_ticks) {
                        const LocomotionState seated_state = seated_locomotion(facing);
                        const std::string& seated_clip =
                            animation.state_clips[static_cast<std::size_t>(seated_state)];
                        if (!seated_clip.empty()) {
                            state = seated_state;
                        }
                    }
                }
                animation.facing_state = idle_locomotion(state);
                if (state != animation.current_state || force_restart) {
                    const std::string& clip_id =
                        animation.state_clips[static_cast<std::size_t>(state)];
                    const bool action_transition = is_dodging_locomotion(state) ||
                                                   is_seated_locomotion(state) ||
                                                   is_shooting_locomotion(state) ||
                                                   is_dodging_locomotion(animation.current_state) ||
                                                   is_seated_locomotion(animation.current_state) ||
                                                   is_shooting_locomotion(animation.current_state);
                    const AnimationTransitionMode transition =
                        action_transition ? AnimationTransitionMode::restart
                                          : AnimationTransitionMode::preserve_cycle_phase;
                    static_cast<void>(force_restart ? animation.player.play(clip_id, true)
                                                    : animation.player.play(clip_id, transition));
                    animation.current_state = state;
                }
            }
            for (AnimationFrameEvent& event : animation.player.advance()) {
                result.events.emplace_back(SceneAnimationEvent{
                    .entity_uuid = animation.entity_uuid,
                    .entity_id = animation.authored_entity_id,
                    .clip_id = std::move(event.clip_id),
                    .name = std::move(event.name),
                    .frame_index = event.frame_index,
                });
            }
            if (!animation.reaction_state || !animation.player.sample().finished ||
                !animation.body_index) {
                continue;
            }

            BodyBinding& body = body_bindings[*animation.body_index];
            if (*animation.reaction_state == LocomotionState::hurt_south) {
                animation.reaction_state.reset();
                static_cast<void>(animation.player.play(
                    animation.state_clips[static_cast<std::size_t>(animation.current_state)],
                    AnimationTransitionMode::restart));
                continue;
            }
            if (*animation.reaction_state == LocomotionState::death_south) {
                const std::string& explosion =
                    animation.state_clips[static_cast<std::size_t>(LocomotionState::explode_south)];
                if (!explosion.empty()) {
                    static_cast<void>(animation.player.play(explosion, true));
                    animation.reaction_state = LocomotionState::explode_south;
                    continue;
                }
            }

            animation.reaction_state.reset();
            animation.terminal_sequence = false;
            body.hold_presentation_until_terminal = false;
            body.presentation_active = false;
            ++completed_actor_terminal_animations;
        }
        if (player_shoot_ticks_remaining > 0) {
            --player_shoot_ticks_remaining;
        }
        player_shot_restarted = false;
    }

    void synchronize_binding(const std::size_t index) {
        BodyBinding& body = body_bindings[index];
        for (const BoundEntity& bound : body.entities) {
            static_cast<void>(
                world.set_position(bound.entity, {
                                                     body.current_position.x + bound.offset.x,
                                                     body.current_position.y + bound.offset.y,
                                                     body.current_position.z + bound.offset.z,
                                                 }));
        }
    }

    // Physics reports one snapshot per body every tick and each one has to
    // find its binding. Scanning the binding list for each is quadratic in the
    // body count, which dominates the tick once a scene holds thousands of
    // actors, so the mapping is kept alongside the bindings instead.
    [[nodiscard]] std::optional<std::size_t>
    body_index_for_physics(const PhysicsBodyId physics_body) const noexcept {
        const auto found = body_index_by_physics_body.find(physics_body_key(physics_body));
        return found == body_index_by_physics_body.end()
                   ? std::nullopt
                   : std::optional<std::size_t>{found->second};
    }

    // Counting sort into flat arrays, the same shape the crowd steering uses:
    // one pass to count, one to place, no per-cell allocation.
    void rebuild_actor_index() {
        ActorIndex& index = actor_index;
        index.dirty = false;
        index.bounds = definition.ground().walkable_bounds;
        index.cell_size = 64.0F;
        index.columns =
            std::max(1, static_cast<std::int32_t>(std::ceil(index.bounds.width / index.cell_size)));
        index.rows =
            std::max(1, static_cast<std::int32_t>(std::ceil(index.bounds.depth / index.cell_size)));
        const std::size_t cell_count =
            static_cast<std::size_t>(index.columns) * static_cast<std::size_t>(index.rows);
        index.cell_start.assign(cell_count + 1U, 0U);
        std::vector<std::uint32_t> cell_of;
        cell_of.reserve(body_bindings.size());
        std::vector<std::uint32_t> members;
        members.reserve(body_bindings.size());
        for (std::size_t body_index = 0; body_index < body_bindings.size(); ++body_index) {
            const BodyBinding& body = body_bindings[body_index];
            if (body.physics_backed || !body.active) {
                continue;
            }
            const std::optional<std::size_t> cell =
                actor_index_cell(body.current_position.x, body.current_position.z);
            if (!cell) {
                continue;
            }
            cell_of.push_back(static_cast<std::uint32_t>(*cell));
            members.push_back(static_cast<std::uint32_t>(body_index));
            ++index.cell_start[*cell + 1U];
        }
        for (std::size_t cell = 0; cell < cell_count; ++cell) {
            index.cell_start[cell + 1U] += index.cell_start[cell];
        }
        std::vector<std::uint32_t> cursor(index.cell_start.begin(), index.cell_start.end() - 1);
        index.items.assign(members.size(), 0U);
        for (std::size_t entry = 0; entry < members.size(); ++entry) {
            index.items[cursor[cell_of[entry]]++] = members[entry];
        }
    }

    [[nodiscard]] std::optional<std::size_t> actor_index_cell(const float x,
                                                              const float z) const noexcept {
        const auto column = static_cast<std::int32_t>(
            std::floor((x - actor_index.bounds.x) / actor_index.cell_size));
        const auto row = static_cast<std::int32_t>(
            std::floor((z - actor_index.bounds.z) / actor_index.cell_size));
        if (column < 0 || row < 0 || column >= actor_index.columns || row >= actor_index.rows) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(row) * static_cast<std::size_t>(actor_index.columns) +
               static_cast<std::size_t>(column);
    }

    [[nodiscard]] BodyBinding* find_body(const PhysicsBodyId physics_body) noexcept {
        const std::optional<std::size_t> index = body_index_for_physics(physics_body);
        return index ? &body_bindings[*index] : nullptr;
    }

    [[nodiscard]] const BodyBinding* find_body(const PhysicsBodyId physics_body) const noexcept {
        const std::optional<std::size_t> index = body_index_for_physics(physics_body);
        return index ? &body_bindings[*index] : nullptr;
    }

    void register_body_index(const std::size_t index) {
        body_index_by_physics_body.emplace(physics_body_key(body_bindings[index].physics_body),
                                           index);
    }

    // A destroyed body's key must not stay in the lookup: the backend is free
    // to hand the same identifier to the next body it creates, and a stale
    // entry would then resolve contacts and casts to the wrong actor.
    void forget_body_index(const std::size_t index) {
        body_index_by_physics_body.erase(physics_body_key(body_bindings[index].physics_body));
    }

    [[nodiscard]] PhysicsBodyId body_for(const EntityUuid entity_uuid) const noexcept {
        const std::optional<EntityId> entity = world.find(entity_uuid);
        if (!entity) {
            return {};
        }
        const auto binding = entity_bindings.find(entity->value);
        return binding == entity_bindings.end()
                   ? PhysicsBodyId{}
                   : body_bindings[binding->second.body_index].physics_body;
    }

    [[nodiscard]] std::optional<std::size_t>
    body_index_for(const EntityUuid entity_uuid) const noexcept {
        const std::optional<EntityId> entity = world.find(entity_uuid);
        if (!entity) {
            return std::nullopt;
        }
        const auto binding = entity_bindings.find(entity->value);
        return binding == entity_bindings.end()
                   ? std::nullopt
                   : std::optional<std::size_t>{binding->second.body_index};
    }

    [[nodiscard]] EntityUuid
    gameplay_entity_for_index(const std::size_t body_index) const noexcept {
        if (body_index >= body_bindings.size()) {
            return {};
        }
        for (const AnimatedEntity& animation : animated_entities) {
            if (animation.body_index && *animation.body_index == body_index) {
                return animation.entity_uuid;
            }
        }
        for (const BoundEntity& bound : body_bindings[body_index].entities) {
            if (const std::optional<EntityUuid> uuid = world.uuid(bound.entity)) {
                return *uuid;
            }
        }
        return {};
    }

    [[nodiscard]] EntityUuid gameplay_entity_for(const PhysicsBodyId physics_body) const noexcept {
        const BodyBinding* body = find_body(physics_body);
        return body == nullptr ? EntityUuid{}
                               : gameplay_entity_for_index(
                                     static_cast<std::size_t>(body - body_bindings.data()));
    }

    SceneDefinition definition;
    TextureAssets* textures{nullptr};
    GroundMap ground_map;
    PhysicsWorld physics;
    World world;
    std::vector<TextureHandle> owned_textures;
    std::unordered_map<std::string, TextureHandle> texture_handles;
    std::vector<BodyBinding> body_bindings;
    // Body-less actors, bucketed by position so a projectile can find them
    // without the broadphase they no longer live in. Rebuilt lazily on the
    // first cast of a tick, so a scene that fires nothing pays nothing.
    struct ActorIndex {
        float cell_size{0.0F};
        std::int32_t columns{0};
        std::int32_t rows{0};
        RectXZ bounds{};
        std::vector<std::uint32_t> cell_start;
        std::vector<std::uint32_t> items;
        bool dirty{true};
    } actor_index;

    struct ResolvedMove {
        GroundMoveResult movement{};
        EntityUuid actor{};
        bool requested{false};
    };

    // Reused across ticks so a steady tick stops reallocating its scratch.
    std::vector<const RuntimeSceneActorMotion*> actor_motion_slots;
    std::vector<std::size_t> movement_candidates;
    std::vector<ResolvedMove> resolved_moves;
    // Physics body identity to index in body_bindings, maintained as bindings
    // are appended so per-tick snapshot sync stays linear in the body count.
    std::unordered_map<std::uint64_t, std::size_t> body_index_by_physics_body;
    std::unordered_map<std::string, std::size_t> body_indices;
    std::unordered_map<std::uint64_t, EntityBindingLookup> entity_bindings;
    std::unordered_map<std::string, EntityId> entity_ids;
    std::unordered_map<std::string, TextureHandle> animation_clip_textures;
    std::unordered_map<std::string, const SceneAnimationClipDefinition*> animation_clip_definitions;
    std::vector<AnimatedEntity> animated_entities;
    std::unordered_map<std::uint64_t, std::size_t> animation_indices;
    std::size_t player_index{0};
    std::size_t primary_prop_index{0};
    std::size_t physics_body_count{0};
    EntityUuid player_uuid{};
    std::optional<std::uint32_t> active_trigger;
    Vec2 player_presentation_direction{};
    bool player_dodging{false};
    std::uint64_t player_shot_sequence{0};
    std::uint32_t player_shoot_ticks_remaining{0};
    std::uint32_t player_stationary_ticks{0};
    bool player_shot_restarted{false};
    std::uint64_t next_runtime_uuid{1};
    std::uint64_t runtime_spawn_sequence{0};
    bool simulation_started{false};
    std::uint64_t completed_actor_terminal_animations{0};
};

RuntimeScene::RuntimeScene(SceneDefinition definition, TextureAssets& textures)
    : impl_{std::make_unique<Impl>(std::move(definition), textures)} {}

RuntimeScene::~RuntimeScene() = default;
RuntimeScene::RuntimeScene(RuntimeScene&&) noexcept = default;
RuntimeScene& RuntimeScene::operator=(RuntimeScene&&) noexcept = default;

std::vector<EntityUuid>
RuntimeScene::spawn_actor_copies(const ScenePhysicsRole role,
                                 const std::vector<Vec2>& ground_positions) {
    return impl_->spawn_actor_copies(role, ground_positions);
}

void RuntimeScene::reset() {
    for (std::size_t index = 0; index < impl_->body_bindings.size(); ++index) {
        Impl::BodyBinding& body = impl_->body_bindings[index];
        body.previous_position = body.start_position;
        body.current_position = body.start_position;
        if (!body.physics_backed) {
            body.active = true;
        } else if (!body.active) {
            body.physics_body = impl_->physics.create_box(body.definition);
            impl_->register_body_index(index);
            body.active = true;
            ++impl_->physics_body_count;
        } else {
            static_cast<void>(impl_->physics.set_transform(
                body.physics_body, {body.start_position.x, body.start_position.z},
                body.definition.rotation_radians));
        }
        body.presentation_active = true;
        body.hold_presentation_until_terminal = false;
        if (body.physics_backed && body.definition.motion != PhysicsMotionType::static_body) {
            static_cast<void>(impl_->physics.set_linear_velocity(body.physics_body, {}));
        }
        impl_->synchronize_binding(index);
    }
    impl_->actor_index.dirty = true;
    for (Impl::AnimatedEntity& animation : impl_->animated_entities) {
        animation.player.reset();
        static_cast<void>(animation.player.advance(animation.initial_tick_offset));
        if (animation.body_index) {
            animation.facing_state = idle_locomotion(animation.initial_state);
            animation.current_state = animation.initial_state;
            animation.reaction_state.reset();
            animation.terminal_sequence = false;
        }
    }
    impl_->retired_entities.clear();
    impl_->active_trigger.reset();
    impl_->player_dodging = false;
    impl_->player_shot_sequence = 0;
    impl_->player_shoot_ticks_remaining = 0;
    impl_->player_stationary_ticks = 0;
    impl_->player_shot_restarted = false;
    impl_->completed_actor_terminal_animations = 0;
}

RuntimeSceneTickResult RuntimeScene::tick(const RuntimeScenePlayerMotion& player_motion,
                                          const float fixed_step_seconds) {
    return tick(player_motion, {}, fixed_step_seconds);
}

RuntimeSceneTickResult RuntimeScene::tick(const RuntimeScenePlayerMotion& player_motion,
                                          const std::vector<RuntimeSceneActorMotion>& actor_motions,
                                          const float fixed_step_seconds, JobSystem* const jobs) {
    if (!finite(player_motion.world_direction) || !finite(player_motion.presentation_direction) ||
        !std::isfinite(player_motion.speed_multiplier) || player_motion.speed_multiplier <= 0.0F ||
        !std::isfinite(fixed_step_seconds) || fixed_step_seconds <= 0.0F) {
        throw std::invalid_argument{
            "Runtime scene ticks require finite input and a positive step."};
    }
    impl_->simulation_started = true;

    // Body indices are dense, so a flat slot per binding replaces a hash
    // insert and later hash lookup for every actor in the scene. The buffer is
    // reused, and only the slots touched this tick are cleared afterwards.
    std::vector<const RuntimeSceneActorMotion*>& actor_motion_by_body = impl_->actor_motion_slots;
    actor_motion_by_body.assign(impl_->body_bindings.size(), nullptr);
    for (const RuntimeSceneActorMotion& actor_motion : actor_motions) {
        const float direction_length_squared =
            actor_motion.world_direction.x * actor_motion.world_direction.x +
            actor_motion.world_direction.y * actor_motion.world_direction.y;
        const std::optional<std::size_t> body_index = impl_->body_index_for(actor_motion.actor);
        if (!actor_motion.actor || !finite(actor_motion.world_direction) ||
            !std::isfinite(actor_motion.speed) || actor_motion.speed < 0.0F ||
            direction_length_squared > 1.0002F || !body_index ||
            *body_index == impl_->player_index || !impl_->body_bindings[*body_index].active ||
            impl_->body_bindings[*body_index].definition.motion !=
                PhysicsMotionType::kinematic_body ||
            actor_motion_by_body[*body_index] != nullptr) {
            throw std::invalid_argument{
                "Runtime actor motion requires one active non-player kinematic actor, a finite "
                "speed, and a normalized direction."};
        }
        actor_motion_by_body[*body_index] = &actor_motion;
    }
    impl_->player_presentation_direction = player_motion.presentation_direction;
    impl_->player_dodging = player_motion.dodging;
    impl_->player_shot_restarted = false;
    if (player_motion.shot_sequence > impl_->player_shot_sequence) {
        impl_->player_shoot_ticks_remaining = player_shoot_presentation_ticks;
        impl_->player_shot_restarted = true;
    }
    impl_->player_shot_sequence = player_motion.shot_sequence;

    for (std::size_t body_index = 0; body_index < impl_->body_bindings.size(); ++body_index) {
        Impl::BodyBinding& body = impl_->body_bindings[body_index];
        body.previous_position = body.current_position;
        const RuntimeSceneActorMotion* const requested = actor_motion_by_body[body_index];
        body.requested_direction =
            requested != nullptr && requested->speed > 0.0F ? requested->world_direction : Vec2{};
    }
    Impl::BodyBinding& player = impl_->body_bindings[impl_->player_index];
    const float movement_speed =
        impl_->definition.simulation().player_speed * player_motion.speed_multiplier;
    const Vec2 desired_position{
        player.current_position.x +
            player_motion.world_direction.x * movement_speed * fixed_step_seconds,
        player.current_position.z +
            player_motion.world_direction.y * movement_speed * fixed_step_seconds,
    };
    const GroundMoveResult movement = impl_->ground_map.move(
        player.current_position, desired_position, player.definition.half_extents);
    if (!impl_->physics.set_kinematic_target(
            player.physics_body, {movement.position.x, movement.position.z}, fixed_step_seconds)) {
        throw std::logic_error{"The authored player body rejected its kinematic target."};
    }

    struct PlannedActorMotion {
        std::size_t body_index{0};
        EntityUuid actor{};
        Vec3 start_position{};
        float planned_distance{0.0F};
        bool ground_blocked{false};
    };
    // Resolving ground movement is the bulk of the tick at crowd scale, and it
    // is pure: it reads immutable ground data plus each actor's own position
    // and produces a result for that actor alone. Gathering the actors first
    // lets that phase run across threads, while applying the results, which
    // writes shared World transforms, stays ordered.
    std::vector<std::size_t>& candidates = impl_->movement_candidates;
    candidates.clear();
    for (std::size_t body_index = 0; body_index < impl_->body_bindings.size(); ++body_index) {
        const Impl::BodyBinding& body = impl_->body_bindings[body_index];
        if (!body.active || body_index == impl_->player_index ||
            body.definition.motion != PhysicsMotionType::kinematic_body) {
            continue;
        }
        // A body-less actor with no motion this tick is not moving, so there
        // is nothing to slide, resolve or write. A body-backed one still needs
        // its kinematic target set, or the solver keeps its previous velocity.
        if (!body.physics_backed && actor_motion_by_body[body_index] == nullptr) {
            continue;
        }
        candidates.push_back(body_index);
    }

    std::vector<Impl::ResolvedMove>& resolved = impl_->resolved_moves;
    resolved.assign(candidates.size(), Impl::ResolvedMove{});
    const auto resolve_range = [&](const std::size_t first, const std::size_t last) {
        for (std::size_t entry = first; entry < last; ++entry) {
            const std::size_t body_index = candidates[entry];
            const Impl::BodyBinding& body = impl_->body_bindings[body_index];
            const RuntimeSceneActorMotion* const requested = actor_motion_by_body[body_index];
            Vec2 direction{};
            float speed = 0.0F;
            Impl::ResolvedMove& output = resolved[entry];
            if (requested != nullptr) {
                direction = requested->world_direction;
                speed = requested->speed;
                output.actor = requested->actor;
                output.requested = true;
            }
            const Vec2 desired_actor_position{
                body.current_position.x + direction.x * speed * fixed_step_seconds,
                body.current_position.z + direction.y * speed * fixed_step_seconds,
            };
            output.movement = impl_->ground_map.move(body.current_position, desired_actor_position,
                                                     body.definition.half_extents);
        }
    };
    constexpr std::size_t parallel_actor_threshold = 512;
    constexpr std::size_t minimum_move_batch = 256;
    if (jobs != nullptr && candidates.size() >= parallel_actor_threshold) {
        jobs->parallel_for(candidates.size(), minimum_move_batch, resolve_range);
    } else {
        resolve_range(0, candidates.size());
    }

    std::vector<PlannedActorMotion> planned_actor_motions;
    planned_actor_motions.reserve(actor_motions.size());
    for (std::size_t entry = 0; entry < candidates.size(); ++entry) {
        const std::size_t body_index = candidates[entry];
        Impl::BodyBinding& body = impl_->body_bindings[body_index];
        const Impl::ResolvedMove& move = resolved[entry];
        // Captured before the move is committed, so the planned distance is
        // measured against where the actor actually started this tick.
        const Vec3 start_position = body.current_position;
        if (body.physics_backed) {
            if (!impl_->physics.set_kinematic_target(
                    body.physics_body, {move.movement.position.x, move.movement.position.z},
                    fixed_step_seconds)) {
                throw std::logic_error{"An authored actor body rejected its kinematic target."};
            }
        } else {
            // Ground movement is already the authoritative result for these
            // actors, so it is committed here rather than round-tripped
            // through a solver that would return it unchanged. Previous
            // positions were advanced for every binding at the top of the tick.
            body.current_position = move.movement.position;
            impl_->synchronize_binding(body_index);
        }
        if (move.requested) {
            const float moved_x = move.movement.position.x - start_position.x;
            const float moved_z = move.movement.position.z - start_position.z;
            planned_actor_motions.push_back({
                .body_index = body_index,
                .actor = move.actor,
                .start_position = start_position,
                .planned_distance = std::sqrt(moved_x * moved_x + moved_z * moved_z),
                .ground_blocked = move.movement.blocked_x || move.movement.blocked_z,
            });
        }
    }

    RuntimeSceneTickResult result{
        .player_blocked = movement.blocked_x || movement.blocked_z,
        .player_elevated = movement.position.y > 0.0F,
        .player_distance_moved = std::sqrt((movement.position.x - player.current_position.x) *
                                               (movement.position.x - player.current_position.x) +
                                           (movement.position.z - player.current_position.z) *
                                               (movement.position.z - player.current_position.z)),
    };
    const PhysicsStepResult physics_step = impl_->physics.step(fixed_step_seconds);
    for (const PhysicsEvent& event : physics_step.events) {
        if (event.kind == PhysicsEventKind::contact_begin ||
            event.kind == PhysicsEventKind::contact_end) {
            result.events.emplace_back(SceneContactEvent{
                .began = event.kind == PhysicsEventKind::contact_begin,
                .tag_a = event.tag_a,
                .tag_b = event.tag_b,
            });
        } else {
            result.events.emplace_back(SceneTriggerEvent{
                .entered = event.kind == PhysicsEventKind::trigger_begin,
                .tag = event.tag_a,
                .player_visitor = event.body_b == player.physics_body,
            });
        }
        if (event.kind == PhysicsEventKind::trigger_begin && event.body_b == player.physics_body) {
            impl_->active_trigger = event.tag_a;
        } else if (event.kind == PhysicsEventKind::trigger_end &&
                   event.body_b == player.physics_body && impl_->active_trigger == event.tag_a) {
            impl_->active_trigger.reset();
        }
    }

    for (const PhysicsBodySnapshot& snapshot : physics_step.bodies) {
        Impl::BodyBinding* body = impl_->find_body(snapshot.body);
        if (body == nullptr) {
            continue;
        }
        const float elevation = body->role == ScenePhysicsRole::player
                                    ? movement.position.y
                                    : impl_->ground_map.elevation_at(snapshot.center);
        body->current_position = {snapshot.center.x, elevation, snapshot.center.y};
        impl_->synchronize_binding(static_cast<std::size_t>(body - impl_->body_bindings.data()));
    }

    // Crowd positions have moved, so any index built for a segment cast this
    // tick no longer describes where they are.
    impl_->actor_index.dirty = true;

    result.actor_motions.reserve(planned_actor_motions.size());
    for (const PlannedActorMotion& planned : planned_actor_motions) {
        const Impl::BodyBinding& body = impl_->body_bindings[planned.body_index];
        const float moved_x = body.current_position.x - planned.start_position.x;
        const float moved_z = body.current_position.z - planned.start_position.z;
        const float actual_distance = std::sqrt(moved_x * moved_x + moved_z * moved_z);
        constexpr float blocked_distance_epsilon = 0.01F;
        result.actor_motions.push_back({
            .actor = planned.actor,
            .blocked = planned.ground_blocked ||
                       actual_distance + blocked_distance_epsilon < planned.planned_distance,
            .distance_moved = actual_distance,
        });
    }

    const Impl::BodyBinding& prop = impl_->body_bindings[impl_->primary_prop_index];
    result.primary_prop_moved = std::abs(prop.current_position.x - prop.start_position.x) > 0.5F ||
                                std::abs(prop.current_position.z - prop.start_position.z) > 0.5F;
    impl_->advance_animations(result);
    result.active_trigger = impl_->active_trigger;
    return result;
}

const std::string& RuntimeScene::id() const noexcept { return impl_->definition.id(); }
const Camera25DState& RuntimeScene::initial_camera() const noexcept {
    return impl_->definition.camera();
}
const GroundMapDefinition& RuntimeScene::ground_definition() const noexcept {
    return impl_->definition.ground();
}
EntityUuid RuntimeScene::player_uuid() const noexcept { return impl_->player_uuid; }
Vec3 RuntimeScene::player_position() const noexcept {
    return impl_->body_bindings[impl_->player_index].current_position;
}
Vec3 RuntimeScene::primary_prop_position() const noexcept {
    return impl_->body_bindings[impl_->primary_prop_index].current_position;
}

std::vector<EntityUuid> RuntimeScene::actor_uuids(const ScenePhysicsRole role) const {
    std::vector<EntityUuid> result;
    for (std::size_t index = 0; index < impl_->body_bindings.size(); ++index) {
        if (impl_->body_bindings[index].role != role) {
            continue;
        }
        const EntityUuid actor = impl_->gameplay_entity_for_index(index);
        if (actor) {
            result.push_back(actor);
        }
    }
    return result;
}

std::optional<Vec3> RuntimeScene::actor_position(const EntityUuid actor) const noexcept {
    const std::optional<EntityId> entity = impl_->world.find(actor);
    if (!entity) {
        return std::nullopt;
    }
    const auto binding = impl_->entity_bindings.find(entity->value);
    if (binding == impl_->entity_bindings.end()) {
        return std::nullopt;
    }
    return impl_->body_bindings[binding->second.body_index].current_position;
}

bool RuntimeScene::is_crowd_actor(const EntityUuid actor) const noexcept {
    const std::optional<EntityId> entity = impl_->world.find(actor);
    if (!entity) {
        return false;
    }
    const auto binding = impl_->entity_bindings.find(entity->value);
    return binding != impl_->entity_bindings.end() &&
           !impl_->body_bindings[binding->second.body_index].physics_backed;
}

bool RuntimeScene::retire_actor(const EntityUuid actor) noexcept {
    const std::optional<EntityId> entity = impl_->world.find(actor);
    if (!entity) {
        return false;
    }
    const auto binding = impl_->entity_bindings.find(entity->value);
    if (binding == impl_->entity_bindings.end()) {
        return false;
    }
    Impl::BodyBinding& body = impl_->body_bindings[binding->second.body_index];
    if (!body.active || body.role == ScenePhysicsRole::player) {
        return false;
    }
    // A crowd actor has no rigid body to destroy. Retiring must still succeed
    // for it, or killing one would leave it standing in the scene forever.
    if (body.physics_backed) {
        if (!impl_->physics.destroy_body(body.physics_body)) {
            return false;
        }
        impl_->forget_body_index(binding->second.body_index);
        body.physics_body = {};
        --impl_->physics_body_count;
    }
    body.active = false;
    body.presentation_active = body.hold_presentation_until_terminal;
    impl_->actor_index.dirty = true;
    return true;
}

bool RuntimeScene::play_actor_hurt(const EntityUuid actor) {
    return impl_->play_actor_reaction(actor, LocomotionState::hurt_south, false);
}

bool RuntimeScene::begin_actor_death(const EntityUuid actor) {
    return impl_->play_actor_reaction(actor, LocomotionState::death_south, true);
}

std::uint64_t RuntimeScene::completed_actor_terminal_animation_count() const noexcept {
    return impl_->completed_actor_terminal_animations;
}
bool RuntimeScene::retire_entity(const EntityUuid entity) noexcept {
    if (!entity || entity == player_uuid()) {
        return false;
    }
    if (!impl_->world.find(entity)) {
        return false;
    }
    // Hidden, not destroyed. Destroying it would free the World identity for
    // reuse while bindings and animation indices still name it, and reset()
    // rebuilds bodies rather than entities, so a used item could never come
    // back. Hiding keeps retirement reversible by exactly the same rule that
    // makes a retired actor come back.
    if (!impl_->retired_entities.insert(entity.value).second) {
        return false;
    }
    // A parent owns its children's lifetime, so a used pickup takes its shadow
    // with it instead of leaving it lying on the ground. The walk is iterative
    // and bounded by the authored set, and authoring already rejects cycles.
    std::vector<EntityUuid> pending{entity};
    while (!pending.empty()) {
        const EntityUuid owner = pending.back();
        pending.pop_back();
        for (const SceneEntityDefinition& authored : impl_->definition.entities()) {
            if (authored.parent != owner || !authored.uuid) {
                continue;
            }
            // The player is never retired, so a child of the player is not
            // either; anything else joins the walk once.
            if (authored.uuid == player_uuid() ||
                !impl_->retired_entities.insert(authored.uuid.value).second) {
                continue;
            }
            pending.push_back(authored.uuid);
        }
    }
    return true;
}

std::optional<RuntimeSceneSegmentHit>
RuntimeScene::cast_segment(const Vec2& start, const Vec2& end,
                           const EntityUuid ignored_entity) const {
    // Authored geometry, the player and props still answer through the
    // broadphase; only body-less crowd actors need the separate query. The
    // nearer of the two wins, so a wall still stops a shot aimed past it.
    const std::optional<PhysicsSegmentHit> physics_hit = impl_->physics.cast_segment({
        .start = start,
        .end = end,
        .ignored_body = impl_->body_for(ignored_entity),
    });
    std::optional<RuntimeSceneSegmentHit> nearest;
    if (physics_hit) {
        nearest = RuntimeSceneSegmentHit{
            .entity = impl_->gameplay_entity_for(physics_hit->body),
            .point = physics_hit->point,
            .normal = physics_hit->normal,
            .fraction = physics_hit->fraction,
            .tag = physics_hit->tag,
        };
    }

    if (impl_->actor_index.dirty) {
        impl_->rebuild_actor_index();
    }
    if (impl_->actor_index.items.empty()) {
        return nearest;
    }
    // Resolving a binding back to its actor walks the animation table, so the
    // shooter is matched by index here rather than per candidate below.
    std::size_t ignored_body_index = impl_->body_bindings.size();
    if (const std::optional<EntityId> entity = impl_->world.find(ignored_entity)) {
        const auto binding = impl_->entity_bindings.find(entity->value);
        if (binding != impl_->entity_bindings.end()) {
            ignored_body_index = binding->second.body_index;
        }
    }

    // Walk only the index cells the segment's bounding box touches. A tick's
    // travel is short next to a cell, so this is a handful of cells however
    // many actors the scene holds.
    const float minimum_x = std::min(start.x, end.x);
    const float maximum_x = std::max(start.x, end.x);
    const float minimum_z = std::min(start.y, end.y);
    const float maximum_z = std::max(start.y, end.y);
    const Impl::ActorIndex& index = impl_->actor_index;
    const auto clamp_column = [&index](const float value) {
        const auto raw =
            static_cast<std::int32_t>(std::floor((value - index.bounds.x) / index.cell_size));
        return std::clamp(raw, 0, index.columns - 1);
    };
    const auto clamp_row = [&index](const float value) {
        const auto raw =
            static_cast<std::int32_t>(std::floor((value - index.bounds.z) / index.cell_size));
        return std::clamp(raw, 0, index.rows - 1);
    };
    const std::int32_t first_column = clamp_column(minimum_x);
    const std::int32_t last_column = clamp_column(maximum_x);
    const std::int32_t first_row = clamp_row(minimum_z);
    const std::int32_t last_row = clamp_row(maximum_z);

    for (std::int32_t row = first_row; row <= last_row; ++row) {
        for (std::int32_t column = first_column; column <= last_column; ++column) {
            const auto cell =
                static_cast<std::size_t>(row) * static_cast<std::size_t>(index.columns) +
                static_cast<std::size_t>(column);
            for (std::uint32_t slot = index.cell_start[cell]; slot < index.cell_start[cell + 1U];
                 ++slot) {
                const std::size_t body_index = index.items[slot];
                if (body_index == ignored_body_index) {
                    continue;
                }
                const Impl::BodyBinding& body = impl_->body_bindings[body_index];
                const std::optional<SegmentBoxHit> box_hit =
                    segment_box_hit(start, end, {body.current_position.x, body.current_position.z},
                                    body.definition.half_extents);
                if (!box_hit || (nearest && box_hit->fraction > nearest->fraction)) {
                    continue;
                }
                const EntityUuid actor = impl_->gameplay_entity_for_index(body_index);
                if (!actor || actor == ignored_entity) {
                    continue;
                }
                // Equal fractions resolve by actor identity so a shot into a
                // packed crowd always resolves to the same target.
                if (nearest && box_hit->fraction == nearest->fraction &&
                    !(actor < nearest->entity)) {
                    continue;
                }
                nearest = RuntimeSceneSegmentHit{
                    .entity = actor,
                    .point = box_hit->point,
                    .normal = box_hit->normal,
                    .fraction = box_hit->fraction,
                    .tag = body.definition.tag,
                };
            }
        }
    }
    return nearest;
}
WorldSnapshot RuntimeScene::world_snapshot() const { return impl_->world.snapshot(); }

bool RuntimeScene::is_entity_presented(const EntityUuid entity) const noexcept {
    const std::optional<EntityId> found = impl_->world.find(entity);
    return found && impl_->is_presented(*found);
}

std::vector<RenderItem2D>
RuntimeScene::collect_render_items(const float interpolation_alpha,
                                   const std::optional<RectXZ> region) const {
    if (!std::isfinite(interpolation_alpha)) {
        throw std::invalid_argument{"Render interpolation alpha must be finite."};
    }
    const float alpha = std::clamp(interpolation_alpha, 0.0F, 1.0F);
    std::vector<RenderItem2D> items = impl_->world.collect_render_items(region);
    std::erase_if(items,
                  [this](const RenderItem2D& item) { return !impl_->is_presented(item.entity); });
    for (RenderItem2D& item : items) {
        const auto found = impl_->entity_bindings.find(item.entity.value);
        if (found != impl_->entity_bindings.end()) {
            const Impl::BodyBinding& body = impl_->body_bindings[found->second.body_index];
            const Vec3 interpolated{
                body.previous_position.x +
                    (body.current_position.x - body.previous_position.x) * alpha,
                body.previous_position.y +
                    (body.current_position.y - body.previous_position.y) * alpha,
                body.previous_position.z +
                    (body.current_position.z - body.previous_position.z) * alpha,
            };
            item.transform.position = {
                interpolated.x + found->second.offset.x,
                interpolated.y + found->second.offset.y,
                interpolated.z + found->second.offset.z,
            };
        }
        const auto animation = impl_->animation_indices.find(item.entity.value);
        if (animation != impl_->animation_indices.end()) {
            const AnimationSample sample =
                impl_->animated_entities[animation->second].player.sample();
            item.sprite.texture = impl_->animation_clip_textures.at(std::string{sample.clip_id});
            item.sprite.source = sample.source;
            item.sprite.flip_x = sample.flip_x;
        }
    }
    return items;
}

std::vector<PhysicsFootprint> RuntimeScene::debug_footprints() const {
    return impl_->physics.debug_footprints();
}

std::size_t RuntimeScene::entity_count() const noexcept { return impl_->world.entity_count(); }
std::size_t RuntimeScene::physics_body_count() const noexcept { return impl_->physics_body_count; }
std::size_t RuntimeScene::animation_binding_count() const noexcept {
    return impl_->animated_entities.size();
}

} // namespace ic2d
