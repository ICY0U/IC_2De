#include "ic2d/physics2d.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ic2d {
namespace {

[[nodiscard]] bool finite(const Vec2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] b2BodyType to_box2d(const PhysicsMotionType motion) noexcept {
    switch (motion) {
    case PhysicsMotionType::static_body:
        return b2_staticBody;
    case PhysicsMotionType::kinematic_body:
        return b2_kinematicBody;
    case PhysicsMotionType::dynamic_body:
        return b2_dynamicBody;
    }
    return b2_staticBody;
}

[[nodiscard]] bool less(const PhysicsBodyId left, const PhysicsBodyId right) noexcept {
    return left.slot < right.slot || (left.slot == right.slot && left.generation < right.generation);
}

} // namespace

struct PhysicsWorld::Impl {
    struct BodyRecord {
        PhysicsBodyId public_id{};
        b2BodyId body_id{};
        b2ShapeId shape_id{};
        PhysicsMotionType motion{PhysicsMotionType::static_body};
        Vec2 half_extents{};
        std::uint32_t tag{0};
        bool sensor{false};
    };

    struct Slot {
        std::uint32_t generation{1};
        std::unique_ptr<BodyRecord> body;
    };

    explicit Impl(const PhysicsWorldConfig& requested_config)
        : config{requested_config} {
        const bool valid_config = std::isfinite(config.pixels_per_metre) &&
                                  config.pixels_per_metre > 0.0F &&
                                  finite(config.gravity_pixels_per_second_squared) &&
                                  config.substep_count > 0 && config.substep_count <= 32;
        if (!valid_config) {
            throw std::invalid_argument{"Physics world configuration is invalid."};
        }

        b2WorldDef world_definition = b2DefaultWorldDef();
        world_definition.gravity = to_metres(config.gravity_pixels_per_second_squared);
        world_id = b2CreateWorld(&world_definition);
        if (B2_IS_NULL(world_id)) {
            throw std::runtime_error{"Box2D could not create a physics world."};
        }
        b2World_EnableSleeping(world_id, config.enable_sleep);
        slots.emplace_back(); // Slot zero is always an invalid public handle.
    }

    ~Impl() {
        if (B2_IS_NON_NULL(world_id)) {
            b2DestroyWorld(world_id);
        }
    }

    [[nodiscard]] b2Vec2 to_metres(const Vec2 value) const noexcept {
        return {value.x / config.pixels_per_metre, value.y / config.pixels_per_metre};
    }

    [[nodiscard]] Vec2 to_pixels(const b2Vec2 value) const noexcept {
        return {value.x * config.pixels_per_metre, value.y * config.pixels_per_metre};
    }

    [[nodiscard]] BodyRecord* find(const PhysicsBodyId id) noexcept {
        if (id.slot == 0 || id.slot >= slots.size()) {
            return nullptr;
        }
        Slot& slot = slots[id.slot];
        return slot.generation == id.generation ? slot.body.get() : nullptr;
    }

    [[nodiscard]] const BodyRecord* find(const PhysicsBodyId id) const noexcept {
        if (id.slot == 0 || id.slot >= slots.size()) {
            return nullptr;
        }
        const Slot& slot = slots[id.slot];
        return slot.generation == id.generation ? slot.body.get() : nullptr;
    }

    [[nodiscard]] BodyRecord* from_shape(const b2ShapeId shape_id) noexcept {
        if (!b2Shape_IsValid(shape_id)) {
            return nullptr;
        }
        auto* record = static_cast<BodyRecord*>(b2Shape_GetUserData(shape_id));
        return record != nullptr && find(record->public_id) == record ? record : nullptr;
    }

    [[nodiscard]] PhysicsBodySnapshot make_snapshot(const BodyRecord& record) const noexcept {
        const b2Rot rotation = b2Body_GetRotation(record.body_id);
        return {
            .body = record.public_id,
            .center = to_pixels(b2Body_GetPosition(record.body_id)),
            .linear_velocity = to_pixels(b2Body_GetLinearVelocity(record.body_id)),
            .rotation_radians = b2Rot_GetAngle(rotation),
            .awake = b2Body_IsAwake(record.body_id),
        };
    }

    void append_event(
        std::vector<PhysicsEvent>& output,
        const PhysicsEventKind kind,
        BodyRecord* body_a,
        BodyRecord* body_b,
        const bool preserve_order
    ) const {
        if (body_a == nullptr || body_b == nullptr) {
            return;
        }
        if (!preserve_order && less(body_b->public_id, body_a->public_id)) {
            std::swap(body_a, body_b);
        }
        output.push_back({
            .kind = kind,
            .body_a = body_a->public_id,
            .body_b = body_b->public_id,
            .tag_a = body_a->tag,
            .tag_b = body_b->tag,
        });
    }

    PhysicsWorldConfig config{};
    b2WorldId world_id{};
    std::vector<Slot> slots;
    std::vector<std::uint32_t> free_slots;
};

PhysicsWorld::PhysicsWorld(const PhysicsWorldConfig config)
    : impl_{std::make_unique<Impl>(config)} {}

PhysicsWorld::~PhysicsWorld() = default;
PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

PhysicsBodyId PhysicsWorld::create_box(const PhysicsBoxDefinition& definition) {
    const bool valid_definition = finite(definition.center) && finite(definition.half_extents) &&
                                  finite(definition.linear_velocity) &&
                                  definition.half_extents.x > 0.0F &&
                                  definition.half_extents.y > 0.0F &&
                                  std::isfinite(definition.rotation_radians) &&
                                  std::isfinite(definition.angular_velocity_radians) &&
                                  std::isfinite(definition.linear_damping) &&
                                  definition.linear_damping >= 0.0F &&
                                  std::isfinite(definition.angular_damping) &&
                                  definition.angular_damping >= 0.0F &&
                                  std::isfinite(definition.gravity_scale) &&
                                  std::isfinite(definition.density) && definition.density >= 0.0F &&
                                  std::isfinite(definition.friction) && definition.friction >= 0.0F &&
                                  std::isfinite(definition.restitution) &&
                                  definition.restitution >= 0.0F && definition.restitution <= 1.0F &&
                                  definition.category_bits != 0;
    if (!valid_definition) {
        throw std::invalid_argument{"Physics box definition is invalid."};
    }

    std::uint32_t slot_index = 0;
    if (!impl_->free_slots.empty()) {
        slot_index = impl_->free_slots.back();
        impl_->free_slots.pop_back();
    } else {
        if (impl_->slots.size() >= std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error{"Physics body handle capacity was exhausted."};
        }
        slot_index = static_cast<std::uint32_t>(impl_->slots.size());
        impl_->slots.emplace_back();
    }

    Impl::Slot& slot = impl_->slots[slot_index];
    auto record = std::make_unique<Impl::BodyRecord>();
    record->public_id = {slot_index, slot.generation};
    record->motion = definition.motion;
    record->half_extents = definition.half_extents;
    record->tag = definition.tag;
    record->sensor = definition.sensor;

    b2BodyDef body_definition = b2DefaultBodyDef();
    body_definition.type = to_box2d(definition.motion);
    body_definition.position = impl_->to_metres(definition.center);
    body_definition.rotation = b2MakeRot(definition.rotation_radians);
    body_definition.linearVelocity = impl_->to_metres(definition.linear_velocity);
    body_definition.angularVelocity = definition.angular_velocity_radians;
    body_definition.linearDamping = definition.linear_damping;
    body_definition.angularDamping = definition.angular_damping;
    body_definition.gravityScale = definition.gravity_scale;
    body_definition.userData = record.get();
    body_definition.fixedRotation = definition.fixed_rotation;
    record->body_id = b2CreateBody(impl_->world_id, &body_definition);

    b2ShapeDef shape_definition = b2DefaultShapeDef();
    shape_definition.userData = record.get();
    shape_definition.density = definition.density;
    shape_definition.material.friction = definition.friction;
    shape_definition.material.restitution = definition.restitution;
    shape_definition.filter.categoryBits = definition.category_bits;
    shape_definition.filter.maskBits = definition.mask_bits;
    shape_definition.isSensor = definition.sensor;
    shape_definition.enableSensorEvents = true;
    shape_definition.enableContactEvents = !definition.sensor;
    const b2Vec2 half_extents_metres = impl_->to_metres(definition.half_extents);
    const b2Polygon box = b2MakeBox(half_extents_metres.x, half_extents_metres.y);
    record->shape_id = b2CreatePolygonShape(record->body_id, &shape_definition, &box);

    slot.body = std::move(record);
    return slot.body->public_id;
}

bool PhysicsWorld::destroy_body(const PhysicsBodyId body) noexcept {
    Impl::BodyRecord* record = impl_->find(body);
    if (record == nullptr) {
        return false;
    }
    b2DestroyBody(record->body_id);
    Impl::Slot& slot = impl_->slots[body.slot];
    slot.body.reset();
    ++slot.generation;
    if (slot.generation == 0) {
        slot.generation = 1;
    }
    impl_->free_slots.push_back(body.slot);
    return true;
}

bool PhysicsWorld::set_transform(
    const PhysicsBodyId body,
    const Vec2& center,
    const float rotation_radians
) {
    if (!finite(center) || !std::isfinite(rotation_radians)) {
        throw std::invalid_argument{"Physics transform must be finite."};
    }
    Impl::BodyRecord* record = impl_->find(body);
    if (record == nullptr) {
        return false;
    }
    b2Body_SetTransform(record->body_id, impl_->to_metres(center), b2MakeRot(rotation_radians));
    return true;
}

bool PhysicsWorld::set_linear_velocity(const PhysicsBodyId body, const Vec2& velocity) {
    if (!finite(velocity)) {
        throw std::invalid_argument{"Physics velocity must be finite."};
    }
    Impl::BodyRecord* record = impl_->find(body);
    if (record == nullptr || record->motion == PhysicsMotionType::static_body) {
        return false;
    }
    b2Body_SetLinearVelocity(record->body_id, impl_->to_metres(velocity));
    return true;
}

bool PhysicsWorld::set_kinematic_target(
    const PhysicsBodyId body,
    const Vec2& center,
    const float time_step_seconds
) {
    if (!finite(center) || !std::isfinite(time_step_seconds) || time_step_seconds <= 0.0F) {
        throw std::invalid_argument{"Kinematic targets require a finite target and positive time step."};
    }
    Impl::BodyRecord* record = impl_->find(body);
    if (record == nullptr || record->motion != PhysicsMotionType::kinematic_body) {
        return false;
    }
    const b2Transform current = b2Body_GetTransform(record->body_id);
    const b2Vec2 target_position = impl_->to_metres(center);
    constexpr float stationary_epsilon_metres = 0.000001F;
    if (std::abs(target_position.x - current.p.x) <= stationary_epsilon_metres &&
        std::abs(target_position.y - current.p.y) <= stationary_epsilon_metres) {
        // Box2D deliberately leaves the previous kinematic velocity unchanged
        // for an effectively identical target. Engine target semantics require
        // that a stationary target stops on this fixed tick.
        b2Body_SetLinearVelocity(record->body_id, b2Vec2_zero);
        b2Body_SetAngularVelocity(record->body_id, 0.0F);
        return true;
    }
    b2Body_SetTargetTransform(
        record->body_id,
        {target_position, current.q},
        time_step_seconds);
    return true;
}

std::optional<PhysicsBodySnapshot> PhysicsWorld::snapshot(const PhysicsBodyId body) const noexcept {
    const Impl::BodyRecord* record = impl_->find(body);
    if (record == nullptr) {
        return std::nullopt;
    }
    return impl_->make_snapshot(*record);
}

PhysicsStepResult PhysicsWorld::step(const float time_step_seconds) {
    if (!std::isfinite(time_step_seconds) || time_step_seconds <= 0.0F) {
        throw std::invalid_argument{"Physics stepping requires a finite positive time step."};
    }

    b2World_Step(impl_->world_id, time_step_seconds, impl_->config.substep_count);
    PhysicsStepResult result;
    result.bodies.reserve(impl_->slots.size() - impl_->free_slots.size());
    for (std::size_t index = 1; index < impl_->slots.size(); ++index) {
        if (impl_->slots[index].body) {
            result.bodies.push_back(impl_->make_snapshot(*impl_->slots[index].body));
        }
    }

    const b2ContactEvents contacts = b2World_GetContactEvents(impl_->world_id);
    result.events.reserve(static_cast<std::size_t>(contacts.beginCount + contacts.endCount));
    for (int index = 0; index < contacts.beginCount; ++index) {
        const b2ContactBeginTouchEvent& event = contacts.beginEvents[index];
        impl_->append_event(result.events, PhysicsEventKind::contact_begin,
                            impl_->from_shape(event.shapeIdA), impl_->from_shape(event.shapeIdB), false);
    }
    for (int index = 0; index < contacts.endCount; ++index) {
        const b2ContactEndTouchEvent& event = contacts.endEvents[index];
        impl_->append_event(result.events, PhysicsEventKind::contact_end,
                            impl_->from_shape(event.shapeIdA), impl_->from_shape(event.shapeIdB), false);
    }

    const b2SensorEvents sensors = b2World_GetSensorEvents(impl_->world_id);
    result.events.reserve(result.events.size() +
                          static_cast<std::size_t>(sensors.beginCount + sensors.endCount));
    for (int index = 0; index < sensors.beginCount; ++index) {
        const b2SensorBeginTouchEvent& event = sensors.beginEvents[index];
        impl_->append_event(result.events, PhysicsEventKind::trigger_begin,
                            impl_->from_shape(event.sensorShapeId),
                            impl_->from_shape(event.visitorShapeId), true);
    }
    for (int index = 0; index < sensors.endCount; ++index) {
        const b2SensorEndTouchEvent& event = sensors.endEvents[index];
        impl_->append_event(result.events, PhysicsEventKind::trigger_end,
                            impl_->from_shape(event.sensorShapeId),
                            impl_->from_shape(event.visitorShapeId), true);
    }

    std::ranges::sort(result.events, [](const PhysicsEvent& left, const PhysicsEvent& right) {
        if (left.kind != right.kind) {
            return left.kind < right.kind;
        }
        if (left.body_a != right.body_a) {
            return less(left.body_a, right.body_a);
        }
        return less(left.body_b, right.body_b);
    });
    return result;
}

std::vector<PhysicsFootprint> PhysicsWorld::debug_footprints() const {
    std::vector<PhysicsFootprint> footprints;
    footprints.reserve(impl_->slots.size() - impl_->free_slots.size());
    for (std::size_t index = 1; index < impl_->slots.size(); ++index) {
        const std::unique_ptr<Impl::BodyRecord>& record = impl_->slots[index].body;
        if (!record) {
            continue;
        }
        const PhysicsBodySnapshot body_snapshot = impl_->make_snapshot(*record);
        footprints.push_back({
            .body = record->public_id,
            .motion = record->motion,
            .center = body_snapshot.center,
            .half_extents = record->half_extents,
            .rotation_radians = body_snapshot.rotation_radians,
            .sensor = record->sensor,
        });
    }
    return footprints;
}

} // namespace ic2d
