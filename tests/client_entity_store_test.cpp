#include "client/ClientEntityStore.h"
#include "ecs/components/Components.h"
#include "ecs/GameplayRegistry.h"
#include "ecs/GameplayServices.h"
#include "ecs/SystemContext.h"
#include "ecs/systems/particle/ParticleSpawnSystem.h"
#include "ecs/util/ParticleEventBuffer.h"
#include "ecs/util/ProjectileDefinitions.h"

#include <cassert>
#include <cmath>
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

static void testMobSpawnCreatesZombieReplica() {
    client::ClientEntityStore store;
    ecs::GameplayRegistry registry;
    store.init(registry, nullptr);

    net::EntitySpawnMessage spawn;
    spawn.netId = 77;
    spawn.kind = net::EntityKind::Mob;
    spawn.position = glm::vec3(2.0f, 64.0f, -3.0f);
    spawn.velocity = glm::vec3(0.0f, 0.0f, 0.25f);
    spawn.yaw = 35.0f;
    store.handleSpawn(spawn);

    require(store.remoteEntityCount() == 1, "mob spawn was not tracked");
    require(store.hasEntity(77), "mob netId missing");

    auto& raw = registry.registry();
    auto view = raw.view<ecs::MobTag,
                         ecs::ChildrenComponent,
                         ecs::MobAIComponent,
                         ecs::VelocityComponent,
                         ecs::EntityNetIdComponent>();
    require(view.begin() != view.end(), "mob replica components missing");
    const entt::entity mob = *view.begin();
    require(raw.get<ecs::ChildrenComponent>(mob).children.size() == 1, "mob replica did not create hierarchy");
    require(!raw.all_of<ecs::MoveIntentComponent>(mob), "mob replica should not run local AI movement");
    require(!raw.all_of<ecs::HealthComponent>(mob), "mob replica should not be client-authoritative health");

    net::EntitySnapshotMessage snapshot;
    snapshot.serverTick = 12;
    net::EntitySnapshotItem item;
    item.netId = 77;
    item.position = glm::vec3(3.0f, 65.0f, -4.0f);
    item.velocity = glm::vec3(0.0f, 0.0f, 0.5f);
    item.yaw = 90.0f;
    snapshot.entities.push_back(item);
    store.handleSnapshot(snapshot);

    require(raw.get<ecs::TransformComponent>(mob).position.x == 3.0f, "mob snapshot position was not applied");
    require(raw.get<ecs::VelocityComponent>(mob).velocity.z == 0.5f, "mob snapshot velocity was not applied");
    require(raw.get<ecs::MobAIComponent>(mob).yaw == 90.0f, "mob snapshot yaw was not applied");
}

static void testProjectileSpawnCreatesAppleReplica() {
    client::ClientEntityStore store;
    ecs::GameplayRegistry registry;
    store.init(registry, nullptr);

    net::EntitySpawnMessage spawn;
    spawn.netId = 91;
    spawn.kind = net::EntityKind::Projectile;
    spawn.position = glm::vec3(1.0f, 66.0f, 2.0f);
    spawn.velocity = glm::vec3(3.0f, 0.5f, 0.0f);
    spawn.yaw = 0.25f;
    spawn.itemId = static_cast<uint16_t>(ItemIds::APPLE);
    spawn.stackCount = 1;
    store.handleSpawn(spawn);

    ecs::ProjectileDefinition appleDefinition;
    require(ecs::getThrowableProjectileDefinition(ItemIds::APPLE, appleDefinition),
            "apple should have a throwable projectile definition");

    require(store.remoteEntityCount() == 1, "projectile spawn was not tracked");
    require(store.hasEntity(91), "projectile netId missing");

    auto& raw = registry.registry();
    auto view = raw.view<ecs::ProjectileTag,
                         ecs::ProjectileComponent,
                         ecs::ItemComponent,
                         ecs::SpinVisualComponent,
                         ecs::EntityNetIdComponent>();
    require(view.begin() != view.end(), "projectile replica components missing");
    const entt::entity projectile = *view.begin();
    require(raw.get<ecs::ItemComponent>(projectile).itemId == ItemIds::APPLE,
            "projectile replica should use apple item texture");
    const auto& projectileComponent = raw.get<ecs::ProjectileComponent>(projectile);
    require(projectileComponent.damage == appleDefinition.damage,
            "projectile replica damage should come from item projectile definition");
    require(std::fabs(projectileComponent.gravity - appleDefinition.gravity) < 0.001f,
            "projectile replica gravity should come from item projectile definition");
    require(projectileComponent.entityImpactParticleBlock == appleDefinition.entityImpactParticleBlock,
            "projectile replica impact particles should come from item projectile definition");
    require(std::fabs(raw.get<ecs::BoundsComponent>(projectile).halfExtents.x -
                      appleDefinition.boundsHalfExtent) < 0.001f,
            "projectile replica bounds should come from item projectile definition");
    require(!raw.all_of<ecs::DropItemTag>(projectile),
            "projectile replica should not be collectable as a drop");

    net::EntitySnapshotMessage snapshot;
    snapshot.serverTick = 13;
    net::EntitySnapshotItem item;
    item.netId = 91;
    item.position = glm::vec3(2.0f, 66.5f, 3.0f);
    item.velocity = glm::vec3(4.0f, 0.25f, 0.0f);
    item.yaw = 0.75f;
    snapshot.entities.push_back(item);
    store.handleSnapshot(snapshot);

    require(raw.get<ecs::TransformComponent>(projectile).position.x == 2.0f,
            "projectile snapshot position was not applied");
    require(raw.get<ecs::VelocityComponent>(projectile).velocity.x == 4.0f,
            "projectile snapshot velocity was not applied");
    require(raw.get<ecs::SpinVisualComponent>(projectile).yawRadians == 0.75f,
            "projectile snapshot yaw was not applied");

    net::EntityDespawnMessage despawn;
    despawn.netId = 91;
    store.handleDespawn(despawn);

    require(!store.hasEntity(91), "projectile despawn should stop tracking net id");
    require(registry.ctxHas<ecs::ParticleEventBus>(),
            "projectile despawn should queue an impact particle event");
    auto& particleBus = registry.ctxGet<ecs::ParticleEventBus>();
    require(particleBus.size() == 1, "projectile despawn should queue exactly one impact event");
    require(particleBus.peek().front().useWorldPos,
            "projectile impact event should use last known world position");
    require(particleBus.peek().front().blockType != 0,
            "projectile impact event should have a particle texture block");

    ecs::GameplayServices services;
    ecs::SystemContext ctx{registry, services, 1.0f / 60.0f, 0};
    ecs::ParticleSpawnSystem particleSpawn;
    particleSpawn.update(ctx);

    auto particleView = raw.view<ecs::ParticleTag,
                                 ecs::ParticleComponent,
                                 ecs::TransformComponent,
                                 ecs::VelocityComponent>();
    require(particleView.size_hint() > 0,
            "projectile impact event should spawn visible particles");
    require(particleBus.empty(), "particle spawn system should drain impact events");
}

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();

    testSpawnBeforeInitIsReplayed();
    testMobSpawnCreatesZombieReplica();
    testProjectileSpawnCreatesAppleReplica();
    std::printf("All ClientEntityStore tests passed!\n");
    return 0;
}
