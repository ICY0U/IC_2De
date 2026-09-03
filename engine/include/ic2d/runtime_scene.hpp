#pragma once

#include "ic2d/assets.hpp"
#include "ic2d/events.hpp"
#include "ic2d/physics2d.hpp"
#include "ic2d/scene.hpp"
#include "ic2d/world.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ic2d {

class JobSystem;

struct RuntimeScenePlayerMotion {
    Vec2 world_direction{};
    Vec2 presentation_direction{};
    float speed_multiplier{1.0F};
    bool dodging{false};
    // Monotonic successful-shot count. A changed value restarts the short
    // firing presentation without making animation state authoritative.
    std::uint64_t shot_sequence{0};
};

struct RuntimeSceneActorMotion {
    EntityUuid actor{};
    Vec2 world_direction{};
    float speed{0.0F};
};

struct RuntimeSceneActorMotionResult {
    EntityUuid actor{};
    bool blocked{false};
    float distance_moved{0.0F};
};

struct RuntimeSceneTickResult {
    bool player_blocked{false};
    bool player_elevated{false};
    bool primary_prop_moved{false};
    float player_distance_moved{0.0F};
    std::optional<std::uint32_t> active_trigger;
    // Authored body order, independent of caller command order.
    std::vector<RuntimeSceneActorMotionResult> actor_motions;
    std::vector<EngineEvent> events;
};

struct RuntimeSceneSegmentHit {
    EntityUuid entity{}; // Zero identifies solid world geometry without an entity.
    Vec2 point{};
    Vec2 normal{};
    float fraction{0.0F};
    std::uint32_t tag{0};
};

// Owns a playable scene's World, GroundMap, PhysicsWorld, asset handles, and
// transform bindings. Callers provide camera-rotated world movement plus the
// screen-relative facing direction used for player presentation.
class RuntimeScene final {
public:
    RuntimeScene(SceneDefinition definition, TextureAssets& textures);
    ~RuntimeScene();

    RuntimeScene(const RuntimeScene&) = delete;
    RuntimeScene& operator=(const RuntimeScene&) = delete;
    RuntimeScene(RuntimeScene&&) noexcept;
    RuntimeScene& operator=(RuntimeScene&&) noexcept;

    // Creates runtime-only copies of the first kinematic actor with this role.
    // The complete bound presentation (including locomotion animation) is
    // copied behind this interface. This is initialization-only so gameplay
    // modules can register the returned stable UUIDs before fixed tick one.
    // The authored SceneDefinition and its source file are never modified.
    [[nodiscard]] std::vector<EntityUuid>
    spawn_actor_copies(ScenePhysicsRole role, const std::vector<Vec2>& ground_positions);

    void reset();
    [[nodiscard]] RuntimeSceneTickResult tick(const RuntimeScenePlayerMotion& player,
                                              float fixed_step_seconds);
    // Resolving a crowd's ground movement is the bulk of a tick at scale and
    // divides cleanly, since each actor reads shared immutable ground data and
    // produces only its own result. An optional job system spreads that phase;
    // applying the results stays ordered, so the outcome does not depend on it.
    [[nodiscard]] RuntimeSceneTickResult tick(const RuntimeScenePlayerMotion& player,
                                              const std::vector<RuntimeSceneActorMotion>& actors,
                                              float fixed_step_seconds, JobSystem* jobs = nullptr);

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] const Camera25DState& initial_camera() const noexcept;
    [[nodiscard]] const GroundMapDefinition& ground_definition() const noexcept;
    [[nodiscard]] EntityUuid player_uuid() const noexcept;
    [[nodiscard]] Vec3 player_position() const noexcept;
    [[nodiscard]] Vec3 primary_prop_position() const noexcept;
    // Returns stable gameplay identities bound to authored physics roles in
    // authored body order. Helper sprites such as shadows are not returned.
    [[nodiscard]] std::vector<EntityUuid> actor_uuids(ScenePhysicsRole role) const;

    // True for a runtime crowd copy, which carries no rigid body and is
    // reached by segment casts through the scene's own actor index.
    [[nodiscard]] bool is_crowd_actor(EntityUuid actor) const noexcept;
    [[nodiscard]] std::optional<Vec3> actor_position(EntityUuid actor) const noexcept;
    // Removes a non-player actor from physics and presentation until reset().
    // Returns false for missing, already retired, or player identities.
    [[nodiscard]] bool retire_actor(EntityUuid actor) noexcept;
    // Starts a non-authoritative reaction override on a bound actor. Hurt
    // returns to locomotion. Death and explosion are independent terminal
    // one-shots: projectile death must never imply a detonation.
    [[nodiscard]] bool play_actor_hurt(EntityUuid actor);
    [[nodiscard]] bool begin_actor_death(EntityUuid actor);
    [[nodiscard]] bool begin_actor_explosion(EntityUuid actor);
    [[nodiscard]] bool is_actor_active(EntityUuid actor) const noexcept;
    [[nodiscard]] std::uint64_t completed_actor_hurt_animation_count() const noexcept;
    [[nodiscard]] std::uint64_t completed_actor_terminal_animation_count() const noexcept;
    [[nodiscard]] std::uint64_t completed_actor_death_animation_count() const noexcept;
    [[nodiscard]] std::uint64_t completed_actor_explosion_animation_count() const noexcept;
    // Removes a non-player entity from presentation until reset(). Unlike
    // retire_actor this needs no physics body, so a pickup or any other plain
    // placement can be taken out of the scene once it has been used.
    [[nodiscard]] bool retire_entity(EntityUuid entity) noexcept;

    // True when the entity is currently drawn. A world snapshot still carries
    // retired entities and the actors of retired bodies, because reset() has
    // to bring them back; anything that follows what is on screen, such as
    // viewport picking or a selection outline, has to ask this rather than
    // trust the snapshot alone.
    [[nodiscard]] bool is_entity_presented(EntityUuid entity) const noexcept;
    [[nodiscard]] std::optional<RuntimeSceneSegmentHit>
    cast_segment(const Vec2& start, const Vec2& end, EntityUuid ignored_entity = {}) const;
    [[nodiscard]] WorldSnapshot world_snapshot() const;
    // The optional region is a world-space X/Z bound on what to gather. It must
    // be generous enough to cover a sprite whose origin sits outside it and a
    // body that has moved since the last fixed tick.
    [[nodiscard]] std::vector<RenderItem2D>
    collect_render_items(float interpolation_alpha,
                         std::optional<RectXZ> region = std::nullopt) const;
    [[nodiscard]] std::vector<PhysicsFootprint> debug_footprints() const;
    [[nodiscard]] std::size_t entity_count() const noexcept;
    [[nodiscard]] std::size_t physics_body_count() const noexcept;
    [[nodiscard]] std::size_t animation_binding_count() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
