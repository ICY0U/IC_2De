#include <doctest/doctest.h>

#include "ic2d/combat.hpp"

#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace {

[[nodiscard]] std::vector<const ic2d::CombatIntentEvent*>
intent_events(const std::vector<ic2d::CombatEvent>& events) {
    std::vector<const ic2d::CombatIntentEvent*> result;
    for (const ic2d::CombatEvent& event : events) {
        if (const auto* intent = std::get_if<ic2d::CombatIntentEvent>(&event)) {
            result.push_back(intent);
        }
    }
    return result;
}

[[nodiscard]] std::vector<const ic2d::ProjectileSpawnedEvent*>
projectile_events(const std::vector<ic2d::CombatEvent>& events) {
    std::vector<const ic2d::ProjectileSpawnedEvent*> result;
    for (const ic2d::CombatEvent& event : events) {
        if (const auto* projectile = std::get_if<ic2d::ProjectileSpawnedEvent>(&event)) {
            result.push_back(projectile);
        }
    }
    return result;
}

[[nodiscard]] std::vector<const ic2d::DodgeStartedEvent*>
dodge_started_events(const std::vector<ic2d::CombatEvent>& events) {
    std::vector<const ic2d::DodgeStartedEvent*> result;
    for (const ic2d::CombatEvent& event : events) {
        if (const auto* dodge = std::get_if<ic2d::DodgeStartedEvent>(&event)) {
            result.push_back(dodge);
        }
    }
    return result;
}

TEST_CASE("fire is buffered until the next fixed tick") {
    ic2d::Combat combat;
    ic2d::CombatCommand command;
    command.actor = {42};
    command.aim_direction = ic2d::Vec2{1.0F, 0.0F};
    command.request(ic2d::CombatIntent::fire);

    CHECK_MESSAGE((combat.submit(command)), "A valid fire command must enter the buffer.");
    const ic2d::CombatSnapshot pending = combat.snapshot();
    CHECK_MESSAGE((pending.tick == 0 && pending.pending_command_count == 1),
                  "Submitting during a render frame must not advance authoritative combat state.");
    CHECK_MESSAGE((combat.drain_events().empty()),
                  "Buffered input must emit no event before a fixed tick consumes it.");

    combat.fixed_update(1);
    const std::vector<ic2d::CombatEvent> events = combat.drain_events();
    const auto intents = intent_events(events);
    CHECK_MESSAGE((intents.size() == 1),
                  "One buffered fire edge must emit exactly one intent acknowledgement.");
    if (intents.size() == 1) {
        const auto& event = *intents.front();
        CHECK_MESSAGE((event.tick == 1 && event.sequence == 1),
                      "The first applied intent must carry tick one and sequence one.");
        CHECK_MESSAGE(
            (event.actor == ic2d::EntityUuid{42} && event.intent == ic2d::CombatIntent::fire),
            "The copied event must preserve actor identity and logical intent.");
        CHECK_MESSAGE((event.aim_direction.x == 1.0F && event.aim_direction.y == 0.0F),
                      "The copied event must carry the resolved X/Z aim direction.");
    }

    const ic2d::CombatSnapshot applied = combat.snapshot();
    CHECK_MESSAGE((applied.tick == 1 && applied.pending_command_count == 0 &&
                   applied.consumed_command_count == 1 && applied.emitted_intent_count == 1),
                  "The snapshot must report authoritative consumption without retaining the edge.");
    CHECK_MESSAGE((combat.drain_events().empty()), "Draining copied events must be destructive.");
}

TEST_CASE("commands keep fifo order and normalize aim") {
    ic2d::Combat combat;
    ic2d::CombatCommand first;
    first.actor = {7};
    first.aim_direction = ic2d::Vec2{3.0F, 4.0F};
    first.request(ic2d::CombatIntent::reload);
    first.request(ic2d::CombatIntent::fire);
    ic2d::CombatCommand second;
    second.actor = {7};
    second.aim_direction = ic2d::Vec2{-1.0F, 0.0F};
    second.request(ic2d::CombatIntent::dodge);

    CHECK_MESSAGE((combat.submit(first) && combat.submit(second)),
                  "Multiple valid render-frame commands must enter one fixed-tick batch.");
    combat.fixed_update(1);
    const std::vector<ic2d::CombatEvent> events = combat.drain_events();
    const auto intents = intent_events(events);
    CHECK_MESSAGE((intents.size() == 3),
                  "Every buffered edge must survive until the authoritative tick.");
    if (intents.size() == 3) {
        const auto& fire = *intents[0];
        const auto& reload = *intents[1];
        const auto& dodge = *intents[2];
        CHECK_MESSAGE((fire.intent == ic2d::CombatIntent::fire &&
                       reload.intent == ic2d::CombatIntent::reload &&
                       dodge.intent == ic2d::CombatIntent::dodge),
                      "Commands must remain FIFO with same-command intents in stable enum order.");
        CHECK_MESSAGE((fire.sequence == 1 && reload.sequence == 2 && dodge.sequence == 3),
                      "Copied intent sequences must be contiguous across one batch.");
        CHECK_MESSAGE((fire.aim_direction.x == 0.6F && fire.aim_direction.y == 0.8F),
                      "Combat must normalize over-range aim without changing its direction.");
        CHECK_MESSAGE((dodge.aim_direction.x == -1.0F && dodge.aim_direction.y == 0.0F),
                      "Each command's events must observe the latest aim preceding them.");
    }
    const ic2d::CombatSnapshot snapshot = combat.snapshot();
    CHECK_MESSAGE((snapshot.consumed_command_count == 2 && snapshot.emitted_intent_count == 3),
                  "The snapshot must count commands separately from emitted intents.");
    CHECK_MESSAGE((snapshot.aim_direction.x == -1.0F && snapshot.aim_direction.y == 0.0F),
                  "The snapshot must retain the batch's latest normalized aim.");
}

TEST_CASE("invalid ticks do not consume commands and reset is clean") {
    ic2d::Combat combat;
    ic2d::CombatCommand initial;
    initial.actor = {9};
    initial.request(ic2d::CombatIntent::fire);
    CHECK_MESSAGE((combat.submit(initial)), "A command may rely on the retained default aim.");
    combat.fixed_update(1);
    static_cast<void>(combat.drain_events());

    ic2d::CombatCommand pending;
    pending.actor = {9};
    pending.request(ic2d::CombatIntent::reload);
    CHECK_MESSAGE((combat.submit(pending)), "A second-tick command must enter the buffer.");

    bool skipped_tick_rejected = false;
    try {
        combat.fixed_update(3);
    } catch (const std::invalid_argument&) {
        skipped_tick_rejected = true;
    }
    CHECK_MESSAGE((skipped_tick_rejected), "Combat must reject a skipped authoritative tick.");
    CHECK_MESSAGE((combat.snapshot().tick == 1 && combat.snapshot().pending_command_count == 1),
                  "A rejected tick must leave both authoritative and buffered state untouched.");

    combat.fixed_update(2);
    const std::vector<ic2d::CombatEvent> second_tick = combat.drain_events();
    const auto second_tick_intents = intent_events(second_tick);
    CHECK_MESSAGE((second_tick_intents.size() == 1 && second_tick_intents.front()->tick == 2 &&
                   second_tick_intents.front()->sequence == 3),
                  "The valid next tick must consume the preserved command exactly once.");

    ic2d::CombatCommand invalid_aim;
    invalid_aim.actor = {9};
    invalid_aim.aim_direction = ic2d::Vec2{std::numeric_limits<float>::infinity(), 0.0F};
    CHECK_MESSAGE((!combat.submit(invalid_aim)), "Non-finite aim must be rejected at submission.");

    combat.reset();
    const ic2d::CombatSnapshot reset = combat.snapshot();
    CHECK_MESSAGE((reset.tick == 0 && reset.consumed_command_count == 0 &&
                   reset.emitted_intent_count == 0 && reset.pending_command_count == 0),
                  "Reset must clear authoritative counts and buffered work.");
    CHECK_MESSAGE((reset.aim_direction.x == 0.0F && reset.aim_direction.y == 1.0F),
                  "Reset must restore the deterministic default aim.");

    CHECK_MESSAGE((combat.submit(initial)), "The reset Combat module must accept a fresh command.");
    combat.fixed_update(1);
    const auto restarted = combat.drain_events();
    const auto restarted_intents = intent_events(restarted);
    CHECK_MESSAGE(
        (restarted_intents.size() == 1 && restarted_intents.front()->sequence == 1),
        "Reset must restart tick and event identity from the initial deterministic state.");
}

TEST_CASE("empty identity and zero aim commands are rejected") {
    ic2d::Combat combat;
    ic2d::CombatCommand empty;
    empty.actor = {4};
    CHECK_MESSAGE((!combat.submit(empty)),
                  "A command with no aim update or intent must be rejected.");

    ic2d::CombatCommand missing_actor;
    missing_actor.request(ic2d::CombatIntent::dodge);
    CHECK_MESSAGE((!combat.submit(missing_actor)),
                  "A command without stable actor identity must be rejected.");

    ic2d::CombatCommand zero_aim;
    zero_aim.actor = {4};
    zero_aim.aim_direction = ic2d::Vec2{};
    CHECK_MESSAGE((!combat.submit(zero_aim)),
                  "An explicit aim update must contain a usable non-zero direction.");
    CHECK_MESSAGE((combat.snapshot().pending_command_count == 0),
                  "Rejected commands must never alter the pending snapshot.");
}

TEST_CASE("render rate aim updates coalesce before the fixed tick") {
    ic2d::Combat combat;

    for (const ic2d::Vec2 aim : {
             ic2d::Vec2{1.0F, 0.0F},
             ic2d::Vec2{0.0F, 1.0F},
             ic2d::Vec2{-1.0F, 0.0F},
         }) {
        ic2d::CombatCommand command;
        command.actor = {12};
        command.aim_direction = aim;
        CHECK_MESSAGE((combat.submit(command)),
                      "Each valid render-rate aim update must be accepted.");
    }

    CHECK_MESSAGE(
        (combat.snapshot().pending_command_count == 1),
        "Consecutive aim-only updates for one actor must coalesce to one pending command.");
    combat.fixed_update(1);

    const ic2d::CombatSnapshot snapshot = combat.snapshot();
    CHECK_MESSAGE((snapshot.consumed_command_count == 1),
                  "The fixed tick must consume only the newest coalesced aim command.");
    CHECK_MESSAGE((snapshot.aim_direction.x == -1.0F && snapshot.aim_direction.y == 0.0F),
                  "Coalescing must preserve the most recent render-rate aim direction.");
    CHECK_MESSAGE((combat.drain_events().empty()),
                  "An aim-only command must update state without inventing a combat intent event.");
}

TEST_CASE("needle pistol fire consumes ammo and emits spawn event") {
    ic2d::Combat combat;
    ic2d::CombatCommand fire;
    fire.actor = {73};
    fire.aim_direction = ic2d::Vec2{1.0F, 0.0F};
    fire.request(ic2d::CombatIntent::fire);

    CHECK_MESSAGE((combat.submit(fire)), "A needle-pistol shot must enter the command buffer.");
    combat.fixed_update(1);
    const std::vector<ic2d::CombatEvent> events = combat.drain_events();

    const ic2d::ProjectileSpawnedEvent* projectile = nullptr;
    for (const ic2d::CombatEvent& event : events) {
        if (const auto* candidate = std::get_if<ic2d::ProjectileSpawnedEvent>(&event)) {
            projectile = candidate;
        }
    }
    CHECK_MESSAGE((projectile != nullptr),
                  "An eligible fire intent must emit one copied projectile-spawn event.");
    if (projectile != nullptr) {
        CHECK_MESSAGE(
            (projectile->tick == 1 && projectile->sequence == 2 && projectile->projectile_id == 1),
            "The first projectile must carry stable tick, event, and projectile identity.");
        CHECK_MESSAGE((projectile->actor == ic2d::EntityUuid{73} &&
                       projectile->weapon == ic2d::WeaponKind::needle_pistol),
                      "The spawn event must identify its owner and weapon family.");
        CHECK_MESSAGE(
            (projectile->aim_direction.x == 1.0F && projectile->aim_direction.y == 0.0F &&
             projectile->speed == ic2d::needle_pistol.projectile_speed &&
             projectile->lifetime_ticks == ic2d::needle_pistol.projectile_lifetime_ticks &&
             projectile->damage == ic2d::needle_pistol.projectile_damage),
            "The spawn event must copy every parameter needed by the projectile module.");
    }

    const ic2d::CombatSnapshot snapshot = combat.snapshot();
    CHECK_MESSAGE(
        (snapshot.weapon.magazine_ammo == ic2d::needle_pistol.magazine_capacity - 1 &&
         snapshot.weapon.reserve_ammo == ic2d::needle_pistol.initial_reserve_ammo),
        "A valid shot must consume exactly one magazine round and no reserve ammunition.");
    CHECK_MESSAGE((snapshot.spawned_projectile_count == 1),
                  "The authoritative snapshot must count successful projectile spawns.");
}

// A full-weapon refill, which is also what an unlimited-ammo development run
// is made of: the same call repeated every tick.
TEST_CASE("replenish fills the weapon and abandons a reload") {
    ic2d::Combat combat;
    constexpr ic2d::EntityUuid actor{73};
    const auto submit = [&combat](const ic2d::CombatIntent intent) {
        ic2d::CombatCommand command;
        command.actor = actor;
        command.aim_direction = ic2d::Vec2{1.0F, 0.0F};
        command.request(intent);
        CHECK_MESSAGE((combat.submit(command)), "Setup command must be accepted.");
    };

    submit(ic2d::CombatIntent::fire);
    combat.fixed_update(1);
    submit(ic2d::CombatIntent::reload);
    combat.fixed_update(2);
    CHECK_MESSAGE((combat.snapshot().weapon.reloading), "Setup must leave a reload running.");

    CHECK_MESSAGE((combat.replenish(actor)), "Replenishing a valid actor must be accepted.");
    const ic2d::CombatSnapshot filled = combat.snapshot();
    CHECK_MESSAGE((filled.weapon.magazine_ammo == ic2d::needle_pistol.magazine_capacity),
                  "Replenish must fill the magazine to capacity.");
    CHECK_MESSAGE((filled.weapon.reserve_ammo == ic2d::needle_pistol.maximum_reserve_ammo),
                  "Replenish must fill the reserve to its ceiling.");
    CHECK_MESSAGE((!filled.weapon.reloading && filled.weapon.reload_ticks_remaining == 0),
                  "Replenish must abandon a reload, because there is nothing left to reload into.");
    CHECK_MESSAGE(
        (!filled.actors.empty() &&
         filled.actors.front().weapon.magazine_ammo == ic2d::needle_pistol.magazine_capacity),
        "Replenish must republish the per-actor snapshot, not only the latest-actor view.");

    // Applied outside a tick, so it must not disturb tick ordering. The shot
    // below lands long before the abandoned reload would have completed, which
    // is the whole point: the weapon is ready, not still reloading.
    std::uint64_t tick = 3;
    for (; tick <= ic2d::needle_pistol.fire_cooldown_ticks + 2; ++tick) {
        combat.fixed_update(tick);
    }
    CHECK_MESSAGE((tick < ic2d::needle_pistol.reload_duration_ticks),
                  "The shot must be taken while the abandoned reload would still be running.");
    submit(ic2d::CombatIntent::fire);
    combat.fixed_update(tick);
    CHECK_MESSAGE(
        (combat.snapshot().weapon.magazine_ammo == ic2d::needle_pistol.magazine_capacity - 1),
        "A replenished weapon must fire rather than finish the reload it abandoned.");

    CHECK_MESSAGE((!combat.replenish({})), "A zero identity must be refused.");
}

TEST_CASE("needle pistol fire cooldown is fixed tick authoritative") {
    ic2d::Combat combat;
    const auto submit_fire = [&combat]() {
        ic2d::CombatCommand fire;
        fire.actor = {81};
        fire.request(ic2d::CombatIntent::fire);
        CHECK_MESSAGE((combat.submit(fire)),
                      "Every render-frame fire edge must remain acknowledged.");
    };

    submit_fire();
    combat.fixed_update(1);
    CHECK_MESSAGE((projectile_events(combat.drain_events()).size() == 1),
                  "The first ready fire command must spawn a projectile.");
    CHECK_MESSAGE((combat.snapshot().weapon.fire_cooldown_ticks_remaining ==
                   ic2d::needle_pistol.fire_cooldown_ticks),
                  "A shot must arm the complete fixed-tick cooldown.");

    for (std::uint64_t tick = 2; tick <= ic2d::needle_pistol.fire_cooldown_ticks; ++tick) {
        submit_fire();
        combat.fixed_update(tick);
        CHECK_MESSAGE((projectile_events(combat.drain_events()).empty()),
                      "A fire command inside cooldown must not spawn or consume ammunition.");
    }

    const std::uint64_t ready_tick = ic2d::needle_pistol.fire_cooldown_ticks + 1;
    submit_fire();
    combat.fixed_update(ready_tick);
    CHECK_MESSAGE((projectile_events(combat.drain_events()).size() == 1),
                  "The first tick after cooldown expiry must permit the next shot.");
    CHECK_MESSAGE(
        (combat.snapshot().weapon.magazine_ammo == ic2d::needle_pistol.magazine_capacity - 2),
        "Blocked fire commands must not consume magazine ammunition.");
    CHECK_MESSAGE((combat.snapshot().spawned_projectile_count == 2),
                  "Only cooldown-eligible commands may increase the projectile count.");
}

TEST_CASE("needle pistol reload has exact duration and blocks fire") {
    ic2d::Combat combat;
    const ic2d::EntityUuid actor{96};
    const auto submit = [&combat, actor](const ic2d::CombatIntent intent) {
        ic2d::CombatCommand command;
        command.actor = actor;
        command.request(intent);
        CHECK_MESSAGE((combat.submit(command)),
                      "A weapon action must enter the fixed-tick buffer.");
    };

    submit(ic2d::CombatIntent::fire);
    combat.fixed_update(1);
    static_cast<void>(combat.drain_events());
    CHECK_MESSAGE(
        (combat.snapshot().weapon.magazine_ammo == ic2d::needle_pistol.magazine_capacity - 1),
        "The reload fixture must begin with exactly one missing round.");

    submit(ic2d::CombatIntent::reload);
    combat.fixed_update(2);
    static_cast<void>(combat.drain_events());
    CHECK_MESSAGE(
        (combat.snapshot().weapon.reloading && combat.snapshot().weapon.reload_ticks_remaining ==
                                                   ic2d::needle_pistol.reload_duration_ticks),
        "Reload must start on its command tick with the complete duration remaining.");

    for (std::uint64_t tick = 3; tick < ic2d::needle_pistol.fire_cooldown_ticks + 2; ++tick) {
        combat.fixed_update(tick);
        static_cast<void>(combat.drain_events());
    }
    const std::uint64_t blocked_fire_tick = ic2d::needle_pistol.fire_cooldown_ticks + 2;
    submit(ic2d::CombatIntent::fire);
    combat.fixed_update(blocked_fire_tick);
    CHECK_MESSAGE((projectile_events(combat.drain_events()).empty()),
                  "Reload must block firing even after the ordinary fire cooldown expires.");
    CHECK_MESSAGE(
        (combat.snapshot().weapon.magazine_ammo == ic2d::needle_pistol.magazine_capacity - 1),
        "A fire attempt during reload must not consume ammunition.");

    const std::uint64_t completion_tick = 2 + ic2d::needle_pistol.reload_duration_ticks;
    for (std::uint64_t tick = blocked_fire_tick + 1; tick < completion_tick; ++tick) {
        combat.fixed_update(tick);
        static_cast<void>(combat.drain_events());
    }
    CHECK_MESSAGE((combat.snapshot().weapon.reloading &&
                   combat.snapshot().weapon.reload_ticks_remaining == 1),
                  "Reload must remain active through the tick immediately before completion.");

    combat.fixed_update(completion_tick);
    CHECK_MESSAGE((!combat.snapshot().weapon.reloading &&
                   combat.snapshot().weapon.reload_ticks_remaining == 0),
                  "Reload must complete exactly on start tick plus authored duration.");
    CHECK_MESSAGE(
        (combat.snapshot().weapon.magazine_ammo == ic2d::needle_pistol.magazine_capacity &&
         combat.snapshot().weapon.reserve_ammo == ic2d::needle_pistol.initial_reserve_ammo - 1),
        "Reload must transfer only the missing rounds from reserve into the magazine.");

    submit(ic2d::CombatIntent::fire);
    combat.fixed_update(completion_tick + 1);
    CHECK_MESSAGE((projectile_events(combat.drain_events()).size() == 1),
                  "Firing must resume on the fixed tick after reload completion.");
}

TEST_CASE("held fire repeats on fixed cooldown until release") {
    ic2d::Combat combat;
    ic2d::CombatCommand hold;
    hold.actor = {120};
    hold.aim_direction = ic2d::Vec2{1.0F, 0.0F};
    hold.set_fire_held(true);
    CHECK_MESSAGE((combat.submit(hold)),
                  "A held-fire state change must enter the fixed-tick command buffer.");

    combat.fixed_update(1);
    CHECK_MESSAGE((projectile_events(combat.drain_events()).size() == 1),
                  "Pressing and holding fire must shoot on the first authoritative tick.");

    std::size_t repeated_shots = 0;
    for (std::uint64_t tick = 2; tick <= 9; ++tick) {
        combat.fixed_update(tick);
        repeated_shots += projectile_events(combat.drain_events()).size();
    }
    CHECK_MESSAGE(
        (repeated_shots == 1 && combat.snapshot().spawned_projectile_count == 2),
        "Holding fire must shoot again on the exact ready tick without another input edge.");

    ic2d::CombatCommand release;
    release.actor = {120};
    release.set_fire_held(false);
    CHECK_MESSAGE((combat.submit(release)), "A held-fire release must enter the command buffer.");
    for (std::uint64_t tick = 10; tick <= 18; ++tick) {
        combat.fixed_update(tick);
        CHECK_MESSAGE((projectile_events(combat.drain_events()).empty()),
                      "Releasing LMB must stop all later automatic shots.");
    }
    CHECK_MESSAGE((combat.snapshot().spawned_projectile_count == 2),
                  "Release must leave the automatic-fire count unchanged.");
}

TEST_CASE("mouse aim update cannot overwrite a held fire release") {
    ic2d::Combat combat;
    const ic2d::EntityUuid actor{121};

    ic2d::CombatCommand hold;
    hold.actor = actor;
    hold.aim_direction = ic2d::Vec2{1.0F, 0.0F};
    hold.set_fire_held(true);
    CHECK_MESSAGE((combat.submit(hold)), "The mouse-release fixture must accept held fire.");
    combat.fixed_update(1);
    static_cast<void>(combat.drain_events());

    ic2d::CombatCommand release;
    release.actor = actor;
    release.aim_direction = ic2d::Vec2{0.9F, 0.1F};
    release.set_fire_held(false);
    ic2d::CombatCommand later_mouse_motion;
    later_mouse_motion.actor = actor;
    later_mouse_motion.aim_direction = ic2d::Vec2{0.8F, 0.2F};
    CHECK_MESSAGE((combat.submit(release) && combat.submit(later_mouse_motion)),
                  "LMB release and later mouse motion must both enter the render-frame buffer.");

    for (std::uint64_t tick = 2; tick <= 9; ++tick) {
        combat.fixed_update(tick);
        CHECK_MESSAGE((projectile_events(combat.drain_events()).empty()),
                      "Mouse motion after LMB release must not leave automatic fire latched.");
    }
    CHECK_MESSAGE((combat.snapshot().spawned_projectile_count == 1),
                  "Releasing while aiming must preserve the original one-shot count.");
}

TEST_CASE("dodge starts on a fixed tick and has an exact invulnerability window") {
    ic2d::Combat combat;
    const ic2d::EntityUuid actor{207};
    ic2d::CombatCommand dodge;
    dodge.actor = actor;
    dodge.dodge_direction = ic2d::Vec2{3.0F, 4.0F};
    dodge.request(ic2d::CombatIntent::dodge);

    CHECK_MESSAGE((combat.submit(dodge)), "A valid dodge request must enter the command buffer.");
    CHECK_MESSAGE((!combat.invulnerable(actor) && !combat.snapshot().dodge.active),
                  "A render-frame dodge request must not change authoritative state early.");

    combat.fixed_update(1);
    const std::vector<ic2d::CombatEvent> events = combat.drain_events();
    const auto starts = dodge_started_events(events);
    CHECK_MESSAGE((starts.size() == 1), "The ready dodge request must emit one start event.");
    if (starts.size() == 1) {
        CHECK_MESSAGE((starts.front()->tick == 1 && starts.front()->actor == actor),
                      "The copied dodge event must identify its authoritative tick and actor.");
        CHECK_MESSAGE(
            (starts.front()->duration_ticks == ic2d::player_dodge.duration_ticks &&
             starts.front()->invulnerability_ticks == ic2d::player_dodge.invulnerability_ticks &&
             starts.front()->cooldown_ticks == ic2d::player_dodge.cooldown_ticks),
            "The dodge event must copy every authored timing value.");
        CHECK_MESSAGE(
            (starts.front()->direction.x == 0.6F && starts.front()->direction.y == 0.8F),
            "The dodge event must freeze the normalized movement direction from its start tick.");
    }

    ic2d::DodgeSnapshot snapshot = combat.snapshot().dodge;
    CHECK_MESSAGE((snapshot.active && snapshot.invulnerable && combat.invulnerable(actor)),
                  "A started dodge must be active and queryable as invulnerable.");
    CHECK_MESSAGE(
        (snapshot.active_ticks_remaining == ic2d::player_dodge.duration_ticks &&
         snapshot.invulnerable_ticks_remaining == ic2d::player_dodge.invulnerability_ticks &&
         snapshot.cooldown_ticks_remaining == ic2d::player_dodge.cooldown_ticks &&
         snapshot.started_count == 1),
        "The start tick must expose the complete authored dodge timers.");
    CHECK_MESSAGE((snapshot.direction.x == 0.6F && snapshot.direction.y == 0.8F),
                  "The active dodge snapshot must expose its frozen world direction.");

    for (std::uint64_t tick = 2; tick <= ic2d::player_dodge.invulnerability_ticks; ++tick) {
        combat.fixed_update(tick);
        static_cast<void>(combat.drain_events());
        CHECK_MESSAGE((combat.invulnerable(actor)),
                      "Invulnerability must remain true through its final authored tick.");
    }
    combat.fixed_update(ic2d::player_dodge.invulnerability_ticks + 1);
    static_cast<void>(combat.drain_events());
    CHECK_MESSAGE((!combat.invulnerable(actor)),
                  "Invulnerability must end exactly after its authored fixed-tick window.");

    for (std::uint64_t tick = ic2d::player_dodge.invulnerability_ticks + 2;
         tick <= ic2d::player_dodge.duration_ticks; ++tick) {
        combat.fixed_update(tick);
        static_cast<void>(combat.drain_events());
    }
    CHECK_MESSAGE((combat.snapshot().dodge.active),
                  "Dodge activity must remain true through the complete duration.");
    combat.fixed_update(ic2d::player_dodge.duration_ticks + 1);
    static_cast<void>(combat.drain_events());
    CHECK_MESSAGE((!combat.snapshot().dodge.active),
                  "Dodge activity must end exactly after its authored duration.");
}

TEST_CASE("dodge direction stays frozen during cooldown") {
    ic2d::Combat combat;
    const ic2d::EntityUuid actor{209};

    ic2d::CombatCommand first;
    first.actor = actor;
    first.dodge_direction = ic2d::Vec2{0.0F, -2.0F};
    first.request(ic2d::CombatIntent::dodge);
    CHECK_MESSAGE((combat.submit(first)),
                  "The first directional dodge must enter the command buffer.");
    combat.fixed_update(1);
    static_cast<void>(combat.drain_events());

    ic2d::CombatCommand rejected_restart;
    rejected_restart.actor = actor;
    rejected_restart.dodge_direction = ic2d::Vec2{1.0F, 0.0F};
    rejected_restart.request(ic2d::CombatIntent::dodge);
    CHECK_MESSAGE((combat.submit(rejected_restart)),
                  "A cooldown dodge still reaches Combat for authoritative rejection.");
    combat.fixed_update(2);
    const std::vector<ic2d::CombatEvent> events = combat.drain_events();

    CHECK_MESSAGE((dodge_started_events(events).empty()),
                  "A cooldown request must not emit another dodge start.");
    const ic2d::DodgeSnapshot snapshot = combat.snapshot().dodge;
    CHECK_MESSAGE((snapshot.direction.x == 0.0F && snapshot.direction.y == -1.0F),
                  "A rejected restart must not redirect the active dodge.");
}

TEST_CASE("combat snapshot copies all actor state in stable identity order") {
    ic2d::Combat combat;

    ic2d::CombatCommand higher_actor;
    higher_actor.actor = {902};
    higher_actor.aim_direction = ic2d::Vec2{1.0F, 0.0F};
    higher_actor.set_fire_held(true);
    CHECK_MESSAGE((combat.submit(higher_actor)),
                  "The higher actor fixture must enter Combat first.");

    ic2d::CombatCommand lower_actor;
    lower_actor.actor = {901};
    lower_actor.aim_direction = ic2d::Vec2{0.0F, -1.0F};
    lower_actor.dodge_direction = ic2d::Vec2{-1.0F, 0.0F};
    lower_actor.request(ic2d::CombatIntent::dodge);
    CHECK_MESSAGE((combat.submit(lower_actor)),
                  "The lower actor fixture must enter Combat second.");

    combat.fixed_update(1);
    static_cast<void>(combat.drain_events());
    const ic2d::CombatSnapshot snapshot = combat.snapshot();

    CHECK_MESSAGE((snapshot.actors.size() == 2),
                  "A copied Combat snapshot must describe every actor with authoritative state.");
    if (snapshot.actors.size() == 2) {
        CHECK_MESSAGE(
            (snapshot.actors[0].actor == ic2d::EntityUuid{901} &&
             snapshot.actors[1].actor == ic2d::EntityUuid{902}),
            "Combat actor snapshots must use stable UUID order, not command arrival order.");
        CHECK_MESSAGE((snapshot.actors[0].dodge.active &&
                       snapshot.actors[0].dodge.direction.x == -1.0F &&
                       !snapshot.actors[0].fire_held),
                      "The lower actor snapshot must copy its frozen dodge and held-fire state.");
        CHECK_MESSAGE((snapshot.actors[1].aim_direction.x == 1.0F && snapshot.actors[1].fire_held),
                      "The higher actor snapshot must copy its retained aim and held-fire state.");
    }
    CHECK_MESSAGE(
        (snapshot.next_event_sequence > 1 &&
         snapshot.next_projectile_id == snapshot.spawned_projectile_count + 1),
        "The snapshot must copy the next identities that influence later deterministic results.");
}

TEST_CASE("dodge cooldown rejects restarts and reset clears state") {
    ic2d::Combat combat;
    const ic2d::EntityUuid actor{208};
    const auto submit_dodge = [&combat, actor]() {
        ic2d::CombatCommand command;
        command.actor = actor;
        command.request(ic2d::CombatIntent::dodge);
        CHECK_MESSAGE((combat.submit(command)),
                      "Every dodge edge must remain acknowledged by Combat.");
    };

    submit_dodge();
    combat.fixed_update(1);
    static_cast<void>(combat.drain_events());

    const std::uint64_t ready_tick = 1 + ic2d::player_dodge.cooldown_ticks;
    for (std::uint64_t tick = 2; tick < ready_tick; ++tick) {
        if (tick == 2 || tick == ic2d::player_dodge.duration_ticks + 2 || tick == ready_tick - 1) {
            submit_dodge();
        }
        combat.fixed_update(tick);
        const std::vector<ic2d::CombatEvent> events = combat.drain_events();
        CHECK_MESSAGE((dodge_started_events(events).empty()),
                      "A dodge request inside cooldown must not restart the dodge.");
        CHECK_MESSAGE((combat.snapshot().dodge.started_count == 1),
                      "Rejected dodge requests must leave the successful-start count unchanged.");
    }

    CHECK_MESSAGE((combat.snapshot().dodge.cooldown_ticks_remaining == 1),
                  "Cooldown must retain one tick immediately before readiness.");
    submit_dodge();
    combat.fixed_update(ready_tick);
    const std::vector<ic2d::CombatEvent> ready_events = combat.drain_events();
    CHECK_MESSAGE((dodge_started_events(ready_events).size() == 1),
                  "A dodge must start again on the exact cooldown-ready tick.");
    CHECK_MESSAGE((combat.snapshot().dodge.started_count == 2 && combat.invulnerable(actor)),
                  "The second eligible dodge must re-arm state exactly once.");

    combat.reset();
    const ic2d::DodgeSnapshot reset = combat.snapshot().dodge;
    CHECK_MESSAGE((!reset.active && !reset.invulnerable && reset.active_ticks_remaining == 0 &&
                   reset.invulnerable_ticks_remaining == 0 && reset.cooldown_ticks_remaining == 0 &&
                   reset.started_count == 0 && !combat.invulnerable(actor)),
                  "Reset must clear dodge activity, invulnerability, cooldown, and counters.");
}

} // namespace
