#pragma once

#include "ic2d/identity.hpp"
#include "ic2d/scene.hpp"
#include "ic2d/scene_document.hpp"
#include "ic2d/types.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ic2d {

enum class SceneEditKind {
    rename_entity,
    move_entity,
    create_prefab_instance,
    destroy_prefab_instance,
};

struct SceneEdit {
    SceneEditKind kind{SceneEditKind::rename_entity};
    EntityUuid uuid{};
    std::string label;
};

// The single undoable seam for authored-scene mutation. Callers describe
// intent by stable UUID; the editor owns history, unsaved state, and validated
// persistence. A rejected command leaves the document and history untouched.
class SceneEditor final {
public:
    static constexpr std::size_t default_history_limit = 64;

    [[nodiscard]] static SceneEditor open(const std::filesystem::path& path);
    explicit SceneEditor(
        SceneDocument document,
        std::size_t history_limit = default_history_limit
    );

    [[nodiscard]] const SceneDocument& document() const noexcept;
    [[nodiscard]] std::vector<SceneDocumentEntity> entities() const;
    [[nodiscard]] std::vector<SceneDocumentPrefab> prefabs() const;

    [[nodiscard]] bool rename_entity(EntityUuid uuid, std::string_view name);
    [[nodiscard]] bool move_unbound_entity(EntityUuid uuid, Vec3 position);
    // Returns the allocated stable identity, or a zero UUID when the prefab is unknown.
    [[nodiscard]] EntityUuid create_prefab_instance(const ScenePrefabPlacement& placement);
    [[nodiscard]] bool destroy_prefab_instance(EntityUuid uuid);

    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    bool undo();
    bool redo();
    [[nodiscard]] std::vector<SceneEdit> history() const; // Applied edits, oldest first.
    [[nodiscard]] std::size_t undone_count() const noexcept;
    [[nodiscard]] bool modified() const noexcept;

    [[nodiscard]] SceneDefinition runtime_copy() const;
    void save_atomic(const std::filesystem::path& destination);

private:
    // Each history entry pairs one described edit with the document state on
    // the other side of it, so undo and redo are the same operation mirrored.
    struct HistoryEntry {
        SceneEdit edit;
        SceneDocument document;
        std::uint64_t revision{0};
    };

    void commit(SceneDocument candidate, SceneEdit edit);

    SceneDocument document_;
    std::vector<HistoryEntry> undo_;
    std::vector<HistoryEntry> redo_;
    std::size_t history_limit_{default_history_limit};
    std::uint64_t current_revision_{0};
    std::uint64_t next_revision_{1};
    std::uint64_t saved_revision_{0};
};

} // namespace ic2d
