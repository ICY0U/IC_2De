#include "ic2d/enemy_intent.hpp"

#include <cmath>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace ic2d {
namespace {

[[nodiscard]] bool finite(const Vec2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool valid(const EnemyIntentDefinition& definition) noexcept {
    return definition.actor && definition.target && definition.actor != definition.target &&
           std::isfinite(definition.movement_speed) && definition.movement_speed > 0.0F &&
           std::isfinite(definition.acquisition_range) && definition.acquisition_range > 0.0F &&
           std::isfinite(definition.attack_range) && definition.attack_range > 0.0F &&
           definition.attack_range <= definition.acquisition_range &&
           definition.attack_cooldown_ticks > 0 && std::isfinite(definition.attack_damage) &&
           definition.attack_damage > 0.0F;
}

} // namespace

struct EnemyIntent::Impl {
    struct ActorState {
        EnemyIntentDefinition definition{};
        EnemyIntentState state{EnemyIntentState::unaware};
        bool acquired{false};
        Vec2 movement_direction{};
        Vec2 facing_direction{0.0F, 1.0F};
        float distance_to_target{0.0F};
        std::uint32_t cooldown_ticks_remaining{0};
        std::uint64_t attack_count{0};
    };

    std::map<std::uint64_t, ActorState> actors;
    std::vector<EnemyIntentEvent> events;
    std::uint64_t tick{0};
    std::uint64_t next_event_sequence{1};
    std::uint64_t acquisition_count{0};
    std::uint64_t attack_count{0};
};

EnemyIntent::EnemyIntent() : impl_{std::make_unique<Impl>()} {}
EnemyIntent::~EnemyIntent() = default;
EnemyIntent::EnemyIntent(EnemyIntent&&) noexcept = default;
EnemyIntent& EnemyIntent::operator=(EnemyIntent&&) noexcept = default;

bool EnemyIntent::register_actor(const EnemyIntentDefinition& definition) noexcept {
    if (impl_->tick != 0 || !valid(definition)) {
        return false;
    }
    return impl_->actors.emplace(definition.actor.value, Impl::ActorState{.definition = definition})
        .second;
}

void EnemyIntent::fixed_update(const std::uint64_t tick,
                               const std::vector<EnemyPerception>& perceptions) {
    if (tick != impl_->tick + 1) {
        throw std::invalid_argument{"EnemyIntent fixed ticks must be one-based and sequential."};
    }
    if (perceptions.size() != impl_->actors.size()) {
        throw std::invalid_argument{"EnemyIntent requires one perception per registered actor."};
    }

    std::unordered_map<std::uint64_t, const EnemyPerception*> perception_by_actor;
    perception_by_actor.reserve(perceptions.size());
    for (const EnemyPerception& perception : perceptions) {
        // Everything a perception can be rejected for is checked here, before
        // any actor state is touched, so a rejected tick leaves the module
        // exactly as it was.
        const auto registered = impl_->actors.find(perception.actor.value);
        if (!perception.actor || !perception.target || !finite(perception.actor_position) ||
            !finite(perception.target_position) || registered == impl_->actors.end() ||
            perception.target != registered->second.definition.target ||
            !perception_by_actor.emplace(perception.actor.value, &perception).second) {
            throw std::invalid_argument{
                "EnemyIntent perceptions require unique identities and finite positions."};
        }
    }

    for (auto& [actor_id, actor] : impl_->actors) {
        const auto found = perception_by_actor.find(actor_id);
        if (found == perception_by_actor.end()) {
            throw std::invalid_argument{
                "EnemyIntent perception identities must match registered definitions."};
        }
        const EnemyPerception& perception = *found->second;
        const Vec2 delta{
            perception.target_position.x - perception.actor_position.x,
            perception.target_position.y - perception.actor_position.y,
        };
        const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (!std::isfinite(distance)) {
            throw std::invalid_argument{"EnemyIntent perception distance must be finite."};
        }
        actor.distance_to_target = distance;
        actor.movement_direction = {};

        if (!perception.actor_alive || !perception.target_alive) {
            actor.state = EnemyIntentState::inactive;
            continue;
        }

        constexpr float direction_epsilon = 0.0001F;
        Vec2 direction{};
        if (distance > direction_epsilon) {
            direction = {delta.x / distance, delta.y / distance};
            actor.facing_direction = direction;
        }

        if (!actor.acquired && distance <= actor.definition.acquisition_range) {
            actor.acquired = true;
            ++impl_->acquisition_count;
            impl_->events.emplace_back(EnemyAcquiredTargetEvent{
                .tick = tick,
                .sequence = impl_->next_event_sequence++,
                .actor = actor.definition.actor,
                .target = actor.definition.target,
            });
        }

        if (!actor.acquired) {
            actor.state = EnemyIntentState::unaware;
            continue;
        }

        if (actor.cooldown_ticks_remaining > 0) {
            --actor.cooldown_ticks_remaining;
        }
        if (distance > actor.definition.attack_range) {
            actor.state = EnemyIntentState::pursuing;
            actor.movement_direction = direction;
            continue;
        }

        actor.state = EnemyIntentState::attacking;
        if (actor.cooldown_ticks_remaining == 0) {
            actor.cooldown_ticks_remaining = actor.definition.attack_cooldown_ticks;
            ++actor.attack_count;
            ++impl_->attack_count;
            impl_->events.emplace_back(EnemyAttackRequestedEvent{
                .tick = tick,
                .sequence = impl_->next_event_sequence++,
                .actor = actor.definition.actor,
                .target = actor.definition.target,
                .damage = actor.definition.attack_damage,
            });
        }
    }
    impl_->tick = tick;
}

EnemyIntentSnapshot EnemyIntent::snapshot() const {
    EnemyIntentSnapshot result{
        .tick = impl_->tick,
        .next_event_sequence = impl_->next_event_sequence,
        .acquisition_count = impl_->acquisition_count,
        .attack_count = impl_->attack_count,
    };
    result.actors.reserve(impl_->actors.size());
    for (const auto& [actor_id, actor] : impl_->actors) {
        static_cast<void>(actor_id);
        result.actors.push_back({
            .actor = actor.definition.actor,
            .target = actor.definition.target,
            .state = actor.state,
            .acquired = actor.acquired,
            .movement_direction = actor.movement_direction,
            .facing_direction = actor.facing_direction,
            .distance_to_target = actor.distance_to_target,
            .movement_speed = actor.definition.movement_speed,
            .acquisition_range = actor.definition.acquisition_range,
            .attack_range = actor.definition.attack_range,
            .attack_cooldown_ticks = actor.definition.attack_cooldown_ticks,
            .attack_cooldown_ticks_remaining = actor.cooldown_ticks_remaining,
            .attack_damage = actor.definition.attack_damage,
            .attack_count = actor.attack_count,
        });
    }
    return result;
}

std::vector<EnemyIntentEvent> EnemyIntent::drain_events() {
    std::vector<EnemyIntentEvent> drained;
    drained.swap(impl_->events);
    return drained;
}

void EnemyIntent::reset() noexcept {
    for (auto& [actor_id, actor] : impl_->actors) {
        static_cast<void>(actor_id);
        const EnemyIntentDefinition definition = actor.definition;
        actor = Impl::ActorState{.definition = definition};
    }
    impl_->events.clear();
    impl_->tick = 0;
    impl_->next_event_sequence = 1;
    impl_->acquisition_count = 0;
    impl_->attack_count = 0;
}

} // namespace ic2d
