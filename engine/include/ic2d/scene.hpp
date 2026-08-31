#pragma once

#include "ic2d/animation.hpp"
#include "ic2d/ground_map.hpp"
#include "ic2d/identity.hpp"
#include "ic2d/locomotion.hpp"
#include "ic2d/physics2d.hpp"
#include "ic2d/projection25d.hpp"
#include "ic2d/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ic2d {

enum class SceneTextureKind {
    file,
    checker,
    radial,
};

enum class SceneTextureSampling {
    pixel,
    smooth,
};

struct SceneTextureDefinition {
    std::string id;
    SceneTextureKind kind{SceneTextureKind::file};
    SceneTextureSampling sampling{SceneTextureSampling::pixel};
    std::filesystem::path relative_path;
    int width{0};
    int height{0};
    int cell_size{0};
    ColorRgba8 first_color{};
    ColorRgba8 second_color{};
};

enum class ScenePhysicsRole {
    player,
    primary_prop,
    enemy,
    generic,
};

struct ScenePhysicsBodyDefinition {
    std::string id;
    ScenePhysicsRole role{ScenePhysicsRole::generic};
    PhysicsBoxDefinition box{};
};

struct SceneSpriteDefinition {
    Vec2 size{16.0F, 16.0F};
    Vec2 normalized_origin{0.5F, 1.0F};
    ColorRgba8 tint{};
    std::int32_t layer{0};
    std::string texture_id;
};

// A reusable sprite template with its own persistent identity. Instances copy
// the template and may override individual sprite fields.
struct ScenePrefabDefinition {
    std::string id;
    EntityUuid uuid{};
    std::string name;
    SceneSpriteDefinition sprite{};
};

struct SceneEntityDefinition {
    std::string id;
    EntityUuid uuid{};
    std::string name;
    std::string physics_binding;
    std::string prefab_id; // Empty for entities authored without a prefab.
    Vec3 position{};
    SceneSpriteDefinition sprite{};
};

struct SceneAnimationClipDefinition {
    AnimationClip clip;
    std::string texture_id;
};

struct SceneAnimationBindingDefinition {
    std::string entity_id;
    LocomotionState initial_state{LocomotionState::idle_south};
    std::array<std::string, locomotion_state_count> state_clips;
};

struct SceneSimulationDefinition {
    PhysicsWorldConfig physics{};
    std::uint64_t ground_category_bits{1};
    std::uint64_t ground_mask_bits{~std::uint64_t{0}};
    std::uint64_t trigger_category_bits{1};
    std::uint64_t trigger_mask_bits{~std::uint64_t{0}};
    float world_boundary_thickness{32.0F};
    float player_speed{130.0F};
};

// Immutable, fully validated authored data. Loading performs schema, numeric,
// path, uniqueness, reference, GroundMap, and PhysicsWorld validation.
class SceneDefinition final {
public:
    [[nodiscard]] static SceneDefinition load(const std::filesystem::path& scene_path);

    [[nodiscard]] std::uint32_t schema_version() const noexcept;
    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] const std::filesystem::path& source_path() const noexcept;
    [[nodiscard]] std::filesystem::path resolve_asset(
        const std::filesystem::path& relative_path
    ) const;
    [[nodiscard]] const GroundMapDefinition& ground() const noexcept;
    [[nodiscard]] const SceneSimulationDefinition& simulation() const noexcept;
    [[nodiscard]] const Camera25DState& camera() const noexcept;
    [[nodiscard]] const std::vector<SceneTextureDefinition>& textures() const noexcept;
    [[nodiscard]] const std::vector<ScenePhysicsBodyDefinition>& physics_bodies() const noexcept;
    [[nodiscard]] const std::vector<ScenePrefabDefinition>& prefabs() const noexcept;
    // Authored entities and expanded prefab instances in authored source order.
    [[nodiscard]] const std::vector<SceneEntityDefinition>& entities() const noexcept;
    [[nodiscard]] const std::vector<SceneAnimationClipDefinition>& animation_clips() const noexcept;
    [[nodiscard]] const std::vector<SceneAnimationBindingDefinition>& animation_bindings() const noexcept;

private:
    std::uint32_t schema_version_{0};
    std::string id_;
    std::filesystem::path source_path_;
    GroundMapDefinition ground_;
    SceneSimulationDefinition simulation_;
    Camera25DState camera_;
    std::vector<SceneTextureDefinition> textures_;
    std::vector<ScenePhysicsBodyDefinition> physics_bodies_;
    std::vector<ScenePrefabDefinition> prefabs_;
    std::vector<SceneEntityDefinition> entities_;
    std::vector<SceneAnimationClipDefinition> animation_clips_;
    std::vector<SceneAnimationBindingDefinition> animation_bindings_;
};

} // namespace ic2d
