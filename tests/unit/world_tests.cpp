#include <doctest/doctest.h>

#include "ic2d/world.hpp"

#include <cmath>
#include <stdexcept>
#include <string_view>

namespace {

TEST_CASE("structural commands are deferred") {
    ic2d::World world;
    const ic2d::EntityId entity = world.queue_spawn({.name = "Deferred"});
    CHECK_MESSAGE((!world.alive(entity)), "Queued spawn must not become visible before flush.");
    CHECK_MESSAGE((world.entity_count() == 0),
                  "Queued spawn must not change the live count before flush.");

    world.flush();
    CHECK_MESSAGE((world.alive(entity)), "Flushed spawn must become alive.");
    CHECK_MESSAGE((world.entity_count() == 1), "Flushed spawn must increment the live count.");

    world.queue_destroy(entity);
    CHECK_MESSAGE((world.alive(entity)),
                  "Queued destroy must not invalidate an entity before flush.");
    world.flush();
    CHECK_MESSAGE((!world.alive(entity)), "Flushed destroy must invalidate the entity.");
}

TEST_CASE("spawn then destroy in one flush obeys order") {
    ic2d::World world;
    const ic2d::EntityId entity = world.queue_spawn({.name = "Short lived"});
    world.queue_destroy(entity);
    world.flush();
    CHECK_MESSAGE((!world.alive(entity)),
                  "Command processing must preserve spawn-then-destroy order.");
    CHECK_MESSAGE((world.entity_count() == 0), "Spawn-then-destroy must leave no live entity.");
}

TEST_CASE("transform access is safe") {
    ic2d::World world;
    const ic2d::EntityId entity = world.queue_spawn({
        .name = "Mover",
        .transform = {.position = {10.0F, 20.0F, 30.0F}},
    });
    world.flush();

    const bool updated = world.set_transform(entity, {.position = {42.0F, 24.0F, 12.0F}});
    const auto transform = world.transform(entity);
    CHECK_MESSAGE((updated), "Live entity transform must be updateable.");
    CHECK_MESSAGE((transform.has_value()), "Live entity transform must be readable.");
    CHECK_MESSAGE((transform && std::abs(transform->position.x - 42.0F) < 0.001F),
                  "Transform update must persist.");
    CHECK_MESSAGE((transform && std::abs(transform->position.y - 24.0F) < 0.001F &&
                   std::abs(transform->position.z - 12.0F) < 0.001F),
                  "World transform must preserve elevation and depth.");
    CHECK_MESSAGE((!world.set_transform(ic2d::EntityId{9999}, {})),
                  "Unknown entity transform update must fail safely.");
}

TEST_CASE("render snapshot is sorted") {
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
    CHECK_MESSAGE((items.size() == 2), "All sprite entities must appear in the render snapshot.");
    CHECK_MESSAGE((items.size() == 2 && items.front().entity == background),
                  "Render snapshot must be sorted by layer.");
}

TEST_CASE("clear discards live and queued entities") {
    ic2d::World world;
    static_cast<void>(world.queue_spawn({.name = "Live"}));
    world.flush();
    const auto queued = world.queue_spawn({.name = "Queued"});
    world.clear();
    CHECK_MESSAGE((world.entity_count() == 0), "Clear must remove live entities.");
    world.flush();
    CHECK_MESSAGE((!world.alive(queued)), "Clear must discard queued commands.");
}

TEST_CASE("persistent identity is unique and queryable") {
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
    CHECK_MESSAGE((duplicate_rejected),
                  "Duplicate persistent UUIDs must be rejected before flush.");

    world.flush();
    CHECK_MESSAGE((world.uuid(authored) == ic2d::EntityUuid{9001}),
                  "An authored UUID must remain attached to its transient entity.");
    CHECK_MESSAGE((world.find({9001}) == authored),
                  "A persistent UUID must resolve to the current transient entity.");
    const auto generated_uuid = world.uuid(generated);
    CHECK_MESSAGE((generated_uuid.has_value() && static_cast<bool>(*generated_uuid)),
                  "World must allocate a non-zero UUID when one is not authored.");
    CHECK_MESSAGE((generated_uuid && world.find(*generated_uuid) == generated),
                  "Generated UUIDs must use the same lookup path as authored UUIDs.");
}

TEST_CASE("snapshot restore preserves authored state with new runtime ids") {
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
    CHECK_MESSAGE((snapshot.entities.size() == 2),
                  "A World snapshot must contain every live entity.");
    CHECK_MESSAGE((snapshot.entities.size() == 2 && snapshot.entities[0].uuid.value == 7001 &&
                   snapshot.entities[1].uuid.value == 7002),
                  "World snapshots must use deterministic persistent-UUID order.");

    static_cast<void>(source.set_transform(first, {.position = {99.0F, 99.0F, 99.0F}}));
    ic2d::World restored;
    static_cast<void>(restored.queue_spawn({.name = "State to replace"}));
    restored.flush();
    restored.restore(snapshot);

    const auto restored_first = restored.find({7001});
    const auto restored_second = restored.find({7002});
    CHECK_MESSAGE((restored.entity_count() == 2 && restored_first && restored_second),
                  "Restore must replace the World with entities addressable by stable UUID.");
    const auto first_transform =
        restored_first ? restored.transform(*restored_first) : std::nullopt;
    CHECK_MESSAGE((first_transform && std::abs(first_transform->position.x - 1.0F) < 0.001F),
                  "Snapshot data must not alias later source-World mutations.");
    const auto copied = restored.snapshot();
    CHECK_MESSAGE((copied.entities.size() == 2 && copied.entities[1].sprite &&
                   copied.entities[1].sprite->size.x == 20.0F &&
                   copied.entities[1].sprite->layer == 3),
                  "Restore must preserve optional sprite state through the World interface.");
}

TEST_CASE("invalid snapshot does not replace live world") {
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
    CHECK_MESSAGE((rejected), "Restore must reject duplicate persistent UUIDs.");
    CHECK_MESSAGE((world.alive(original) && world.find({8001}) == original),
                  "A rejected snapshot must leave the original World intact.");
}

} // namespace
