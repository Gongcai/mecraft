#include "client/ClientEntityStore.h"
#include "ecs/components/Components.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::abort();
    }
}

static void testSpawnBeforeInitIsReplayed() {
    client::ClientEntityStore store;

    net::EntitySpawnMessage spawn;
    spawn.netId = 42;
    spawn.kind = net::EntityKind::Drop;
    spawn.position = glm::vec3(1.0f, 2.0f, 3.0f);
    spawn.velocity = glm::vec3(0.25f, 0.0f, 0.0f);
    spawn.itemId = 1;
    spawn.stackCount = 1;
    store.handleSpawn(spawn);

    net::EntitySnapshotMessage snapshot;
    snapshot.serverTick = 7;
    net::EntitySnapshotItem item;
    item.netId = 42;
    item.position = glm::vec3(4.0f, 5.0f, 6.0f);
    item.velocity = glm::vec3(0.0f, 0.5f, 0.0f);
    snapshot.entities.push_back(item);
    store.handleSnapshot(snapshot);

    entt::registry registry;
    store.init(registry, nullptr);

    require(store.remoteEntityCount() == 1, "pending spawn was not replayed");
    require(store.hasEntity(42), "spawned entity netId missing");

    auto view = registry.view<ecs::EntityNetIdComponent, ecs::TransformComponent, ecs::VelocityComponent>();
    require(view.begin() != view.end(), "spawned entity components missing");
    const auto entity = *view.begin();
    const auto& transform = registry.get<ecs::TransformComponent>(entity);
    const auto& velocity = registry.get<ecs::VelocityComponent>(entity);
    require(transform.position.x == 4.0f, "snapshot x was not applied");
    require(transform.position.y == 5.0f, "snapshot y was not applied");
    require(transform.position.z == 6.0f, "snapshot z was not applied");
    require(velocity.velocity.y == 0.5f, "snapshot velocity was not applied");
}

int main() {
    testSpawnBeforeInitIsReplayed();
    std::printf("All ClientEntityStore tests passed!\n");
    return 0;
}
