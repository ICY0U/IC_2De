#include "ic2d/scene_editor.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ic2d {
namespace {

[[nodiscard]] std::string quoted_text(const std::string_view text) {
    return '\'' + std::string{text} + '\'';
}

[[nodiscard]] std::string placement_label(const ScenePrefabPlacement& placement) {
    return "Create " + quoted_text(placement.prefab_id) + " instance " +
           quoted_text(placement.instance_id);
}

[[nodiscard]] std::string instance_id_of(
    const std::vector<SceneDocumentEntity>& entities,
    const EntityUuid uuid
) {
    const auto found = std::ranges::find(entities, uuid, &SceneDocumentEntity::uuid);
    return found == entities.end() ? std::string{} : found->id;
}

} // namespace

SceneEditor::SceneEditor(SceneDocument document, const std::size_t history_limit)
    : document_{std::move(document)}, history_limit_{history_limit} {
    if (history_limit_ == 0) {
        throw std::invalid_argument{"A scene editor requires room for at least one undo step."};
    }
}

SceneEditor SceneEditor::open(const std::filesystem::path& path) {
    return SceneEditor{SceneDocument::open(path)};
}

const SceneDocument& SceneEditor::document() const noexcept { return document_; }
std::vector<SceneDocumentEntity> SceneEditor::entities() const { return document_.entities(); }
std::vector<SceneDocumentPrefab> SceneEditor::prefabs() const { return document_.prefabs(); }

void SceneEditor::commit(SceneDocument candidate, SceneEdit edit) {
    redo_.clear();
    undo_.push_back({
        .edit = std::move(edit),
        .document = std::move(document_),
        .revision = current_revision_,
    });
    document_ = std::move(candidate);
    current_revision_ = next_revision_++;
    if (undo_.size() > history_limit_) {
        undo_.erase(undo_.begin());
    }
}

bool SceneEditor::rename_entity(const EntityUuid uuid, const std::string_view name) {
    SceneDocument candidate = document_;
    if (!candidate.rename_entity(uuid, name)) {
        return false;
    }
    commit(std::move(candidate), {
        .kind = SceneEditKind::rename_entity,
        .uuid = uuid,
        .label = "Rename entity to " + quoted_text(name),
    });
    return true;
}

bool SceneEditor::move_unbound_entity(const EntityUuid uuid, const Vec3 position) {
    SceneDocument candidate = document_;
    if (!candidate.set_unbound_entity_position(uuid, position)) {
        return false;
    }
    commit(std::move(candidate), {
        .kind = SceneEditKind::move_entity,
        .uuid = uuid,
        .label = "Move entity " + quoted_text(instance_id_of(document_.entities(), uuid)),
    });
    return true;
}

EntityUuid SceneEditor::create_prefab_instance(const ScenePrefabPlacement& placement) {
    SceneDocument candidate = document_;
    const EntityUuid created = candidate.create_prefab_instance(placement);
    if (!created) {
        return {};
    }
    commit(std::move(candidate), {
        .kind = SceneEditKind::create_prefab_instance,
        .uuid = created,
        .label = placement_label(placement),
    });
    return created;
}

bool SceneEditor::destroy_prefab_instance(const EntityUuid uuid) {
    const std::string instance_id = instance_id_of(document_.entities(), uuid);
    SceneDocument candidate = document_;
    if (!candidate.destroy_prefab_instance(uuid)) {
        return false;
    }
    commit(std::move(candidate), {
        .kind = SceneEditKind::destroy_prefab_instance,
        .uuid = uuid,
        .label = "Destroy prefab instance " + quoted_text(instance_id),
    });
    return true;
}

bool SceneEditor::can_undo() const noexcept { return !undo_.empty(); }
bool SceneEditor::can_redo() const noexcept { return !redo_.empty(); }

bool SceneEditor::undo() {
    if (undo_.empty()) {
        return false;
    }
    HistoryEntry entry = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back({
        .edit = entry.edit,
        .document = std::move(document_),
        .revision = current_revision_,
    });
    document_ = std::move(entry.document);
    current_revision_ = entry.revision;
    return true;
}

bool SceneEditor::redo() {
    if (redo_.empty()) {
        return false;
    }
    HistoryEntry entry = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back({
        .edit = entry.edit,
        .document = std::move(document_),
        .revision = current_revision_,
    });
    document_ = std::move(entry.document);
    current_revision_ = entry.revision;
    return true;
}

std::vector<SceneEdit> SceneEditor::history() const {
    std::vector<SceneEdit> result;
    result.reserve(undo_.size());
    for (const HistoryEntry& entry : undo_) {
        result.push_back(entry.edit);
    }
    return result;
}

std::size_t SceneEditor::undone_count() const noexcept { return redo_.size(); }

bool SceneEditor::modified() const noexcept {
    return current_revision_ != saved_revision_;
}

SceneDefinition SceneEditor::runtime_copy() const { return document_.runtime_copy(); }

void SceneEditor::save_atomic(const std::filesystem::path& destination) {
    document_.save_atomic(destination);
    // Saving a copy elsewhere does not make the opened document clean.
    if (std::filesystem::absolute(destination).lexically_normal() == document_.source_path()) {
        saved_revision_ = current_revision_;
    }
}

} // namespace ic2d
