#include "DropCollectionBridgeSystem.h"

#include "../../components/Components.h"
#include "ItemLifetimeSystem.h"
#include "ItemMergeSystem.h"
#include "ItemPhysicsSystem.h"
#include "ItemPickupSystem.h"
#include "ItemSpawnSystem.h"
#include "../../../world/World.h"

namespace ecs {

namespace {
constexpr float kDropCollectRadius = 1.35f;
}

void DropCollectionBridgeSystem::update(GameplayRegistry& registry,
                                        DropSystem& dropSystem,
                                        const World& world,
                                        const float dt) {
    static_cast<void>(dropSystem);

    ItemSpawnSystem::update(registry);
    ItemPhysicsSystem::update(registry, world, dt);
    ItemMergeSystem::update(registry, dt);

    auto view = registry.view<LocalPlayerTag, TransformComponent, InventoryDataComponent>();
    for (auto e : view) {
        const auto& transform = view.get<TransformComponent>(e);
        auto& inventoryData = view.get<InventoryDataComponent>(e);
        static_cast<void>(ItemPickupSystem::update(registry,
                                                   transform.position,
                                                   kDropCollectRadius,
                                                   inventoryData.inventory));
        break;
    }

    ItemLifetimeSystem::update(registry, dt);
}

} // namespace ecs
