#include "ItemMergeSystem.h"

#include <algorithm>
#include <vector>

#include "../../components/Components.h"
#include "../../util/DropRuntimeState.h"

namespace ecs {

namespace {
constexpr float kMergeRadius = 1.75f;
constexpr float kMergeRadiusSq = kMergeRadius * kMergeRadius;
constexpr float kMergeIntervalSeconds = 0.2f;

bool canMerge(const ItemComponent& a, const ItemComponent& b) {
    return a.itemId == b.itemId && a.stackCount > 0 && b.stackCount > 0;
}
}

void ItemMergeSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    const float dt = ctx.dt;

    if (dt <= 0.0f) {
        return;
    }

    auto view = registry.view<DropItemTag,
                              DropEntityIdComponent,
                              TransformComponent,
                              ItemComponent,
                              VelocityComponent,
                              LifetimeComponent,
                              GroundedStateComponent>();

    std::vector<entt::entity> entities;
    for (const entt::entity e : view) {
        entities.push_back(e);
    }

    if (entities.size() < 2) {
        return;
    }

    auto& state = ensureDropRuntimeState(registry);
    state.mergeAccumulator += dt;
    if (state.mergeAccumulator < kMergeIntervalSeconds) {
        return;
    }
    state.mergeAccumulator = 0.0f;

    std::sort(entities.begin(), entities.end(),
              [&](const entt::entity a, const entt::entity b) {
                  return view.get<DropEntityIdComponent>(a).dropId < view.get<DropEntityIdComponent>(b).dropId;
              });

    std::vector<entt::entity> removed;
    for (size_t i = 0; i < entities.size(); ++i) {
        const entt::entity baseEntity = entities[i];
        auto& baseItem = view.get<ItemComponent>(baseEntity);
        if (baseItem.stackCount == 0) {
            continue;
        }

        auto& baseTransform = view.get<TransformComponent>(baseEntity);
        auto& baseVelocity = view.get<VelocityComponent>(baseEntity);
        auto& baseLifetime = view.get<LifetimeComponent>(baseEntity);
        auto& baseGrounded = view.get<GroundedStateComponent>(baseEntity);

        for (size_t j = i + 1; j < entities.size(); ++j) {
            const entt::entity candidateEntity = entities[j];
            auto& candidateItem = view.get<ItemComponent>(candidateEntity);
            if (!canMerge(baseItem, candidateItem)) {
                continue;
            }

            const auto& candidateTransform = view.get<TransformComponent>(candidateEntity);
            const glm::vec3 delta = baseTransform.position - candidateTransform.position;
            if (glm::dot(delta, delta) > kMergeRadiusSq) {
                continue;
            }

            auto& candidateVelocity = view.get<VelocityComponent>(candidateEntity);
            const auto& candidateLifetime = view.get<LifetimeComponent>(candidateEntity);
            const auto& candidateGrounded = view.get<GroundedStateComponent>(candidateEntity);

            const uint32_t totalCount = baseItem.stackCount + candidateItem.stackCount;
            if (totalCount == 0) {
                continue;
            }

            const float baseWeight = static_cast<float>(baseItem.stackCount);
            const float candidateWeight = static_cast<float>(candidateItem.stackCount);
            const float invTotal = 1.0f / static_cast<float>(totalCount);

            baseTransform.position = (baseTransform.position * baseWeight + candidateTransform.position * candidateWeight) * invTotal;
            baseVelocity.velocity = (baseVelocity.velocity * baseWeight + candidateVelocity.velocity * candidateWeight) * invTotal;
            baseLifetime.ageSeconds = std::min(baseLifetime.ageSeconds, candidateLifetime.ageSeconds);
            baseGrounded.grounded = baseGrounded.grounded || candidateGrounded.grounded;
            baseItem.stackCount = totalCount;
            candidateItem.stackCount = 0;
            removed.push_back(candidateEntity);
        }
    }

    for (const entt::entity e : removed) {
        if (registry.registry().valid(e)) {
            registry.destroy(e);
        }
    }
}

} // namespace ecs
