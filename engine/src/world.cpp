#include "ic2d/world.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>

namespace ic2d {
namespace {

struct Identity {
    EntityId id{};
    EntityUuid uuid{};
};

struct Name {
    std::string value;
};

struct SpawnCommand {
    EntityId id{};
    EntityBlueprint blueprint{};
};

struct DestroyCommand {
    EntityId id{};
};

using WorldCommand = std::variant<SpawnCommand, DestroyCommand>;

} // namespace

struct World::Impl {
    entt::registry registry;
    std::unordered_map<std::uint64_t, entt::entity> entities;
    std::unordered_map<std::uint64_t, EntityId> entities_by_uuid;
    std::unordered_set<std::uint64_t> reserved_uuids;
    std::vector<WorldCommand> commands;
    std::uint64_t next_entity_id{1};
    std::uint64_t next_entity_uuid{1};
};

World::World()
    : impl_{std::make_unique<Impl>()} {}

World::~World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

EntityId World::queue_spawn(EntityBlueprint blueprint) {
    if (!blueprint.uuid) {
        while (impl_->next_entity_uuid != 0 &&
               impl_->reserved_uuids.contains(impl_->next_entity_uuid)) {
            ++impl_->next_entity_uuid;
        }
        if (impl_->next_entity_uuid == 0) {
            throw std::overflow_error{"World exhausted persistent entity UUIDs."};
        }
        blueprint.uuid = EntityUuid{impl_->next_entity_uuid++};
    } else if (impl_->reserved_uuids.contains(blueprint.uuid.value)) {
        throw std::invalid_argument{"World entity UUIDs must be non-zero and unique."};
    }

    impl_->reserved_uuids.insert(blueprint.uuid.value);
    const EntityId id{impl_->next_entity_id++};
    impl_->commands.emplace_back(SpawnCommand{.id = id, .blueprint = std::move(blueprint)});
    return id;
}

void World::queue_destroy(const EntityId entity) {
    if (entity) {
        impl_->commands.emplace_back(DestroyCommand{.id = entity});
    }
}

void World::flush() {
    for (WorldCommand& command : impl_->commands) {
        if (auto* spawn = std::get_if<SpawnCommand>(&command)) {
            if (impl_->entities.contains(spawn->id.value)) {
                continue;
            }

            const entt::entity internal = impl_->registry.create();
            impl_->registry.emplace<Identity>(
                internal, Identity{.id = spawn->id, .uuid = spawn->blueprint.uuid});
            impl_->registry.emplace<Name>(internal, std::move(spawn->blueprint.name));
            impl_->registry.emplace<WorldTransform>(internal, spawn->blueprint.transform);
            if (spawn->blueprint.sprite) {
                impl_->registry.emplace<Sprite2D>(internal, *spawn->blueprint.sprite);
            }
            impl_->entities.emplace(spawn->id.value, internal);
            impl_->entities_by_uuid.emplace(spawn->blueprint.uuid.value, spawn->id);
            continue;
        }

        const DestroyCommand& destroy = std::get<DestroyCommand>(command);
        const auto found = impl_->entities.find(destroy.id.value);
        if (found != impl_->entities.end()) {
            const EntityUuid persistent = impl_->registry.get<Identity>(found->second).uuid;
            impl_->registry.destroy(found->second);
            impl_->entities.erase(found);
            impl_->entities_by_uuid.erase(persistent.value);
            impl_->reserved_uuids.erase(persistent.value);
        }
    }
    impl_->commands.clear();
}

void World::clear() {
    impl_->commands.clear();
    impl_->entities.clear();
    impl_->entities_by_uuid.clear();
    impl_->reserved_uuids.clear();
    impl_->registry.clear();
}

bool World::alive(const EntityId entity) const noexcept {
    return entity && impl_->entities.contains(entity.value);
}

std::optional<EntityUuid> World::uuid(const EntityId entity) const noexcept {
    const auto found = impl_->entities.find(entity.value);
    if (found == impl_->entities.end()) {
        return std::nullopt;
    }
    return impl_->registry.get<Identity>(found->second).uuid;
}

std::optional<EntityId> World::find(const EntityUuid persistent) const noexcept {
    const auto found = impl_->entities_by_uuid.find(persistent.value);
    return found == impl_->entities_by_uuid.end()
               ? std::nullopt
               : std::optional<EntityId>{found->second};
}

std::size_t World::entity_count() const noexcept {
    return impl_->entities.size();
}

std::optional<WorldTransform> World::transform(const EntityId entity) const {
    const auto found = impl_->entities.find(entity.value);
    if (found == impl_->entities.end()) {
        return std::nullopt;
    }
    const auto* value = impl_->registry.try_get<WorldTransform>(found->second);
    return value ? std::optional<WorldTransform>{*value} : std::nullopt;
}

bool World::set_position(const EntityId entity, const Vec3& position) {
    const auto found = impl_->entities.find(entity.value);
    if (found == impl_->entities.end()) {
        return false;
    }
    auto* current = impl_->registry.try_get<WorldTransform>(found->second);
    if (!current) {
        return false;
    }
    current->position = position;
    return true;
}

bool World::set_transform(const EntityId entity, const WorldTransform& transform_value) {
    const auto found = impl_->entities.find(entity.value);
    if (found == impl_->entities.end()) {
        return false;
    }
    auto* current = impl_->registry.try_get<WorldTransform>(found->second);
    if (!current) {
        return false;
    }
    *current = transform_value;
    return true;
}

WorldSnapshot World::snapshot() const {
    WorldSnapshot result;
    result.entities.reserve(impl_->entities.size());
    for (const auto& [id, internal] : impl_->entities) {
        static_cast<void>(id);
        const Identity& identity = impl_->registry.get<Identity>(internal);
        const Name& name = impl_->registry.get<Name>(internal);
        const WorldTransform& transform_value = impl_->registry.get<WorldTransform>(internal);
        const Sprite2D* sprite = impl_->registry.try_get<Sprite2D>(internal);
        result.entities.push_back({
            .uuid = identity.uuid,
            .name = name.value,
            .transform = transform_value,
            .sprite = sprite ? std::optional<Sprite2D>{*sprite} : std::nullopt,
        });
    }
    std::ranges::sort(result.entities, {}, [](const EntityBlueprint& entity) {
        return entity.uuid.value;
    });
    return result;
}

void World::restore(const WorldSnapshot& source) {
    World replacement;
    for (const EntityBlueprint& entity : source.entities) {
        if (!entity.uuid) {
            throw std::invalid_argument{"World snapshots require non-zero entity UUIDs."};
        }
        static_cast<void>(replacement.queue_spawn(entity));
    }
    replacement.flush();
    *this = std::move(replacement);
}

std::vector<RenderItem2D> World::collect_render_items(
    const std::optional<RectXZ> region
) const {
    std::vector<RenderItem2D> items;
    items.reserve(impl_->entities.size());

    const auto inside = [&region](const WorldTransform& transform_value) {
        if (!region) {
            return true;
        }
        return transform_value.position.x >= region->x &&
               transform_value.position.x <= region->x + region->width &&
               transform_value.position.z >= region->z &&
               transform_value.position.z <= region->z + region->depth;
    };

    for (const auto& [id, internal] : impl_->entities) {
        const auto* transform_value = impl_->registry.try_get<WorldTransform>(internal);
        const auto* sprite = impl_->registry.try_get<Sprite2D>(internal);
        if (transform_value && sprite && inside(*transform_value)) {
            items.push_back(RenderItem2D{
                .entity = EntityId{id},
                .transform = *transform_value,
                .sprite = *sprite,
            });
        }
    }

    std::ranges::sort(items, [](const RenderItem2D& left, const RenderItem2D& right) {
        if (left.sprite.layer != right.sprite.layer) {
            return left.sprite.layer < right.sprite.layer;
        }
        return left.entity.value < right.entity.value;
    });
    return items;
}

} // namespace ic2d
