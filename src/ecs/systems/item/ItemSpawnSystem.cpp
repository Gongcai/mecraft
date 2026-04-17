#include "ItemSpawnSystem.h"

#include <algorithm>
#include <random>

#include <glm/gtc/constants.hpp>

#include "../../components/Components.h"
#include "../../util/DropRuntimeState.h"

namespace ecs {

namespace {

void ensureDropComponents(entt::registry& registry, const entt::entity e) {
    if (!registry.all_of<DropItemTag>(e)) {
        registry.emplace<DropItemTag>(e);
    }
    if (!registry.all_of<DropEntityIdComponent>(e)) {
        registry.emplace<DropEntityIdComponent>(e);
    }
    if (!registry.all_of<TransformComponent>(e)) {
        registry.emplace<TransformComponent>(e);
    }
    if (!registry.all_of<ItemComponent>(e)) {
        registry.emplace<ItemComponent>(e);
    }
    if (!registry.all_of<VelocityComponent>(e)) {
        registry.emplace<VelocityComponent>(e);
    }
    if (!registry.all_of<BoundsComponent>(e)) {
        registry.emplace<BoundsComponent>(e);
    }
    if (!registry.all_of<LifetimeComponent>(e)) {
        registry.emplace<LifetimeComponent>(e);
    }
    if (!registry.all_of<SpinVisualComponent>(e)) {
        registry.emplace<SpinVisualComponent>(e);
    }
    if (!registry.all_of<GroundedStateComponent>(e)) {
        registry.emplace<GroundedStateComponent>(e);
    }
}

bool tryMergeDropAtSpawn(GameplayRegistry& registry,
                         const ItemID itemId,
                         const glm::vec3& spawnPos,
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

void ItemSpawnSystem::update(GameplayRegistry& registry) {
    static_cast<void>(registry);
}

void ItemSpawnSystem::spawn(GameplayRegistry& registry,
                            const ItemID itemId,
                            const glm::ivec3& blockPos,
                            const uint32_t stackCount) {
    if (itemId == 0 || stackCount == 0) {
        return;
    }

    const glm::vec3 spawnPos = glm::vec3(blockPos) + glm::vec3(0.5f, 0.42f, 0.5f);
    if (tryMergeDropAtSpawn(registry, itemId, spawnPos, stackCount)) {
        return;
    }

    auto& raw = registry.registry();
    auto& state = ensureDropRuntimeState(registry);

    static std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> yawDist(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> spinDist(1.8f, 3.2f);
    std::uniform_real_distribution<float> horizontalDist(-1.1f, 1.1f);
    std::uniform_real_distribution<float> upwardDist(2.4f, 3.3f);

    const entt::entity e = raw.create();
    ensureDropComponents(raw, e);

    raw.get<DropEntityIdComponent>(e).dropId = state.nextId++;
    auto& transform = raw.get<TransformComponent>(e);
    transform.position = spawnPos;
    transform.eyeHeight = 0.0f;

    auto& item = raw.get<ItemComponent>(e);
    item.itemId = itemId;
    item.stackCount = stackCount;

    raw.get<VelocityComponent>(e).velocity = glm::vec3(horizontalDist(rng), upwardDist(rng), horizontalDist(rng));
    raw.get<BoundsComponent>(e).halfExtents = glm::vec3(0.175f);

    auto& spin = raw.get<SpinVisualComponent>(e);
    spin.yawRadians = yawDist(rng);
    spin.spinSpeedRadians = spinDist(rng);

    auto& lifetime = raw.get<LifetimeComponent>(e);
    lifetime.ageSeconds = 0.0f;
    lifetime.lifeTimeSeconds = 30.0f;

    raw.get<GroundedStateComponent>(e).grounded = false;
}

} // namespace ecs
