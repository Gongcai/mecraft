#include "DropSystem.h"

#include <algorithm>
#include <vector>

#include "World.h"
#include "../ecs/SystemContext.h"
#include "../ecs/components/Components.h"
#include "../ecs/util/DropRuntimeState.h"
#include "../ecs/GameplayRegistry.h"
#include "../ecs/systems/item/ItemLifetimeSystem.h"
#include "../ecs/systems/item/ItemMergeSystem.h"
#include "../ecs/systems/item/ItemPhysicsSystem.h"
#include "../ecs/systems/item/ItemPickupSystem.h"
#include "../ecs/systems/item/ItemPlacementResolveSystem.h"
#include "../ecs/systems/item/ItemSpawnSystem.h"
#include "../player/Inventory.h"

namespace {

DropEntity readDropEntity(const entt::registry& registry, const entt::entity e) {
    const auto& idComp = registry.get<ecs::DropEntityIdComponent>(e);
    const auto& transform = registry.get<ecs::TransformComponent>(e);
    const auto& item = registry.get<ecs::ItemComponent>(e);
    const auto& velocity = registry.get<ecs::VelocityComponent>(e);
    const auto& bounds = registry.get<ecs::BoundsComponent>(e);
    const auto& lifetime = registry.get<ecs::LifetimeComponent>(e);
    const auto& spin = registry.get<ecs::SpinVisualComponent>(e);
    const auto& grounded = registry.get<ecs::GroundedStateComponent>(e);

    DropEntity drop;
    drop.id = idComp.dropId;
    drop.itemId = item.itemId;
    drop.position = transform.position;
    drop.velocity = velocity.velocity;
    drop.halfExtents = bounds.halfExtents;
    drop.yawRadians = spin.yawRadians;
    drop.spinSpeedRadians = spin.spinSpeedRadians;
    drop.ageSeconds = lifetime.ageSeconds;
    drop.lifeTimeSeconds = lifetime.lifeTimeSeconds;
    drop.stackCount = item.stackCount;
    drop.grounded = grounded.grounded;
    return drop;
}

std::vector<DropEntity> snapshotDrops(ecs::GameplayRegistry& registry) {
    auto& raw = registry.registry();
    auto view = raw.view<ecs::DropItemTag,
                         ecs::DropEntityIdComponent,
                         ecs::TransformComponent,
                         ecs::ItemComponent,
                         ecs::VelocityComponent,
                         ecs::BoundsComponent,
                         ecs::LifetimeComponent,
                         ecs::SpinVisualComponent,
                         ecs::GroundedStateComponent>();

    std::vector<DropEntity> drops;
    for (const entt::entity e : view) {
        drops.push_back(readDropEntity(raw, e));
    }

    std::sort(drops.begin(), drops.end(),
              [](const DropEntity& a, const DropEntity& b) {
                  return a.id < b.id;
              });
    return drops;
}

} // namespace

void DropSystem::bindRegistry(ecs::GameplayRegistry& registry) {
    m_registry = &registry;
    static_cast<void>(ecs::ensureDropRuntimeState(registry));
    m_dropCache.clear();
}

void DropSystem::bindServices(ecs::GameplayServices& services) {
    m_services = &services;
}

void DropSystem::spawnItemDrop(const ItemID itemId, const glm::ivec3& blockPos, const uint32_t stackCount) {
    if (m_registry == nullptr) {
        return;
    }

    ecs::ItemSpawnSystem::spawn(*m_registry, itemId, blockPos, stackCount);
}

void DropSystem::spawnBlockDrop(const BlockID blockId, const glm::ivec3& blockPos) {
    if (blockId == 0) {
        return;
    }
    const BlockDropEntry& drop = BlockDropTable::get(blockId);
    if (drop.dropItem == 0) {
        return;
    }
    spawnItemDrop(drop.dropItem, blockPos, drop.minCount);
}

void DropSystem::onBlockPlaced(const glm::ivec3& blockPos, const World& world) {
    if (m_registry == nullptr) {
        return;
    }
    ecs::ItemPlacementResolveSystem::update(*m_registry, world, blockPos);
}

void DropSystem::update(const float dt, const World& world) {
    if (m_registry == nullptr || dt <= 0.0f || m_services == nullptr) {
        return;
    }

    // Ensure the world service is available for physics
    ecs::OptionalService<World> savedWorld = m_services->world;
    m_services->world = const_cast<World*>(&world);

    ecs::SystemContext ctx{*m_registry, *m_services, dt, 0};

    ecs::ItemPhysicsSystem physicsSys;
    physicsSys.update(ctx);

    ecs::ItemMergeSystem mergeSys;
    mergeSys.update(ctx);

    ecs::ItemLifetimeSystem lifetimeSys;
    lifetimeSys.update(ctx);

    // Restore original world pointer
    m_services->world = savedWorld;
}

uint32_t DropSystem::collectNearbyDrops(const glm::vec3& position, const float radius, Inventory& inventory) {
    if (m_registry == nullptr) {
        return 0;
    }
    return ecs::ItemPickupSystem::pickup(*m_registry, position, radius, inventory);
}

void DropSystem::clear() {
    m_dropCache.clear();
    if (m_registry == nullptr) {
        return;
    }

    std::vector<entt::entity> entities;
    auto& raw = m_registry->registry();
    auto view = raw.view<ecs::DropItemTag>();
    for (const entt::entity e : view) {
        entities.push_back(e);
    }
    for (const entt::entity e : entities) {
        if (raw.valid(e)) {
            m_registry->destroy(e);
        }
    }

    if (m_registry->ctxHas<ecs::DropRuntimeState>()) {
        m_registry->ctxGet<ecs::DropRuntimeState>() = {};
    }
}

const std::vector<DropEntity>& DropSystem::getDrops() const {
    m_dropCache.clear();
    if (m_registry == nullptr) {
        return m_dropCache;
    }

    m_dropCache = snapshotDrops(*m_registry);
    return m_dropCache;
}

void DropSystem::restoreDrops(const std::vector<DropEntity>& drops) {
    if (m_registry == nullptr) {
        return;
    }

    auto& raw = m_registry->registry();
    for (const auto& drop : drops) {
        const entt::entity e = raw.create();
        raw.emplace<ecs::DropItemTag>(e);
        raw.emplace<ecs::TransformComponent>(e, drop.position);
        raw.emplace<ecs::VelocityComponent>(e, drop.velocity);
        raw.emplace<ecs::PhysicsBodyComponent>(e, ecs::PhysicsBodyComponent{
            {drop.position, drop.velocity, drop.halfExtents, glm::vec3(0.0f), 0.0f}
        });
        raw.emplace<ecs::ItemComponent>(e, drop.itemId, drop.stackCount);
        raw.emplace<ecs::LifetimeComponent>(e, drop.ageSeconds, drop.lifeTimeSeconds);
        raw.emplace<ecs::SpinVisualComponent>(e, drop.yawRadians, drop.spinSpeedRadians);
        raw.emplace<ecs::GroundedStateComponent>(e, ecs::GroundedStateComponent{drop.grounded});
    }
}
