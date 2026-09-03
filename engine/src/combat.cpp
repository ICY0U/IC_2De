#include "ic2d/combat.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace ic2d {
namespace {

[[nodiscard]] std::size_t intent_index(const CombatIntent intent) noexcept {
    return static_cast<std::size_t>(intent);
}

[[nodiscard]] bool finite(const Vec2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool normalize_direction(Vec2& direction) noexcept {
    if (!finite(direction)) {
        return false;
    }
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (!(length > 0.0001F)) {
        return false;
    }
    direction.x /= length;
    direction.y /= length;
    return true;
}

[[nodiscard]] bool has_requested_intent(const CombatCommand& command) noexcept {
    for (const CombatIntent intent : combat_intents) {
        if (command.requested(intent)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_ordered_action(const CombatCommand& command) noexcept {
    return has_requested_intent(command) || command.fire_held_update().has_value();
}

} // namespace

struct Combat::Impl {
    struct PendingProjectile {
        EntityUuid actor{};
        Vec2 aim_direction{};
    };

    [[nodiscard]] WeaponSnapshot& weapon_for(const EntityUuid actor) {
        const auto [found, inserted] = weapons.try_emplace(actor.value);
        if (inserted) {
            found->second = WeaponSnapshot{
                .weapon = needle_pistol.kind,
                .magazine_ammo = needle_pistol.magazine_capacity,
                .reserve_ammo = needle_pistol.initial_reserve_ammo,
            };
        }
        return found->second;
    }

    [[nodiscard]] Vec2& aim_for(const EntityUuid actor) {
        return aim_directions.try_emplace(actor.value, Vec2{0.0F, 1.0F}).first->second;
    }

    [[nodiscard]] DodgeSnapshot& dodge_for(const EntityUuid actor) {
        return dodges.try_emplace(actor.value).first->second;
    }

    void refresh_actor_snapshots() {
        snapshot.actors.clear();
        snapshot.actors.reserve(weapons.size());
        for (const auto& [actor_value, weapon] : weapons) {
            const auto aim = aim_directions.find(actor_value);
            const auto dodge = dodges.find(actor_value);
            const auto held = held_fire.find(actor_value);
            snapshot.actors.push_back({
                .actor = EntityUuid{actor_value},
                .aim_direction = aim != aim_directions.end() ? aim->second : Vec2{0.0F, 1.0F},
                .weapon = weapon,
                .dodge = dodge != dodges.end() ? dodge->second : DodgeSnapshot{},
                .fire_held = held != held_fire.end() && held->second,
            });
        }
        std::ranges::sort(snapshot.actors, {},
                          [](const CombatActorSnapshot& actor) { return actor.actor.value; });
        snapshot.next_event_sequence = next_sequence;
        snapshot.next_projectile_id = next_projectile_id;
    }

    std::vector<CombatCommand> pending;
    std::vector<CombatEvent> events;
    std::unordered_map<std::uint64_t, WeaponSnapshot> weapons;
    std::unordered_map<std::uint64_t, Vec2> aim_directions;
    std::unordered_map<std::uint64_t, DodgeSnapshot> dodges;
    // Ordered identity makes simultaneous automatic-fire results deterministic.
    std::map<std::uint64_t, bool> held_fire;
    CombatSnapshot snapshot{};
    std::uint64_t next_sequence{1};
    std::uint64_t next_projectile_id{1};
};

std::string_view combat_intent_name(const CombatIntent intent) noexcept {
    switch (intent) {
    case CombatIntent::fire:
        return "Fire";
    case CombatIntent::reload:
        return "Reload";
    case CombatIntent::dodge:
        return "Dodge";
    case CombatIntent::swap_weapon:
        return "Swap weapon";
    case CombatIntent::count:
        break;
    }
    return "Unknown";
}

void CombatCommand::request(const CombatIntent intent, const bool requested_value) noexcept {
    const std::size_t index = intent_index(intent);
    if (index < intents_.size()) {
        intents_[index] = requested_value;
    }
}

void CombatCommand::set_fire_held(const bool held) noexcept { fire_held_update_ = held; }

bool CombatCommand::requested(const CombatIntent intent) const noexcept {
    const std::size_t index = intent_index(intent);
    return index < intents_.size() && intents_[index];
}

std::optional<bool> CombatCommand::fire_held_update() const noexcept { return fire_held_update_; }

bool CombatCommand::empty() const noexcept {
    if (aim_direction || fire_held_update_) {
        return false;
    }
    for (const CombatIntent intent : combat_intents) {
        if (requested(intent)) {
            return false;
        }
    }
    return true;
}

Combat::Combat() : impl_{std::make_unique<Impl>()} {}
Combat::~Combat() = default;
Combat::Combat(Combat&&) noexcept = default;
Combat& Combat::operator=(Combat&&) noexcept = default;

bool Combat::submit(const CombatCommand& command) noexcept {
    if (!command.actor || command.empty() ||
        (command.aim_direction && !finite(*command.aim_direction)) ||
        (command.dodge_direction && !command.requested(CombatIntent::dodge))) {
        return false;
    }
    CombatCommand accepted = command;
    if (accepted.aim_direction) {
        Vec2& aim = *accepted.aim_direction;
        const float length = std::sqrt(aim.x * aim.x + aim.y * aim.y);
        if (!(length > 0.0001F)) {
            return false;
        }
        if (length > 1.0F) {
            aim.x /= length;
            aim.y /= length;
        }
    }
    if (accepted.dodge_direction && !normalize_direction(*accepted.dodge_direction)) {
        return false;
    }

    // Mouse/controller aim can arrive several times between fixed ticks. Keep
    // only the newest consecutive aim-only sample for an actor, while leaving
    // commands containing action edges or held-state transitions in strict FIFO
    // order. A later mouse sample must never overwrite an LMB release.
    if (accepted.aim_direction && !has_ordered_action(accepted) && !impl_->pending.empty()) {
        CombatCommand& newest = impl_->pending.back();
        if (newest.actor == accepted.actor && newest.aim_direction && !has_ordered_action(newest)) {
            newest = std::move(accepted);
            return true;
        }
    }
    impl_->pending.push_back(std::move(accepted));
    impl_->snapshot.pending_command_count = impl_->pending.size();
    return true;
}

std::uint32_t Combat::resupply(const EntityUuid actor, const std::uint32_t rounds) noexcept {
    if (!actor || rounds == 0) {
        return 0;
    }
    WeaponSnapshot& weapon = impl_->weapon_for(actor);
    const std::uint32_t ceiling = needle_pistol.maximum_reserve_ammo;
    if (weapon.reserve_ammo >= ceiling) {
        return 0;
    }
    const std::uint32_t taken = std::min(rounds, ceiling - weapon.reserve_ammo);
    weapon.reserve_ammo += taken;

    // The cached snapshot is otherwise only rebuilt on a tick. Resupply applies
    // immediately, so it republishes what it changed rather than leaving a
    // reader to see a stale reserve until the next fixed update.
    if (impl_->snapshot.latest_actor == actor || !impl_->snapshot.latest_actor) {
        impl_->snapshot.latest_actor = actor;
        impl_->snapshot.weapon = weapon;
    }
    impl_->refresh_actor_snapshots();
    return taken;
}

bool Combat::replenish(const EntityUuid actor) noexcept {
    if (!actor) {
        return false;
    }
    WeaponSnapshot& weapon = impl_->weapon_for(actor);
    weapon.magazine_ammo = needle_pistol.magazine_capacity;
    weapon.reserve_ammo = needle_pistol.maximum_reserve_ammo;
    weapon.reloading = false;
    weapon.reload_ticks_remaining = 0;

    // Applied immediately, so like resupply it republishes what it changed
    // rather than leaving a reader on a stale magazine until the next tick.
    if (impl_->snapshot.latest_actor == actor || !impl_->snapshot.latest_actor) {
        impl_->snapshot.latest_actor = actor;
        impl_->snapshot.weapon = weapon;
    }
    impl_->refresh_actor_snapshots();
    return true;
}

void Combat::fixed_update(const std::uint64_t tick) {
    const std::uint64_t expected_tick = impl_->snapshot.tick + 1;
    if (tick != expected_tick) {
        throw std::invalid_argument{"Combat fixed ticks must be one-based and sequential."};
    }
    for (auto& [actor, weapon] : impl_->weapons) {
        static_cast<void>(actor);
        if (weapon.fire_cooldown_ticks_remaining > 0) {
            --weapon.fire_cooldown_ticks_remaining;
        }
        if (weapon.reloading && weapon.reload_ticks_remaining > 0) {
            --weapon.reload_ticks_remaining;
            if (weapon.reload_ticks_remaining == 0) {
                const std::uint32_t missing =
                    needle_pistol.magazine_capacity - weapon.magazine_ammo;
                const std::uint32_t transferred = std::min(missing, weapon.reserve_ammo);
                weapon.magazine_ammo += transferred;
                weapon.reserve_ammo -= transferred;
                weapon.reloading = false;
            }
        }
    }
    for (auto& [actor, dodge] : impl_->dodges) {
        static_cast<void>(actor);
        if (dodge.active_ticks_remaining > 0) {
            --dodge.active_ticks_remaining;
        }
        if (dodge.invulnerable_ticks_remaining > 0) {
            --dodge.invulnerable_ticks_remaining;
        }
        if (dodge.cooldown_ticks_remaining > 0) {
            --dodge.cooldown_ticks_remaining;
        }
        dodge.active = dodge.active_ticks_remaining > 0;
        dodge.invulnerable = dodge.invulnerable_ticks_remaining > 0;
    }
    std::vector<Impl::PendingProjectile> projectiles;
    for (const CombatCommand& command : impl_->pending) {
        impl_->snapshot.latest_actor = command.actor;
        WeaponSnapshot& weapon = impl_->weapon_for(command.actor);
        Vec2& actor_aim = impl_->aim_for(command.actor);
        if (command.aim_direction) {
            actor_aim = *command.aim_direction;
        }
        impl_->snapshot.aim_direction = actor_aim;
        if (command.fire_held_update()) {
            impl_->held_fire[command.actor.value] = *command.fire_held_update();
        }
        ++impl_->snapshot.consumed_command_count;
        for (const CombatIntent intent : combat_intents) {
            if (!command.requested(intent)) {
                continue;
            }
            impl_->events.emplace_back(CombatIntentEvent{
                .tick = tick,
                .sequence = impl_->next_sequence++,
                .actor = command.actor,
                .intent = intent,
                .aim_direction = actor_aim,
            });
            ++impl_->snapshot.emitted_intent_count;
            if (intent == CombatIntent::fire && !weapon.reloading && weapon.magazine_ammo > 0 &&
                weapon.fire_cooldown_ticks_remaining == 0) {
                --weapon.magazine_ammo;
                weapon.fire_cooldown_ticks_remaining = needle_pistol.fire_cooldown_ticks;
                projectiles.push_back({
                    .actor = command.actor,
                    .aim_direction = actor_aim,
                });
            } else if (intent == CombatIntent::reload && !weapon.reloading &&
                       weapon.magazine_ammo < needle_pistol.magazine_capacity &&
                       weapon.reserve_ammo > 0) {
                weapon.reloading = true;
                weapon.reload_ticks_remaining = needle_pistol.reload_duration_ticks;
            } else if (intent == CombatIntent::dodge) {
                DodgeSnapshot& dodge = impl_->dodge_for(command.actor);
                if (dodge.cooldown_ticks_remaining == 0) {
                    Vec2 direction = command.dodge_direction.value_or(actor_aim);
                    if (!normalize_direction(direction)) {
                        direction = {0.0F, 1.0F};
                    }
                    dodge.direction = direction;
                    dodge.active_ticks_remaining = player_dodge.duration_ticks;
                    dodge.invulnerable_ticks_remaining = player_dodge.invulnerability_ticks;
                    dodge.cooldown_ticks_remaining = player_dodge.cooldown_ticks;
                    dodge.active = true;
                    dodge.invulnerable = true;
                    ++dodge.started_count;
                    impl_->events.emplace_back(DodgeStartedEvent{
                        .tick = tick,
                        .sequence = impl_->next_sequence++,
                        .actor = command.actor,
                        .direction = direction,
                        .duration_ticks = player_dodge.duration_ticks,
                        .invulnerability_ticks = player_dodge.invulnerability_ticks,
                        .cooldown_ticks = player_dodge.cooldown_ticks,
                    });
                }
            }
        }
        impl_->snapshot.weapon = weapon;
        impl_->snapshot.dodge = impl_->dodge_for(command.actor);
    }
    for (const auto& [actor_value, held] : impl_->held_fire) {
        if (!held) {
            continue;
        }
        const EntityUuid actor{actor_value};
        WeaponSnapshot& weapon = impl_->weapon_for(actor);
        if (weapon.reloading || weapon.magazine_ammo == 0 ||
            weapon.fire_cooldown_ticks_remaining != 0) {
            continue;
        }
        --weapon.magazine_ammo;
        weapon.fire_cooldown_ticks_remaining = needle_pistol.fire_cooldown_ticks;
        projectiles.push_back({
            .actor = actor,
            .aim_direction = impl_->aim_for(actor),
        });
    }
    for (const Impl::PendingProjectile& projectile : projectiles) {
        impl_->events.emplace_back(ProjectileSpawnedEvent{
            .tick = tick,
            .sequence = impl_->next_sequence++,
            .projectile_id = impl_->next_projectile_id++,
            .actor = projectile.actor,
            .weapon = needle_pistol.kind,
            .aim_direction = projectile.aim_direction,
            .speed = needle_pistol.projectile_speed,
            .lifetime_ticks = needle_pistol.projectile_lifetime_ticks,
            .damage = needle_pistol.projectile_damage,
        });
        ++impl_->snapshot.spawned_projectile_count;
    }
    if (impl_->snapshot.latest_actor) {
        const auto found = impl_->weapons.find(impl_->snapshot.latest_actor.value);
        if (found != impl_->weapons.end()) {
            impl_->snapshot.weapon = found->second;
        }
        const auto dodge = impl_->dodges.find(impl_->snapshot.latest_actor.value);
        if (dodge != impl_->dodges.end()) {
            impl_->snapshot.dodge = dodge->second;
        }
    }
    impl_->pending.clear();
    impl_->snapshot.tick = tick;
    impl_->snapshot.pending_command_count = 0;
    impl_->refresh_actor_snapshots();
}

CombatSnapshot Combat::snapshot() const { return impl_->snapshot; }

bool Combat::invulnerable(const EntityUuid actor) const noexcept {
    const auto found = impl_->dodges.find(actor.value);
    return found != impl_->dodges.end() && found->second.invulnerable;
}

std::vector<CombatEvent> Combat::drain_events() {
    std::vector<CombatEvent> drained;
    drained.swap(impl_->events);
    return drained;
}

void Combat::reset() noexcept {
    impl_->pending.clear();
    impl_->events.clear();
    impl_->weapons.clear();
    impl_->aim_directions.clear();
    impl_->dodges.clear();
    impl_->held_fire.clear();
    impl_->snapshot = {};
    impl_->next_sequence = 1;
    impl_->next_projectile_id = 1;
}

} // namespace ic2d
