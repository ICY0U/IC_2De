#pragma once

#include "ic2d/identity.hpp"
#include "ic2d/scene.hpp"
#include "ic2d/types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ic2d {

// The editable presentation of one placement. It is a copy: changing it does
// nothing until it is handed back through set_entity_sprite().
struct SceneDocumentSprite {
    Vec2 size{16.0F, 16.0F};
    Vec2 normalized_origin{0.5F, 1.0F};
    ColorRgba8 tint{255, 255, 255, 255};
    std::int32_t layer{0};
    std::string texture_id; // Empty draws an untextured quad in the tint.
    float depth_span{0.0F};
};

struct SceneDocumentEntity {
    EntityUuid uuid{};
    std::string id;
    std::string name;
    std::string prefab_id; // Empty unless the record instantiates a prefab.
    bool physics_bound{false};
    // The placement this one belongs to, or zero when it stands on its own.
    EntityUuid parent{};
    Vec3 position{};
    // A prefab instance draws its template's sprite, so only a plain entity
    // record carries editable sprite fields of its own.
    bool has_own_sprite{false};
    SceneDocumentSprite sprite{};
};

struct SceneDocumentPrefab {
    EntityUuid uuid{};
    std::string id;
    std::string name;
};

// Where and how a new prefab instance enters an authored scene. Editor-created
// instances are unbound; physics-bound placement remains authored content.
struct ScenePrefabPlacement {
    std::string prefab_id;
    std::string instance_id;
    std::string name;
    Vec3 position{};
};

// Where and what a new plain entity is. Editor-created entities are unbound:
// binding one to a physics body stays authored content, because a body is
// gameplay structure rather than a placement.
struct SceneEntityPlacement {
    std::string id;
    std::string name;
    Vec3 position{};
    SceneDocumentSprite sprite{};
};

// Mutable authored-scene document used by tools. It preserves the source
// record layout, edits entities by stable UUID, validates complete candidates,
// and replaces files only after validation succeeds.
class SceneDocument final {
public:
    [[nodiscard]] static SceneDocument open(const std::filesystem::path& path);
    [[nodiscard]] static SceneDocument migrate_to_current(
        const std::filesystem::path& path
    );

    [[nodiscard]] std::uint32_t schema_version() const noexcept;
    [[nodiscard]] const std::filesystem::path& source_path() const noexcept;
    [[nodiscard]] std::vector<SceneDocumentEntity> entities() const;
    [[nodiscard]] std::vector<SceneDocumentPrefab> prefabs() const;

    [[nodiscard]] bool rename_entity(EntityUuid uuid, std::string_view name);
    [[nodiscard]] bool set_unbound_entity_position(EntityUuid uuid, Vec3 position);
    // Rewrites the presentation fields of a plain entity record. Prefab
    // instances are rejected: their sprite belongs to the template, and
    // changing one placement must not silently fork it.
    [[nodiscard]] bool set_entity_sprite(EntityUuid uuid, const SceneDocumentSprite& sprite);
    // Returns the allocated stable identity, or a zero UUID when the prefab is unknown.
    [[nodiscard]] EntityUuid create_prefab_instance(const ScenePrefabPlacement& placement);
    [[nodiscard]] bool destroy_prefab_instance(EntityUuid uuid);
    // Returns the allocated stable identity. Ids must be unique in the scene.
    [[nodiscard]] EntityUuid create_entity(const SceneEntityPlacement& placement);
    // Removes a plain entity record. Prefab instances and records something
    // still references are rejected rather than silently breaking the scene.
    [[nodiscard]] bool destroy_entity(EntityUuid uuid);
    [[nodiscard]] SceneDefinition runtime_copy() const;
    void save_atomic(const std::filesystem::path& destination) const;

private:
    SceneDocument(
        std::filesystem::path source_path,
        std::vector<std::string> lines,
        std::uint32_t schema_version
    );

    std::filesystem::path source_path_;
    std::vector<std::string> lines_;
    std::uint32_t schema_version_{0};
};

} // namespace ic2d
