#pragma once

#include "ic2d/enemy_intent.hpp"
#include "ic2d/health.hpp"
#include "ic2d/identity.hpp"
#include "ic2d/interaction.hpp"
#include "ic2d/nav_agent.hpp"
#include "ic2d/runtime_scene.hpp"
#include "ic2d/scene.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ic2d {

// How an authored scene becomes running gameplay systems.
//
// Loading a scene and populating Health, EnemyIntent and NavAgent from it are
// different jobs: the first reads a document, the second decides what the
// actors in it are worth and what they want. Keeping the second here means the
// application only has to say "bind this scene", and means these rules can be
// read without opening the frame loop that used to contain them.

// Gameplay tuning. These are the numbers the reference encounter is balanced
// around, kept together so a change to one is made next to the others it has to
// stay consistent with.
inline constexpr float player_maximum_health = 100.0F;
inline constexpr float target_dummy_maximum_health = 54.0F;
inline constexpr float attacker_maximum_health = 36.0F;
inline constexpr float attacker_movement_speed = 54.0F;
inline constexpr float attacker_acquisition_range = 180.0F;
inline constexpr float stress_attacker_acquisition_range = 600.0F;
inline constexpr float attacker_attack_range = 20.0F;
inline constexpr std::uint32_t attacker_attack_cooldown_ticks = 45;
inline constexpr float attacker_attack_damage = 12.0F;
inline constexpr float navigation_cell_size = 20.0F;

// Above this many attackers converging on one target, steering switches from a
// route per actor to one shared flow field. A search is paid per actor and a
// field is paid per map, so the field wins as soon as a crowd is large enough
// to be interesting; the threshold sits above the authored encounter sizes so
// small fights keep their individual routes, waypoints and repath behaviour.
inline constexpr std::size_t crowd_flow_field_threshold = 128;

// Gives the player and every authored actor the health its role is worth.
// Throws std::logic_error if the scene offers a duplicate or invalid target,
// because a scene that cannot be bound is not one the run can continue with.
void register_scene_health_targets(Health& health, const RuntimeScene& scene);

// Gives every authored attacker something to want. The acquisition range is a
// parameter because a stress crowd is placed further out than the authored
// encounter and would otherwise idle where it spawned.
void register_scene_enemy_intents(EnemyIntent& intent, const RuntimeScene& scene,
                                  float acquisition_range = attacker_acquisition_range);

// Registers only the actors that consume an individual route.
//
// A field-steered crowd never consumes one, so registering it with the router
// would cost a request, a map traversal and a returned motion per actor every
// tick for a result that is discarded. The caller's steering plan decides both
// registration and steering, so the two cannot disagree.
void register_scene_navigation_agents(NavAgentSystem& navigation,
                                      const std::vector<EntityUuid>& routed_actors);

// Resolves authored interactables against the entities they attach to. Pickups
// do not move, so their authored position is their world position for the whole
// run and nothing has to be re-resolved per tick.
[[nodiscard]] std::vector<Interactable> build_interactables(const SceneDefinition& definition);

} // namespace ic2d
