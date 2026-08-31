#include "ic2d/runtime_scene.hpp"

#include "ic2d/locomotion.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ic2d {
namespace {

[[nodiscard]] bool finite(const Vec2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] TextureSampling to_texture_sampling(const SceneTextureSampling sampling) noexcept {
    return sampling == SceneTextureSampling::pixel ? TextureSampling::pixel : TextureSampling::smooth;
}

} // namespace

struct RuntimeScene::Impl {
    struct BoundEntity {
        EntityId entity{};
        Vec3 offset{};
    };

    struct BodyBinding {
        std::string authored_id;
        ScenePhysicsRole role{ScenePhysicsRole::generic};
        PhysicsBodyId physics_body{};
        PhysicsBoxDefinition definition{};
        Vec3 start_position{};
        Vec3 previous_position{};
        Vec3 current_position{};
        std::vector<BoundEntity> entities;
    };

    struct EntityBindingLookup {
        std::size_t body_index{0};
        Vec3 offset{};
    };

    struct AnimatedEntity {
        std::string authored_entity_id;
        EntityUuid entity_uuid{};
        EntityId entity{};
        std::size_t body_index{0};
        LocomotionState initial_state{LocomotionState::idle_south};
        LocomotionState facing_state{LocomotionState::idle_south};
        LocomotionState current_state{LocomotionState::idle_south};
        std::array<std::string, locomotion_state_count> state_clips;
        AnimationPlayer player;
    };

    Impl(SceneDefinition authored_definition, TextureAssets& texture_assets)
        : definition{std::move(authored_definition)},
          textures{&texture_assets},
          ground_map{definition.ground()},
          physics{definition.simulation().physics} {
        try {
            load_textures();
            build_ground_physics();
            build_authored_bodies();
            build_entities();
            world.flush();
            build_animations();
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
                handle = textures->create_checker(
                    texture.id, texture.width, texture.height, texture.cell_size,
                    texture.first_color, texture.second_color, to_texture_sampling(texture.sampling));
            } else if (texture.kind == SceneTextureKind::radial) {
                handle = textures->create_radial_gradient(
                    texture.id, texture.width, texture.height,
                    texture.first_color, texture.second_color,
                    to_texture_sampling(texture.sampling));
            } else {
                handle = textures->acquire(
                    definition.resolve_asset(texture.relative_path),
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

    [[nodiscard]] PhysicsBodyId create_static_footprint(
        const RectXZ& bounds,
        const std::uint32_t tag,
        const std::uint64_t category_bits,
        const std::uint64_t mask_bits,
        const bool sensor
    ) {
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
                static_cast<void>(create_static_footprint(
                    area.bounds, static_tag++, simulation.ground_category_bits,
                    simulation.ground_mask_bits, false));
            } else if (area.kind == GroundAreaKind::trigger) {
                static_cast<void>(create_static_footprint(
                    area.bounds, area.tag, simulation.trigger_category_bits,
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
                .sprite = Sprite2D{
                    .texture = texture,
                    .size = authored.sprite.size,
                    .normalized_origin = authored.sprite.normalized_origin,
                    .tint = authored.sprite.tint,
                    .layer = authored.sprite.layer,
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
            entity_bindings.emplace(entity.value, EntityBindingLookup{body_index, offset});
        }
    }

    void build_animations() {
        std::unordered_map<std::string, const SceneAnimationClipDefinition*> clip_definitions;
        for (const SceneAnimationClipDefinition& clip : definition.animation_clips()) {
            clip_definitions.emplace(clip.clip.id, &clip);
            animation_clip_textures.emplace(
                clip.clip.id, texture_handles.at(clip.texture_id));
        }

        animated_entities.reserve(definition.animation_bindings().size());
        for (const SceneAnimationBindingDefinition& binding : definition.animation_bindings()) {
            const auto authored_entity = std::ranges::find(
                definition.entities(), binding.entity_id, &SceneEntityDefinition::id);
            if (authored_entity == definition.entities().end()) {
                throw std::logic_error{"Validated animation entity disappeared."};
            }
            std::vector<AnimationClip> clips;
            std::unordered_set<std::string> included_clips;
            for (const std::string& clip_id : binding.state_clips) {
                if (included_clips.insert(clip_id).second) {
                    clips.push_back(clip_definitions.at(clip_id)->clip);
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
            animation_indices.emplace(
                animated_entities.back().entity.value, animation_index);
        }
    }

    void advance_animations(RuntimeSceneTickResult& result) {
        for (AnimatedEntity& animation : animated_entities) {
            const BodyBinding& body = body_bindings[animation.body_index];
            const Vec2 delta{
                body.current_position.x - body.previous_position.x,
                body.current_position.z - body.previous_position.z,
            };
            const bool moving = std::abs(delta.x) > 0.01F || std::abs(delta.y) > 0.01F;
            if (moving) {
                animation.facing_state = locomotion_facing(delta);
            }
            const LocomotionState state = moving
                                              ? moving_locomotion(animation.facing_state)
                                              : animation.facing_state;
            if (state != animation.current_state) {
                const std::string& clip_id =
                    animation.state_clips[static_cast<std::size_t>(state)];
                static_cast<void>(animation.player.play(clip_id));
                animation.current_state = state;
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
        }
    }

    void synchronize_binding(const std::size_t index) {
        BodyBinding& body = body_bindings[index];
        for (const BoundEntity& bound : body.entities) {
            auto transform = world.transform(bound.entity);
            if (!transform) {
                continue;
            }
            transform->position = {
                body.current_position.x + bound.offset.x,
                body.current_position.y + bound.offset.y,
                body.current_position.z + bound.offset.z,
            };
            static_cast<void>(world.set_transform(bound.entity, *transform));
        }
    }

    [[nodiscard]] BodyBinding* find_body(const PhysicsBodyId physics_body) noexcept {
        const auto found = std::ranges::find(
            body_bindings, physics_body, &BodyBinding::physics_body);
        return found == body_bindings.end() ? nullptr : &*found;
    }

    SceneDefinition definition;
    TextureAssets* textures{nullptr};
    GroundMap ground_map;
    PhysicsWorld physics;
    World world;
    std::vector<TextureHandle> owned_textures;
    std::unordered_map<std::string, TextureHandle> texture_handles;
    std::vector<BodyBinding> body_bindings;
    std::unordered_map<std::string, std::size_t> body_indices;
    std::unordered_map<std::uint64_t, EntityBindingLookup> entity_bindings;
    std::unordered_map<std::string, EntityId> entity_ids;
    std::unordered_map<std::string, TextureHandle> animation_clip_textures;
    std::vector<AnimatedEntity> animated_entities;
    std::unordered_map<std::uint64_t, std::size_t> animation_indices;
    std::size_t player_index{0};
    std::size_t primary_prop_index{0};
    std::size_t physics_body_count{0};
    std::optional<std::uint32_t> active_trigger;
};

RuntimeScene::RuntimeScene(SceneDefinition definition, TextureAssets& textures)
    : impl_{std::make_unique<Impl>(std::move(definition), textures)} {}

RuntimeScene::~RuntimeScene() = default;
RuntimeScene::RuntimeScene(RuntimeScene&&) noexcept = default;
RuntimeScene& RuntimeScene::operator=(RuntimeScene&&) noexcept = default;

void RuntimeScene::reset() {
    for (std::size_t index = 0; index < impl_->body_bindings.size(); ++index) {
        Impl::BodyBinding& body = impl_->body_bindings[index];
        body.previous_position = body.start_position;
        body.current_position = body.start_position;
        static_cast<void>(impl_->physics.set_transform(
            body.physics_body, {body.start_position.x, body.start_position.z},
            body.definition.rotation_radians));
        if (body.definition.motion != PhysicsMotionType::static_body) {
            static_cast<void>(impl_->physics.set_linear_velocity(body.physics_body, {}));
        }
        impl_->synchronize_binding(index);
    }
    for (Impl::AnimatedEntity& animation : impl_->animated_entities) {
        animation.player.reset();
        animation.facing_state = idle_locomotion(animation.initial_state);
        animation.current_state = animation.initial_state;
    }
    impl_->active_trigger.reset();
}

RuntimeSceneTickResult RuntimeScene::tick(
    const Vec2& player_ground_direction,
    const float fixed_step_seconds
) {
    if (!finite(player_ground_direction) || !std::isfinite(fixed_step_seconds) ||
        fixed_step_seconds <= 0.0F) {
        throw std::invalid_argument{"Runtime scene ticks require finite input and a positive step."};
    }

    for (Impl::BodyBinding& body : impl_->body_bindings) {
        body.previous_position = body.current_position;
    }
    Impl::BodyBinding& player = impl_->body_bindings[impl_->player_index];
    const Vec2 desired_position{
        player.current_position.x + player_ground_direction.x *
                                        impl_->definition.simulation().player_speed * fixed_step_seconds,
        player.current_position.z + player_ground_direction.y *
                                        impl_->definition.simulation().player_speed * fixed_step_seconds,
    };
    const GroundMoveResult movement = impl_->ground_map.move(
        player.current_position, desired_position, player.definition.half_extents);
    if (!impl_->physics.set_kinematic_target(
            player.physics_body, {movement.position.x, movement.position.z}, fixed_step_seconds)) {
        throw std::logic_error{"The authored player body rejected its kinematic target."};
    }

    RuntimeSceneTickResult result{
        .player_blocked = movement.blocked_x || movement.blocked_z,
        .player_elevated = movement.position.y > 0.0F,
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
        if (event.kind == PhysicsEventKind::trigger_begin &&
            event.body_b == player.physics_body) {
            impl_->active_trigger = event.tag_a;
        } else if (event.kind == PhysicsEventKind::trigger_end &&
                   event.body_b == player.physics_body &&
                   impl_->active_trigger == event.tag_a) {
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
        impl_->synchronize_binding(
            static_cast<std::size_t>(body - impl_->body_bindings.data()));
    }

    const Impl::BodyBinding& prop = impl_->body_bindings[impl_->primary_prop_index];
    result.primary_prop_moved =
        std::abs(prop.current_position.x - prop.start_position.x) > 0.5F ||
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
Vec3 RuntimeScene::player_position() const noexcept {
    return impl_->body_bindings[impl_->player_index].current_position;
}
Vec3 RuntimeScene::primary_prop_position() const noexcept {
    return impl_->body_bindings[impl_->primary_prop_index].current_position;
}
WorldSnapshot RuntimeScene::world_snapshot() const { return impl_->world.snapshot(); }

std::vector<RenderItem2D> RuntimeScene::collect_render_items(const float interpolation_alpha) const {
    if (!std::isfinite(interpolation_alpha)) {
        throw std::invalid_argument{"Render interpolation alpha must be finite."};
    }
    const float alpha = std::clamp(interpolation_alpha, 0.0F, 1.0F);
    std::vector<RenderItem2D> items = impl_->world.collect_render_items();
    for (RenderItem2D& item : items) {
        const auto found = impl_->entity_bindings.find(item.entity.value);
        if (found != impl_->entity_bindings.end()) {
            const Impl::BodyBinding& body = impl_->body_bindings[found->second.body_index];
            const Vec3 interpolated{
                body.previous_position.x + (body.current_position.x - body.previous_position.x) * alpha,
                body.previous_position.y + (body.current_position.y - body.previous_position.y) * alpha,
                body.previous_position.z + (body.current_position.z - body.previous_position.z) * alpha,
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
            const Impl::AnimatedEntity& animated =
                impl_->animated_entities[animation->second];
            const std::string& clip_id =
                animated.state_clips[static_cast<std::size_t>(animated.current_state)];
            item.sprite.texture = impl_->animation_clip_textures.at(clip_id);
            item.sprite.source = sample.source;
        }
    }
    return items;
}

std::vector<PhysicsFootprint> RuntimeScene::debug_footprints() const {
    return impl_->physics.debug_footprints();
}

std::size_t RuntimeScene::entity_count() const noexcept { return impl_->world.entity_count(); }
std::size_t RuntimeScene::physics_body_count() const noexcept {
    return impl_->physics_body_count;
}
std::size_t RuntimeScene::animation_binding_count() const noexcept {
    return impl_->animated_entities.size();
}

} // namespace ic2d
