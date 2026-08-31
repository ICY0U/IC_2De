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

struct SceneDocumentEntity {
    EntityUuid uuid{};
    std::string id;
    std::string name;
    std::string prefab_id; // Empty unless the record instantiates a prefab.
    bool physics_bound{false};
    Vec3 position{};
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
    // Returns the allocated stable identity, or a zero UUID when the prefab is unknown.
    [[nodiscard]] EntityUuid create_prefab_instance(const ScenePrefabPlacement& placement);
    [[nodiscard]] bool destroy_prefab_instance(EntityUuid uuid);
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
