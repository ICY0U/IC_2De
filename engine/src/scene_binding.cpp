#include "ic2d/scene_binding.hpp"

#include <algorithm>
#include <stdexcept>

namespace ic2d {

void register_scene_health_targets(Health& health, const RuntimeScene& scene) {
    if (!health.register_target({
            .target = scene.player_uuid(),
            .maximum_health = player_maximum_health,
        })) {
        throw std::logic_error{"Runtime scene provided an invalid player health target."};
    }
    for (const EntityUuid actor : scene.actor_uuids(ScenePhysicsRole::enemy)) {
        if (!health.register_target({
                .target = actor,
                .maximum_health = target_dummy_maximum_health,
            })) {
            throw std::logic_error{"Runtime scene provided a duplicate or invalid health target."};
        }
    }
    for (const EntityUuid actor : scene.actor_uuids(ScenePhysicsRole::attacker)) {
        if (!health.register_target({
                .target = actor,
                .maximum_health = attacker_maximum_health,
            })) {
            throw std::logic_error{
                "Runtime scene provided a duplicate or invalid attacker health target."};
        }
    }
}

void register_scene_enemy_intents(EnemyIntent& intent, const RuntimeScene& scene,
                                  const float acquisition_range) {
    for (const EntityUuid actor : scene.actor_uuids(ScenePhysicsRole::attacker)) {
        if (!intent.register_actor({
                .actor = actor,
                .target = scene.player_uuid(),
                .movement_speed = attacker_movement_speed,
                .acquisition_range = acquisition_range,
                .attack_range = attacker_attack_range,
                .attack_cooldown_ticks = attacker_attack_cooldown_ticks,
                .attack_damage = attacker_attack_damage,
            })) {
            throw std::logic_error{
                "Runtime scene provided a duplicate or invalid attacker intent."};
        }
    }
}

void register_scene_navigation_agents(NavAgentSystem& navigation,
                                      const std::vector<EntityUuid>& routed_actors) {
    for (const EntityUuid actor : routed_actors) {
        if (!navigation.register_agent(actor)) {
            throw std::logic_error{
                "Runtime scene provided a duplicate or invalid navigation agent."};
        }
    }
}

std::vector<Interactable> build_interactables(const SceneDefinition& definition) {
    std::vector<Interactable> resolved;
    resolved.reserve(definition.interactables().size());
    for (const SceneInteractableDefinition& authored : definition.interactables()) {
        const auto entity = std::ranges::find(definition.entities(), authored.entity_id,
                                              &SceneEntityDefinition::id);
        if (entity == definition.entities().end()) {
            continue;
        }
        resolved.push_back({
            .entity = entity->uuid,
            .position = entity->position,
            .kind = authored.kind,
            .amount = authored.amount,
            .radius = authored.radius,
        });
    }
    return resolved;
}

} // namespace ic2d
