#pragma once

#include "ic2d/assets.hpp"
#include "ic2d/identity.hpp"
#include "ic2d/types.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ic2d {

// World-space coordinates use X/Z for the ground plane and +Y for elevation.
// Presentation decides how this transform is projected; the World is not 2D-renderer aware.
struct WorldTransform {
    Vec3 position{};
    float heading_degrees{0.0F};
    Vec3 scale{1.0F, 1.0F, 1.0F};
};

struct Sprite2D {
    TextureHandle texture{};
    RectF source{}; // A zero-sized source uses the complete texture.
    Vec2 size{16.0F, 16.0F};
    Vec2 normalized_origin{0.5F, 1.0F};
    float rotation_degrees{0.0F};
    ColorRgba8 tint{};
    std::int32_t layer{0};
};

struct EntityBlueprint {
    EntityUuid uuid{}; // Zero asks World to allocate a stable identity.
    std::string name{"Entity"};
    WorldTransform transform{};
    std::optional<Sprite2D> sprite{};
};

// A backend-independent copy of authored/runtime entity state. Transient
// EntityId values are deliberately absent and are recreated on restore.
struct WorldSnapshot {
    std::vector<EntityBlueprint> entities;
};

struct RenderItem2D {
    EntityId entity{};
    WorldTransform transform{};
    Sprite2D sprite{};
};

// Owns entity storage and deferred structural mutation. EnTT remains private.
// A World is main-thread owned; immutable snapshots may be handed to CPU jobs.
class World final {
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept;
    World& operator=(World&&) noexcept;

    [[nodiscard]] EntityId queue_spawn(EntityBlueprint blueprint);
    void queue_destroy(EntityId entity);
    void flush();
    void clear();

    [[nodiscard]] bool alive(EntityId entity) const noexcept;
    [[nodiscard]] std::optional<EntityUuid> uuid(EntityId entity) const noexcept;
    [[nodiscard]] std::optional<EntityId> find(EntityUuid uuid) const noexcept;
    [[nodiscard]] std::size_t entity_count() const noexcept;
    [[nodiscard]] std::optional<WorldTransform> transform(EntityId entity) const;
    [[nodiscard]] bool set_transform(EntityId entity, const WorldTransform& transform);
    [[nodiscard]] WorldSnapshot snapshot() const;
    void restore(const WorldSnapshot& snapshot);
    [[nodiscard]] std::vector<RenderItem2D> collect_render_items() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
