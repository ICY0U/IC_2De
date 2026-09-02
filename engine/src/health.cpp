#include "ic2d/health.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace ic2d {

struct Health::Impl {
    struct TargetState {
        float maximum_health{0.0F};
        float current_health{0.0F};
        bool alive{false};
    };

    std::map<std::uint64_t, TargetState> targets;
    std::set<HitIdentity> accepted_hits;
    std::vector<DamageCommand> pending;
    std::vector<HealthEvent> events;
    std::uint64_t tick{0};
    std::uint64_t next_sequence{1};
    std::uint64_t applied_hit_count{0};
    std::uint64_t death_count{0};
    std::uint64_t rejected_duplicate_hit_count{0};
};

Health::Health() : impl_{std::make_unique<Impl>()} {}
Health::~Health() = default;
Health::Health(Health&&) noexcept = default;
Health& Health::operator=(Health&&) noexcept = default;

bool Health::register_target(const HealthTargetDefinition& definition) noexcept {
    if (!definition.target || !std::isfinite(definition.maximum_health) ||
        !(definition.maximum_health > 0.0F)) {
        return false;
    }
    return impl_->targets.emplace(
        definition.target.value,
        Impl::TargetState{
            .maximum_health = definition.maximum_health,
            .current_health = definition.maximum_health,
            .alive = true,
        }).second;
}

bool Health::submit(const DamageCommand& command) noexcept {
    if (command.tick != impl_->tick + 1 || !command.hit.source || command.hit.value == 0 ||
        !command.target || !std::isfinite(command.damage) || !(command.damage > 0.0F)) {
        return false;
    }
    const auto target = impl_->targets.find(command.target.value);
    if (target == impl_->targets.end() || !target->second.alive) {
        return false;
    }
    if (!impl_->accepted_hits.insert(command.hit).second) {
        ++impl_->rejected_duplicate_hit_count;
        return false;
    }
    impl_->pending.push_back(command);
    return true;
}

void Health::fixed_update(const std::uint64_t tick) {
    if (tick != impl_->tick + 1) {
        throw std::invalid_argument{"Health fixed ticks must be one-based and sequential."};
    }

    for (const DamageCommand& command : impl_->pending) {
        auto target = impl_->targets.find(command.target.value);
        if (target == impl_->targets.end() || !target->second.alive) {
            continue;
        }
        Impl::TargetState& state = target->second;
        const float health_before = state.current_health;
        const float applied_damage = std::min(command.damage, health_before);
        state.current_health = health_before - applied_damage;
        ++impl_->applied_hit_count;
        impl_->events.emplace_back(DamageAppliedEvent{
            .tick = tick,
            .sequence = impl_->next_sequence++,
            .hit = command.hit,
            .target = command.target,
            .requested_damage = command.damage,
            .applied_damage = applied_damage,
            .health_before = health_before,
            .health_after = state.current_health,
        });
        if (state.current_health == 0.0F) {
            state.alive = false;
            ++impl_->death_count;
            impl_->events.emplace_back(ActorDiedEvent{
                .tick = tick,
                .sequence = impl_->next_sequence++,
                .killing_hit = command.hit,
                .target = command.target,
            });
        }
    }
    impl_->pending.clear();
    impl_->tick = tick;
}

HealthSnapshot Health::snapshot() const {
    HealthSnapshot result{
        .tick = impl_->tick,
        .applied_hit_count = impl_->applied_hit_count,
        .death_count = impl_->death_count,
        .rejected_duplicate_hit_count = impl_->rejected_duplicate_hit_count,
        .pending_damage_count = impl_->pending.size(),
        .next_event_sequence = impl_->next_sequence,
    };
    result.accepted_hits.reserve(impl_->accepted_hits.size());
    for (const HitIdentity& hit : impl_->accepted_hits) {
        result.accepted_hits.push_back(hit);
    }
    result.targets.reserve(impl_->targets.size());
    for (const auto& [target, state] : impl_->targets) {
        result.targets.push_back({
            .target = EntityUuid{target},
            .maximum_health = state.maximum_health,
            .current_health = state.current_health,
            .alive = state.alive,
        });
    }
    return result;
}

std::vector<HealthEvent> Health::drain_events() {
    std::vector<HealthEvent> drained;
    drained.swap(impl_->events);
    return drained;
}

void Health::reset() noexcept {
    for (auto& [target, state] : impl_->targets) {
        static_cast<void>(target);
        state.current_health = state.maximum_health;
        state.alive = true;
    }
    impl_->accepted_hits.clear();
    impl_->pending.clear();
    impl_->events.clear();
    impl_->tick = 0;
    impl_->next_sequence = 1;
    impl_->applied_hit_count = 0;
    impl_->death_count = 0;
    impl_->rejected_duplicate_hit_count = 0;
}

} // namespace ic2d
