#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/GameplayServices.h"
#include "../src/ecs/SystemContext.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/components/NetworkComponents.h"
#include "../src/ecs/systems/combat/DeathSystem.h"
#include "../src/item/Item.h"

namespace {

int fail(const char* message) {
    std::cerr << "[death_system_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

uint32_t dropStackCount(ecs::GameplayRegistry& registry, const ItemID itemId) {
    uint32_t total = 0;
    auto view = registry.registry().view<ecs::DropItemTag,
                                         ecs::ItemComponent,
                                         ecs::NetworkSyncTag>();
    for (const entt::entity entity : view) {
        const auto& item = view.get<ecs::ItemComponent>(entity);
        if (item.itemId == itemId) {
            total += item.stackCount;
        }
    }
    return total;
}

} // namespace

int testLocalMobDeathSpawnsDropsAndDestroysEntity() {
    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    ecs::SystemContext context{registry, services, 1.0f / 20.0f, 1};

    entt::registry& raw = registry.registry();
    const entt::entity mob = raw.create();
    raw.emplace<ecs::MobTag>(mob);
    raw.emplace<ecs::TransformComponent>(mob, glm::vec3(3.0f, 64.0f, 5.0f), 1.62f);
    raw.emplace<ecs::HealthComponent>(mob, 0, 20);

    auto& drops = raw.emplace<ecs::DropTableComponent>(mob);
    drops.entries.push_back(ecs::DropTableEntry{ItemIds::COAL, 1u, 1u});
    drops.entries.push_back(ecs::DropTableEntry{ItemIds::APPLE, 2u, 4u});

    ecs::DeathSystem deathSystem;
    deathSystem.update(context);

    if (raw.valid(mob)) {
        return fail("dead mob should be destroyed");
    }

    const uint32_t coalCount = dropStackCount(registry, ItemIds::COAL);
    if (coalCount != 1) {
        return fail("death loot should spawn fixed-count entries");
    }

    const uint32_t appleCount = dropStackCount(registry, ItemIds::APPLE);
    if (appleCount < 2 || appleCount > 4) {
        return fail("death loot should roll ranged entries within min/max");
    }

    return EXIT_SUCCESS;
}

int testNetworkMobDeathQueuesFinalDespawnOnce() {
    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    ecs::SystemContext context{registry, services, 1.0f / 20.0f, 1};

    entt::registry& raw = registry.registry();
    const entt::entity mob = raw.create();
    raw.emplace<ecs::MobTag>(mob);
    raw.emplace<ecs::TransformComponent>(mob, glm::vec3(3.0f, 64.0f, 5.0f), 1.62f);
    raw.emplace<ecs::HealthComponent>(mob, 0, 20);
    raw.emplace<ecs::NetworkSyncTag>(mob);
    raw.emplace<ecs::EntityNetIdComponent>(mob, ecs::EntityNetId{42});
    raw.emplace<ecs::DeathEffectComponent>(mob, BlockIds::ROSE, 28, "mob.zombie.death", 1.0f);
    raw.emplace<ecs::DropTableComponent>(mob, ItemIds::COAL, 1u, 1u);

    const entt::entity child = raw.create();
    raw.emplace<ecs::ChildrenComponent>(mob).children.push_back(child);
    raw.emplace<ecs::ParentComponent>(child, mob);

    ecs::DeathSystem deathSystem;
    deathSystem.update(context);

    if (!raw.valid(mob)) {
        return fail("networked dead mob should remain valid until the server sends despawn");
    }
    if (!raw.valid(child)) {
        return fail("networked dead mob children should remain valid until final server cleanup");
    }
    if (!raw.all_of<ecs::PendingNetworkDespawnTag>(mob)) {
        return fail("networked dead mob should be marked for final network despawn");
    }
    const auto* impact = raw.try_get<ecs::EntityImpactComponent>(mob);
    if (impact == nullptr || impact->particleBlock != BlockIds::ROSE || impact->particleCount != 28) {
        return fail("networked dead mob should carry final death impact data");
    }
    if (dropStackCount(registry, ItemIds::COAL) != 1) {
        return fail("networked death should spawn loot exactly once on first update");
    }

    deathSystem.update(context);
    if (dropStackCount(registry, ItemIds::COAL) != 1) {
        return fail("pending network death should not spawn duplicate loot");
    }

    return EXIT_SUCCESS;
}

int main() {
    ItemRegistry::init();

    if (const int result = testLocalMobDeathSpawnsDropsAndDestroysEntity(); result != EXIT_SUCCESS) {
        return result;
    }
    if (const int result = testNetworkMobDeathQueuesFinalDespawnOnce(); result != EXIT_SUCCESS) {
        return result;
    }

    std::cout << "[death_system_test] PASS\n";
    return EXIT_SUCCESS;
}
