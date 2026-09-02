#include "ic2d/combat.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::vector<const ic2d::CombatIntentEvent*> intent_events(
    const std::vector<ic2d::CombatEvent>& events
) {
    std::vector<const ic2d::CombatIntentEvent*> result;
    for (const ic2d::CombatEvent& event : events) {
        if (const auto* intent = std::get_if<ic2d::CombatIntentEvent>(&event)) {
            result.push_back(intent);
        }
    }
    return result;
}

[[nodiscard]] std::vector<const ic2d::ProjectileSpawnedEvent*> projectile_events(
    const std::vector<ic2d::CombatEvent>& events
) {
    std::vector<const ic2d::ProjectileSpawnedEvent*> result;
    for (const ic2d::CombatEvent& event : events) {
        if (const auto* projectile = std::get_if<ic2d::ProjectileSpawnedEvent>(&event)) {
            result.push_back(projectile);
        }
    }
    return result;
}

[[nodiscard]] std::vector<const ic2d::DodgeStartedEvent*> dodge_started_events(
    const std::vector<ic2d::CombatEvent>& events
) {
    std::vector<const ic2d::DodgeStartedEvent*> result;
    for (const ic2d::CombatEvent& event : events) {
        if (const auto* dodge = std::get_if<ic2d::DodgeStartedEvent>(&event)) {
            result.push_back(dodge);
        }
    }
    return result;
}

void test_fire_is_buffered_until_the_next_fixed_tick() {
    ic2d::Combat combat;
    ic2d::CombatCommand command;
    command.actor = {42};
    command.aim_direction = ic2d::Vec2{1.0F, 0.0F};
    command.request(ic2d::CombatIntent::fire);

    expect(combat.submit(command), "A valid fire command must enter the buffer.");
    const ic2d::CombatSnapshot pending = combat.snapshot();
    expect(pending.tick == 0 && pending.pending_command_count == 1,
           "Submitting during a render frame must not advance authoritative combat state.");
    expect(combat.drain_events().empty(),
           "Buffered input must emit no event before a fixed tick consumes it.");

    combat.fixed_update(1);
    const std::vector<ic2d::CombatEvent> events = combat.drain_events();
    const auto intents = intent_events(events);
    expect(intents.size() == 1, "One buffered fire edge must emit exactly one intent acknowledgement.");
    if (intents.size() == 1) {
        const auto& event = *intents.front();
        expect(event.tick == 1 && event.sequence == 1,
               "The first applied intent must carry tick one and sequence one.");
        expect(event.actor == ic2d::EntityUuid{42} && event.intent == ic2d::CombatIntent::fire,
               "The copied event must preserve actor identity and logical intent.");
        expect(event.aim_direction.x == 1.0F && event.aim_direction.y == 0.0F,
               "The copied event must carry the resolved X/Z aim direction.");
    }

    const ic2d::CombatSnapshot applied = combat.snapshot();
    expect(applied.tick == 1 && applied.pending_command_count == 0 &&
               applied.consumed_command_count == 1 && applied.emitted_intent_count == 1,
           "The snapshot must report authoritative consumption without retaining the edge.");
    expect(combat.drain_events().empty(), "Draining copied events must be destructive.");
}

void test_commands_keep_fifo_order_and_normalize_aim() {
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

    expect(combat.submit(first) && combat.submit(second),
           "Multiple valid render-frame commands must enter one fixed-tick batch.");
    combat.fixed_update(1);
    const std::vector<ic2d::CombatEvent> events = combat.drain_events();
    const auto intents = intent_events(events);
    expect(intents.size() == 3,
           "Every buffered edge must survive until the authoritative tick.");
    if (intents.size() == 3) {
        const auto& fire = *intents[0];
        const auto& reload = *intents[1];
        const auto& dodge = *intents[2];
        expect(fire.intent == ic2d::CombatIntent::fire &&
                   reload.intent == ic2d::CombatIntent::reload &&
                   dodge.intent == ic2d::CombatIntent::dodge,
               "Commands must remain FIFO with same-command intents in stable enum order.");
        expect(fire.sequence == 1 && reload.sequence == 2 && dodge.sequence == 3,
               "Copied intent sequences must be contiguous across one batch.");
        expect(fire.aim_direction.x == 0.6F && fire.aim_direction.y == 0.8F,
               "Combat must normalize over-range aim without changing its direction.");
        expect(dodge.aim_direction.x == -1.0F && dodge.aim_direction.y == 0.0F,
               "Each command's events must observe the latest aim preceding them.");
    }
    const ic2d::CombatSnapshot snapshot = combat.snapshot();
    expect(snapshot.consumed_command_count == 2 && snapshot.emitted_intent_count == 3,
           "The snapshot must count commands separately from emitted intents.");
    expect(snapshot.aim_direction.x == -1.0F && snapshot.aim_direction.y == 0.0F,
           "The snapshot must retain the batch's latest normalized aim.");
}

void test_invalid_ticks_do_not_consume_commands_and_reset_is_clean() {
    ic2d::Combat combat;
    ic2d::CombatCommand initial;
    initial.actor = {9};
    initial.request(ic2d::CombatIntent::fire);
    expect(combat.submit(initial), "A command may rely on the retained default aim.");
    combat.fixed_update(1);
    static_cast<void>(combat.drain_events());

    ic2d::CombatCommand pending;
    pending.actor = {9};
    pending.request(ic2d::CombatIntent::reload);
    expect(combat.submit(pending), "A second-tick command must enter the buffer.");

    bool skipped_tick_rejected = false;
    try {
        combat.fixed_update(3);
    } catch (const std::invalid_argument&) {
        skipped_tick_rejected = true;
    }
    expect(skipped_tick_rejected, "Combat must reject a skipped authoritative tick.");
    expect(combat.snapshot().tick == 1 && combat.snapshot().pending_command_count == 1,
           "A rejected tick must leave both authoritative and buffered state untouched.");

    combat.fixed_update(2);
    const std::vector<ic2d::CombatEvent> second_tick = combat.drain_events();
    const auto second_tick_intents = intent_events(second_tick);
    expect(second_tick_intents.size() == 1 && second_tick_intents.front()->tick == 2 &&
               second_tick_intents.front()->sequence == 3,
           "The valid next tick must consume the preserved command exactly once.");

    ic2d::CombatCommand invalid_aim;
    invalid_aim.actor = {9};
    invalid_aim.aim_direction = ic2d::Vec2{
        std::numeric_limits<float>::infinity(), 0.0F};
    expect(!combat.submit(invalid_aim), "Non-finite aim must be rejected at submission.");

    combat.reset();
    const ic2d::CombatSnapshot reset = combat.snapshot();
    expect(reset.tick == 0 && reset.consumed_command_count == 0 &&
               reset.emitted_intent_count == 0 && reset.pending_command_count == 0,
           "Reset must clear authoritative counts and buffered work.");
    expect(reset.aim_direction.x == 0.0F && reset.aim_direction.y == 1.0F,
           "Reset must restore the deterministic default aim.");

    expect(combat.submit(initial), "The reset Combat module must accept a fresh command.");
    combat.fixed_update(1);
    const auto restarted = combat.drain_events();
    const auto restarted_intents = intent_events(restarted);
    expect(restarted_intents.size() == 1 && restarted_intents.front()->sequence == 1,
           "Reset must restart tick and event identity from the initial deterministic state.");
}

void test_empty_identity_and_zero_aim_commands_are_rejected() {
    ic2d::Combat combat;
    ic2d::CombatCommand empty;
    empty.actor = {4};
    expect(!combat.submit(empty), "A command with no aim update or intent must be rejected.");

    ic2d::CombatCommand missing_actor;
    missing_actor.request(ic2d::CombatIntent::dodge);
    expect(!combat.submit(missing_actor), "A command without stable actor identity must be rejected.");

    ic2d::CombatCommand zero_aim;
    zero_aim.actor = {4};
    zero_aim.aim_direction = ic2d::Vec2{};
    expect(!combat.submit(zero_aim),
           "An explicit aim update must contain a usable non-zero direction.");
    expect(combat.snapshot().pending_command_count == 0,
           "Rejected commands must never alter the pending snapshot.");
}

void test_render_rate_aim_updates_coalesce_before_the_fixed_tick() {
    ic2d::Combat combat;

    for (const ic2d::Vec2 aim : {
             ic2d::Vec2{1.0F, 0.0F},
             ic2d::Vec2{0.0F, 1.0F},
             ic2d::Vec2{-1.0F, 0.0F},
         }) {
        ic2d::CombatCommand command;
        command.actor = {12};
        command.aim_direction = aim;
        expect(combat.submit(command), "Each valid render-rate aim update must be accepted.");
    }

    expect(combat.snapshot().pending_command_count == 1,
           "Consecutive aim-only updates for one actor must coalesce to one pending command.");
    combat.fixed_update(1);

    const ic2d::CombatSnapshot snapshot = combat.snapshot();
    expect(snapshot.consumed_command_count == 1,
           "The fixed tick must consume only the newest coalesced aim command.");
    expect(snapshot.aim_direction.x == -1.0F && snapshot.aim_direction.y == 0.0F,
           "Coalescing must preserve the most recent render-rate aim direction.");
    expect(combat.drain_events().empty(),
           "An aim-only command must update state without inventing a combat intent event.");
}

void test_needle_pistol_fire_consumes_ammo_and_emits_spawn_event() {
    ic2d::Combat combat;
    ic2d::CombatCommand fire;
    fire.actor = {73};
    fire.aim_direction = ic2d::Vec2{1.0F, 0.0F};
    fire.request(ic2d::CombatIntent::fire);

    expect(combat.submit(fire), "A needle-pistol shot must enter the command buffer.");
    combat.fixed_update(1);
    const std::vector<ic2d::CombatEvent> events = combat.drain_events();

    const ic2d::ProjectileSpawnedEvent* projectile = nullptr;
    for (const ic2d::CombatEvent& event : events) {
        if (const auto* candidate = std::get_if<ic2d::ProjectileSpawnedEvent>(&event)) {
            projectile = candidate;
        }
    }
    expect(projectile != nullptr,
           "An eligible fire intent must emit one copied projectile-spawn event.");
    if (projectile != nullptr) {
        expect(projectile->tick == 1 && projectile->sequence == 2 &&
                   projectile->projectile_id == 1,
               "The first projectile must carry stable tick, event, and projectile identity.");
        expect(projectile->actor == ic2d::EntityUuid{73} &&
                   projectile->weapon == ic2d::WeaponKind::needle_pistol,
               "The spawn event must identify its owner and weapon family.");
        expect(projectile->aim_direction.x == 1.0F && projectile->aim_direction.y == 0.0F &&
                   projectile->speed == ic2d::needle_pistol.projectile_speed &&
                   projectile->lifetime_ticks == ic2d::needle_pistol.projectile_lifetime_ticks &&
                   projectile->damage == ic2d::needle_pistol.projectile_damage,
               "The spawn event must copy every parameter needed by the projectile module.");
    }

    const ic2d::CombatSnapshot snapshot = combat.snapshot();
    expect(snapshot.weapon.magazine_ammo == ic2d::needle_pistol.magazine_capacity - 1 &&
               snapshot.weapon.reserve_ammo == ic2d::needle_pistol.initial_reserve_ammo,
           "A valid shot must consume exactly one magazine round and no reserve ammunition.");
    expect(snapshot.spawned_projectile_count == 1,
           "The authoritative snapshot must count successful projectile spawns.");
}

void test_needle_pistol_fire_cooldown_is_fixed_tick_authoritative() {
    ic2d::Combat combat;
    const auto submit_fire = [&combat]() {
        ic2d::CombatCommand fire;
        fire.actor = {81};
        fire.request(ic2d::CombatIntent::fire);
        expect(combat.submit(fire), "Every render-frame fire edge must remain acknowledged.");
    };

    submit_fire();
    combat.fixed_update(1);
    expect(projectile_events(combat.drain_events()).size() == 1,
           "The first ready fire command must spawn a projectile.");
    expect(combat.snapshot().weapon.fire_cooldown_ticks_remaining ==
               ic2d::needle_pistol.fire_cooldown_ticks,
           "A shot must arm the complete fixed-tick cooldown.");

    for (std::uint64_t tick = 2; tick <= ic2d::needle_pistol.fire_cooldown_ticks; ++tick) {
        submit_fire();
        combat.fixed_update(tick);
        expect(projectile_events(combat.drain_events()).empty(),
               "A fire command inside cooldown must not spawn or consume ammunition.");
    }

    const std::uint64_t ready_tick = ic2d::needle_pistol.fire_cooldown_ticks + 1;
    submit_fire();
    combat.fixed_update(ready_tick);
    expect(projectile_events(combat.drain_events()).size() == 1,
           "The first tick after cooldown expiry must permit the next shot.");
    expect(combat.snapshot().weapon.magazine_ammo ==
               ic2d::needle_pistol.magazine_capacity - 2,
           "Blocked fire commands must not consume magazine ammunition.");
    expect(combat.snapshot().spawned_projectile_count == 2,
           "Only cooldown-eligible commands may increase the projectile count.");
}

void test_needle_pistol_reload_has_exact_duration_and_blocks_fire() {
    ic2d::Combat combat;
    const ic2d::EntityUuid actor{96};
    const auto submit = [&combat, actor](const ic2d::CombatIntent intent) {
        ic2d::CombatCommand command;
        command.actor = actor;
        command.request(intent);
        expect(combat.submit(command), "A weapon action must enter the fixed-tick buffer.");
    };

    submit(ic2d::CombatIntent::fire);
    combat.fixed_update(1);
    static_cast<void>(combat.drain_events());
    expect(combat.snapshot().weapon.magazine_ammo ==
               ic2d::needle_pistol.magazine_capacity - 1,
           "The reload fixture must begin with exactly one missing round.");

    submit(ic2d::CombatIntent::reload);
    combat.fixed_update(2);
    static_cast<void>(combat.drain_events());
    expect(combat.snapshot().weapon.reloading &&
               combat.snapshot().weapon.reload_ticks_remaining ==
                   ic2d::needle_pistol.reload_duration_ticks,
           "Reload must start on its command tick with the complete duration remaining.");

    for (std::uint64_t tick = 3;
         tick < ic2d::needle_pistol.fire_cooldown_ticks + 2;
         ++tick) {
        combat.fixed_update(tick);
        static_cast<void>(combat.drain_events());
    }
    const std::uint64_t blocked_fire_tick = ic2d::needle_pistol.fire_cooldown_ticks + 2;
    submit(ic2d::CombatIntent::fire);
    combat.fixed_update(blocked_fire_tick);
    expect(projectile_events(combat.drain_events()).empty(),
           "Reload must block firing even after the ordinary fire cooldown expires.");
    expect(combat.snapshot().weapon.magazine_ammo ==
               ic2d::needle_pistol.magazine_capacity - 1,
           "A fire attempt during reload must not consume ammunition.");

    const std::uint64_t completion_tick = 2 + ic2d::needle_pistol.reload_duration_ticks;
    for (std::uint64_t tick = blocked_fire_tick + 1; tick < completion_tick; ++tick) {
        combat.fixed_update(tick);
        static_cast<void>(combat.drain_events());
    }
    expect(combat.snapshot().weapon.reloading &&
               combat.snapshot().weapon.reload_ticks_remaining == 1,
           "Reload must remain active through the tick immediately before completion.");

    combat.fixed_update(completion_tick);
    expect(!combat.snapshot().weapon.reloading &&
               combat.snapshot().weapon.reload_ticks_remaining == 0,
           "Reload must complete exactly on start tick plus authored duration.");
    expect(combat.snapshot().weapon.magazine_ammo == ic2d::needle_pistol.magazine_capacity &&
               combat.snapshot().weapon.reserve_ammo ==
                   ic2d::needle_pistol.initial_reserve_ammo - 1,
           "Reload must transfer only the missing rounds from reserve into the magazine.");

    submit(ic2d::CombatIntent::fire);
    combat.fixed_update(completion_tick + 1);
    expect(projectile_events(combat.drain_events()).size() == 1,
           "Firing must resume on the fixed tick after reload completion.");
}

void test_held_fire_repeats_on_fixed_cooldown_until_release() {
    ic2d::Combat combat;
    ic2d::CombatCommand hold;
    hold.actor = {120};
    hold.aim_direction = ic2d::Vec2{1.0F, 0.0F};
    hold.set_fire_held(true);
    expect(combat.submit(hold),
           "A held-fire state change must enter the fixed-tick command buffer.");

    combat.fixed_update(1);
    expect(projectile_events(combat.drain_events()).size() == 1,
           "Pressing and holding fire must shoot on the first authoritative tick.");

    std::size_t repeated_shots = 0;
    for (std::uint64_t tick = 2; tick <= 9; ++tick) {
        combat.fixed_update(tick);
        repeated_shots += projectile_events(combat.drain_events()).size();
    }
    expect(repeated_shots == 1 && combat.snapshot().spawned_projectile_count == 2,
           "Holding fire must shoot again on the exact ready tick without another input edge.");

    ic2d::CombatCommand release;
    release.actor = {120};
    release.set_fire_held(false);
    expect(combat.submit(release), "A held-fire release must enter the command buffer.");
    for (std::uint64_t tick = 10; tick <= 18; ++tick) {
        combat.fixed_update(tick);
        expect(projectile_events(combat.drain_events()).empty(),
               "Releasing LMB must stop all later automatic shots.");
    }
    expect(combat.snapshot().spawned_projectile_count == 2,
           "Release must leave the automatic-fire count unchanged.");
}

void test_mouse_aim_update_cannot_overwrite_a_held_fire_release() {
    ic2d::Combat combat;
    const ic2d::EntityUuid actor{121};

    ic2d::CombatCommand hold;
    hold.actor = actor;
    hold.aim_direction = ic2d::Vec2{1.0F, 0.0F};
    hold.set_fire_held(true);
    expect(combat.submit(hold), "The mouse-release fixture must accept held fire.");
    combat.fixed_update(1);
    static_cast<void>(combat.drain_events());

    ic2d::CombatCommand release;
    release.actor = actor;
    release.aim_direction = ic2d::Vec2{0.9F, 0.1F};
    release.set_fire_held(false);
    ic2d::CombatCommand later_mouse_motion;
    later_mouse_motion.actor = actor;
    later_mouse_motion.aim_direction = ic2d::Vec2{0.8F, 0.2F};
    expect(combat.submit(release) && combat.submit(later_mouse_motion),
           "LMB release and later mouse motion must both enter the render-frame buffer.");

    for (std::uint64_t tick = 2; tick <= 9; ++tick) {
        combat.fixed_update(tick);
        expect(projectile_events(combat.drain_events()).empty(),
               "Mouse motion after LMB release must not leave automatic fire latched.");
    }
    expect(combat.snapshot().spawned_projectile_count == 1,
           "Releasing while aiming must preserve the original one-shot count.");
}

void test_dodge_starts_on_a_fixed_tick_and_has_an_exact_invulnerability_window() {
    ic2d::Combat combat;
    const ic2d::EntityUuid actor{207};
    ic2d::CombatCommand dodge;
    dodge.actor = actor;
    dodge.dodge_direction = ic2d::Vec2{3.0F, 4.0F};
    dodge.request(ic2d::CombatIntent::dodge);

    expect(combat.submit(dodge), "A valid dodge request must enter the command buffer.");
    expect(!combat.invulnerable(actor) && !combat.snapshot().dodge.active,
           "A render-frame dodge request must not change authoritative state early.");

    combat.fixed_update(1);
    const std::vector<ic2d::CombatEvent> events = combat.drain_events();
    const auto starts = dodge_started_events(events);
    expect(starts.size() == 1, "The ready dodge request must emit one start event.");
    if (starts.size() == 1) {
        expect(starts.front()->tick == 1 && starts.front()->actor == actor,
               "The copied dodge event must identify its authoritative tick and actor.");
        expect(starts.front()->duration_ticks == ic2d::player_dodge.duration_ticks &&
                   starts.front()->invulnerability_ticks ==
                       ic2d::player_dodge.invulnerability_ticks &&
                   starts.front()->cooldown_ticks == ic2d::player_dodge.cooldown_ticks,
               "The dodge event must copy every authored timing value.");
        expect(starts.front()->direction.x == 0.6F && starts.front()->direction.y == 0.8F,
               "The dodge event must freeze the normalized movement direction from its start tick.");
    }

    ic2d::DodgeSnapshot snapshot = combat.snapshot().dodge;
    expect(snapshot.active && snapshot.invulnerable && combat.invulnerable(actor),
           "A started dodge must be active and queryable as invulnerable.");
    expect(snapshot.active_ticks_remaining == ic2d::player_dodge.duration_ticks &&
               snapshot.invulnerable_ticks_remaining ==
                   ic2d::player_dodge.invulnerability_ticks &&
               snapshot.cooldown_ticks_remaining == ic2d::player_dodge.cooldown_ticks &&
               snapshot.started_count == 1,
           "The start tick must expose the complete authored dodge timers.");
    expect(snapshot.direction.x == 0.6F && snapshot.direction.y == 0.8F,
           "The active dodge snapshot must expose its frozen world direction.");

    for (std::uint64_t tick = 2;
         tick <= ic2d::player_dodge.invulnerability_ticks;
         ++tick) {
        combat.fixed_update(tick);
        static_cast<void>(combat.drain_events());
        expect(combat.invulnerable(actor),
               "Invulnerability must remain true through its final authored tick.");
    }
    combat.fixed_update(ic2d::player_dodge.invulnerability_ticks + 1);
    static_cast<void>(combat.drain_events());
    expect(!combat.invulnerable(actor),
           "Invulnerability must end exactly after its authored fixed-tick window.");

    for (std::uint64_t tick = ic2d::player_dodge.invulnerability_ticks + 2;
         tick <= ic2d::player_dodge.duration_ticks;
         ++tick) {
        combat.fixed_update(tick);
        static_cast<void>(combat.drain_events());
    }
    expect(combat.snapshot().dodge.active,
           "Dodge activity must remain true through the complete duration.");
    combat.fixed_update(ic2d::player_dodge.duration_ticks + 1);
    static_cast<void>(combat.drain_events());
    expect(!combat.snapshot().dodge.active,
           "Dodge activity must end exactly after its authored duration.");
}

void test_dodge_direction_stays_frozen_during_cooldown() {
    ic2d::Combat combat;
    const ic2d::EntityUuid actor{209};

    ic2d::CombatCommand first;
    first.actor = actor;
    first.dodge_direction = ic2d::Vec2{0.0F, -2.0F};
    first.request(ic2d::CombatIntent::dodge);
    expect(combat.submit(first), "The first directional dodge must enter the command buffer.");
    combat.fixed_update(1);
    static_cast<void>(combat.drain_events());

    ic2d::CombatCommand rejected_restart;
    rejected_restart.actor = actor;
    rejected_restart.dodge_direction = ic2d::Vec2{1.0F, 0.0F};
    rejected_restart.request(ic2d::CombatIntent::dodge);
    expect(combat.submit(rejected_restart),
           "A cooldown dodge still reaches Combat for authoritative rejection.");
    combat.fixed_update(2);
    const std::vector<ic2d::CombatEvent> events = combat.drain_events();

    expect(dodge_started_events(events).empty(),
           "A cooldown request must not emit another dodge start.");
    const ic2d::DodgeSnapshot snapshot = combat.snapshot().dodge;
    expect(snapshot.direction.x == 0.0F && snapshot.direction.y == -1.0F,
           "A rejected restart must not redirect the active dodge.");
}

void test_combat_snapshot_copies_all_actor_state_in_stable_identity_order() {
    ic2d::Combat combat;

    ic2d::CombatCommand higher_actor;
    higher_actor.actor = {902};
    higher_actor.aim_direction = ic2d::Vec2{1.0F, 0.0F};
    higher_actor.set_fire_held(true);
    expect(combat.submit(higher_actor),
           "The higher actor fixture must enter Combat first.");

    ic2d::CombatCommand lower_actor;
    lower_actor.actor = {901};
    lower_actor.aim_direction = ic2d::Vec2{0.0F, -1.0F};
    lower_actor.dodge_direction = ic2d::Vec2{-1.0F, 0.0F};
    lower_actor.request(ic2d::CombatIntent::dodge);
    expect(combat.submit(lower_actor),
           "The lower actor fixture must enter Combat second.");

    combat.fixed_update(1);
    static_cast<void>(combat.drain_events());
    const ic2d::CombatSnapshot snapshot = combat.snapshot();

    expect(snapshot.actors.size() == 2,
           "A copied Combat snapshot must describe every actor with authoritative state.");
    if (snapshot.actors.size() == 2) {
        expect(snapshot.actors[0].actor == ic2d::EntityUuid{901} &&
                   snapshot.actors[1].actor == ic2d::EntityUuid{902},
               "Combat actor snapshots must use stable UUID order, not command arrival order.");
        expect(snapshot.actors[0].dodge.active &&
                   snapshot.actors[0].dodge.direction.x == -1.0F &&
                   !snapshot.actors[0].fire_held,
               "The lower actor snapshot must copy its frozen dodge and held-fire state.");
        expect(snapshot.actors[1].aim_direction.x == 1.0F &&
                   snapshot.actors[1].fire_held,
               "The higher actor snapshot must copy its retained aim and held-fire state.");
    }
    expect(snapshot.next_event_sequence > 1 &&
               snapshot.next_projectile_id == snapshot.spawned_projectile_count + 1,
           "The snapshot must copy the next identities that influence later deterministic results.");
}

void test_dodge_cooldown_rejects_restarts_and_reset_clears_state() {
    ic2d::Combat combat;
    const ic2d::EntityUuid actor{208};
    const auto submit_dodge = [&combat, actor]() {
        ic2d::CombatCommand command;
        command.actor = actor;
        command.request(ic2d::CombatIntent::dodge);
        expect(combat.submit(command), "Every dodge edge must remain acknowledged by Combat.");
    };

    submit_dodge();
    combat.fixed_update(1);
    static_cast<void>(combat.drain_events());

    const std::uint64_t ready_tick = 1 + ic2d::player_dodge.cooldown_ticks;
    for (std::uint64_t tick = 2; tick < ready_tick; ++tick) {
        if (tick == 2 || tick == ic2d::player_dodge.duration_ticks + 2 ||
            tick == ready_tick - 1) {
            submit_dodge();
        }
        combat.fixed_update(tick);
        const std::vector<ic2d::CombatEvent> events = combat.drain_events();
        expect(dodge_started_events(events).empty(),
               "A dodge request inside cooldown must not restart the dodge.");
        expect(combat.snapshot().dodge.started_count == 1,
               "Rejected dodge requests must leave the successful-start count unchanged.");
    }

    expect(combat.snapshot().dodge.cooldown_ticks_remaining == 1,
           "Cooldown must retain one tick immediately before readiness.");
    submit_dodge();
    combat.fixed_update(ready_tick);
    const std::vector<ic2d::CombatEvent> ready_events = combat.drain_events();
    expect(dodge_started_events(ready_events).size() == 1,
           "A dodge must start again on the exact cooldown-ready tick.");
    expect(combat.snapshot().dodge.started_count == 2 && combat.invulnerable(actor),
           "The second eligible dodge must re-arm state exactly once.");

    combat.reset();
    const ic2d::DodgeSnapshot reset = combat.snapshot().dodge;
    expect(!reset.active && !reset.invulnerable && reset.active_ticks_remaining == 0 &&
               reset.invulnerable_ticks_remaining == 0 &&
               reset.cooldown_ticks_remaining == 0 && reset.started_count == 0 &&
               !combat.invulnerable(actor),
           "Reset must clear dodge activity, invulnerability, cooldown, and counters.");
}

} // namespace

int main() {
    test_fire_is_buffered_until_the_next_fixed_tick();
    test_commands_keep_fifo_order_and_normalize_aim();
    test_invalid_ticks_do_not_consume_commands_and_reset_is_clean();
    test_empty_identity_and_zero_aim_commands_are_rejected();
    test_render_rate_aim_updates_coalesce_before_the_fixed_tick();
    test_needle_pistol_fire_consumes_ammo_and_emits_spawn_event();
    test_needle_pistol_fire_cooldown_is_fixed_tick_authoritative();
    test_needle_pistol_reload_has_exact_duration_and_blocks_fire();
    test_held_fire_repeats_on_fixed_cooldown_until_release();
    test_mouse_aim_update_cannot_overwrite_a_held_fire_release();
    test_dodge_starts_on_a_fixed_tick_and_has_an_exact_invulnerability_window();
    test_dodge_direction_stays_frozen_during_cooldown();
    test_combat_snapshot_copies_all_actor_state_in_stable_identity_order();
    test_dodge_cooldown_rejects_restarts_and_reset_clears_state();

    if (failures == 0) {
        std::cout << "Combat command-buffer tests passed.\n";
        return 0;
    }
    std::cerr << failures << " combat assertion(s) failed.\n";
    return 1;
}
