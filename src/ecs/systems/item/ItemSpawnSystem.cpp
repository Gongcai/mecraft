#include "ItemSpawnSystem.h"

#include <algorithm>
#include <random>

#include <glm/gtc/constants.hpp>

#include "../../components/Components.h"
#include "../../entity/EntityFactory.h"
#include "../../util/DropSpawnEventBuffer.h"

namespace ecs {

namespace {

bool tryMergeDropAtSpawn(GameplayRegistry& registry, const ItemID itemId, const glm::vec3& spawnPos,
                         const uint32_t stackCount) {
    auto view = registry.view<DropItemTag, TransformComponent, ItemComponent, LifetimeComponent>();
    entt::entity bestMatch = entt::null;
    constexpr float kMergeRadius = 1.75f;
    constexpr float kMergeRadiusSq = kMergeRadius * kMergeRadius;
    float bestDistSq = kMergeRadiusSq;

    for (const entt::entity e : view) {
        auto& item = view.get<ItemComponent>(e);
        if (item.itemId != itemId || item.stackCount == 0) {
            continue;
        }

        auto& transform = view.get<TransformComponent>(e);
        const glm::vec3 delta = transform.position - spawnPos;
        const float distSq = glm::dot(delta, delta);
        if (distSq > bestDistSq) {
            continue;
        }

        bestDistSq = distSq;
        bestMatch = e;
    }

    if (bestMatch == entt::null) {
        return false;
    }

    auto& item = view.get<ItemComponent>(bestMatch);
    auto& transform = view.get<TransformComponent>(bestMatch);
    auto& lifetime = view.get<LifetimeComponent>(bestMatch);
    item.stackCount += stackCount;
    transform.position = (transform.position + spawnPos) * 0.5f;
    lifetime.ageSeconds = std::max(0.0f, lifetime.ageSeconds - 1.0f);
    return true;
}

} // namespace

void ItemSpawnSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;

    if (!registry.ctxHas<DropSpawnEventBus>())
        return;
    auto& dropBus = registry.ctxGet<DropSpawnEventBus>();

    for (const auto& req : dropBus.events) {
        if (req.blockId == 0)
            continue;
        const BlockDropEntry& drop = BlockDropTable::get(req.blockId);
        if (drop.dropItem == 0)
            continue;
        spawn(registry, drop.dropItem, req.blockPos, drop.minCount);
    }
    dropBus.clear();
}

void ItemSpawnSystem::spawn(GameplayRegistry& registry, const ItemID itemId, const glm::ivec3& blockPos,
                            const uint32_t stackCount) {
    if (itemId == 0 || stackCount == 0) {
        return;
    }

    const glm::vec3 spawnPos = glm::vec3(blockPos) + glm::vec3(0.5f, 0.42f, 0.5f);
    spawnAtPosition(registry, itemId, spawnPos, stackCount);
}

void ItemSpawnSystem::spawnAtPosition(GameplayRegistry& registry, const ItemID itemId, const glm::vec3& spawnPos,
                                      const uint32_t stackCount) {
    if (itemId == 0 || stackCount == 0) {
        return;
    }

    if (tryMergeDropAtSpawn(registry, itemId, spawnPos, stackCount)) {
        return;
    }

    static std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> yawDist(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> spinDist(1.8f, 3.2f);
    std::uniform_real_distribution<float> horizontalDist(-1.1f, 1.1f);
    std::uniform_real_distribution<float> upwardDist(2.4f, 3.3f);

    ItemDropSpawnParams params;
    params.itemId = itemId;
    params.stackCount = stackCount;
    params.position = spawnPos;
    params.velocity = glm::vec3(horizontalDist(rng), upwardDist(rng), horizontalDist(rng));
    params.halfExtents = glm::vec3(0.175f);
    params.yawRadians = yawDist(rng);
    params.spinSpeedRadians = spinDist(rng);
    params.ageSeconds = 0.0f;
    params.lifeTimeSeconds = 30.0f;
    params.grounded = false;
    EntityFactory::createItemDrop(registry, params);
}

} // namespace ecs
