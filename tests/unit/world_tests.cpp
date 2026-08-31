#include "ic2d/world.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_structural_commands_are_deferred() {
    ic2d::World world;
    const ic2d::EntityId entity = world.queue_spawn({.name = "Deferred"});
    expect(!world.alive(entity), "Queued spawn must not become visible before flush.");
    expect(world.entity_count() == 0, "Queued spawn must not change the live count before flush.");

    world.flush();
    expect(world.alive(entity), "Flushed spawn must become alive.");
    expect(world.entity_count() == 1, "Flushed spawn must increment the live count.");

    world.queue_destroy(entity);
    expect(world.alive(entity), "Queued destroy must not invalidate an entity before flush.");
    world.flush();
    expect(!world.alive(entity), "Flushed destroy must invalidate the entity.");
}

void test_spawn_then_destroy_in_one_flush_obeys_order() {
    ic2d::World world;
    const ic2d::EntityId entity = world.queue_spawn({.name = "Short lived"});
    world.queue_destroy(entity);
    world.flush();
    expect(!world.alive(entity), "Command processing must preserve spawn-then-destroy order.");
    expect(world.entity_count() == 0, "Spawn-then-destroy must leave no live entity.");
}

void test_transform_access_is_safe() {
    ic2d::World world;
    const ic2d::EntityId entity = world.queue_spawn({
        .name = "Mover",
        .transform = {.position = {10.0F, 20.0F, 30.0F}},
    });
    world.flush();

    const bool updated = world.set_transform(entity, {.position = {42.0F, 24.0F, 12.0F}});
    const auto transform = world.transform(entity);
    expect(updated, "Live entity transform must be updateable.");
    expect(transform.has_value(), "Live entity transform must be readable.");
    expect(transform && std::abs(transform->position.x - 42.0F) < 0.001F,
           "Transform update must persist.");
    expect(transform && std::abs(transform->position.y - 24.0F) < 0.001F &&
               std::abs(transform->position.z - 12.0F) < 0.001F,
           "World transform must preserve elevation and depth.");
    expect(!world.set_transform(ic2d::EntityId{9999}, {}),
           "Unknown entity transform update must fail safely.");
}

void test_render_snapshot_is_sorted() {
    ic2d::World world;
    const auto foreground = world.queue_spawn({
        .name = "Foreground",
        .sprite = ic2d::Sprite2D{.layer = 5},
    });
    const auto background = world.queue_spawn({
        .name = "Background",
        .sprite = ic2d::Sprite2D{.layer = -2},
    });
    static_cast<void>(foreground);
    world.flush();

    const auto items = world.collect_render_items();
    expect(items.size() == 2, "All sprite entities must appear in the render snapshot.");
    expect(items.size() == 2 && items.front().entity == background,
           "Render snapshot must be sorted by layer.");
}

void test_clear_discards_live_and_queued_entities() {
    ic2d::World world;
    static_cast<void>(world.queue_spawn({.name = "Live"}));
    world.flush();
    const auto queued = world.queue_spawn({.name = "Queued"});
    world.clear();
    expect(world.entity_count() == 0, "Clear must remove live entities.");
    world.flush();
    expect(!world.alive(queued), "Clear must discard queued commands.");
}

void test_persistent_identity_is_unique_and_queryable() {
    ic2d::World world;
    const ic2d::EntityId authored = world.queue_spawn({
        .uuid = {9001},
        .name = "Authored",
    });
    const ic2d::EntityId generated = world.queue_spawn({.name = "Generated"});

    bool duplicate_rejected = false;
    try {
        static_cast<void>(world.queue_spawn({.uuid = {9001}, .name = "Duplicate"}));
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    expect(duplicate_rejected, "Duplicate persistent UUIDs must be rejected before flush.");

    world.flush();
    expect(world.uuid(authored) == ic2d::EntityUuid{9001},
           "An authored UUID must remain attached to its transient entity.");
    expect(world.find({9001}) == authored,
           "A persistent UUID must resolve to the current transient entity.");
    const auto generated_uuid = world.uuid(generated);
    expect(generated_uuid.has_value() && static_cast<bool>(*generated_uuid),
           "World must allocate a non-zero UUID when one is not authored.");
    expect(generated_uuid && world.find(*generated_uuid) == generated,
           "Generated UUIDs must use the same lookup path as authored UUIDs.");
}

void test_snapshot_restore_preserves_authored_state_with_new_runtime_ids() {
    ic2d::World source;
    const ic2d::EntityId second = source.queue_spawn({
        .uuid = {7002},
        .name = "Second",
        .transform = {.position = {2.0F, 4.0F, 6.0F}},
        .sprite = ic2d::Sprite2D{.size = {20.0F, 30.0F}, .layer = 3},
    });
    const ic2d::EntityId first = source.queue_spawn({
        .uuid = {7001},
        .name = "First",
        .transform = {.position = {1.0F, 3.0F, 5.0F}},
    });
    static_cast<void>(second);
    source.flush();

    const ic2d::WorldSnapshot snapshot = source.snapshot();
    expect(snapshot.entities.size() == 2,
           "A World snapshot must contain every live entity.");
    expect(snapshot.entities.size() == 2 && snapshot.entities[0].uuid.value == 7001 &&
               snapshot.entities[1].uuid.value == 7002,
           "World snapshots must use deterministic persistent-UUID order.");

    static_cast<void>(source.set_transform(first, {.position = {99.0F, 99.0F, 99.0F}}));
    ic2d::World restored;
    static_cast<void>(restored.queue_spawn({.name = "State to replace"}));
    restored.flush();
    restored.restore(snapshot);

    const auto restored_first = restored.find({7001});
    const auto restored_second = restored.find({7002});
    expect(restored.entity_count() == 2 && restored_first && restored_second,
           "Restore must replace the World with entities addressable by stable UUID.");
    const auto first_transform = restored_first ? restored.transform(*restored_first) : std::nullopt;
    expect(first_transform && std::abs(first_transform->position.x - 1.0F) < 0.001F,
           "Snapshot data must not alias later source-World mutations.");
    const auto copied = restored.snapshot();
    expect(copied.entities.size() == 2 && copied.entities[1].sprite &&
               copied.entities[1].sprite->size.x == 20.0F &&
               copied.entities[1].sprite->layer == 3,
           "Restore must preserve optional sprite state through the World interface.");
}

void test_invalid_snapshot_does_not_replace_live_world() {
    ic2d::World world;
    const ic2d::EntityId original = world.queue_spawn({.uuid = {8001}, .name = "Original"});
    world.flush();
    const ic2d::WorldSnapshot invalid{{
        {.uuid = {8002}, .name = "One"},
        {.uuid = {8002}, .name = "Duplicate"},
    }};
    bool rejected = false;
    try {
        world.restore(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "Restore must reject duplicate persistent UUIDs.");
    expect(world.alive(original) && world.find({8001}) == original,
           "A rejected snapshot must leave the original World intact.");
}

} // namespace

int main() {
    test_structural_commands_are_deferred();
    test_spawn_then_destroy_in_one_flush_obeys_order();
    test_transform_access_is_safe();
    test_render_snapshot_is_sorted();
    test_clear_discards_live_and_queued_entities();
    test_persistent_identity_is_unique_and_queryable();
    test_snapshot_restore_preserves_authored_state_with_new_runtime_ids();
    test_invalid_snapshot_does_not_replace_live_world();

    if (failures == 0) {
        std::cout << "All world tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
