#include "ItemMergeSystem.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "../../components/Components.h"
#include "../../util/DropRuntimeState.h"
#include "../../util/SimulationDistance.h"

namespace ecs {

namespace {
constexpr float kMergeRadius = 1.75f;
constexpr float kMergeRadiusSq = kMergeRadius * kMergeRadius;
constexpr float kMergeIntervalSeconds = 0.2f;
constexpr float kInvCellSize = 1.0f / kMergeRadius;

bool canMerge(const ItemComponent& a, const ItemComponent& b) {
    return a.itemId == b.itemId && a.stackCount > 0 && b.stackCount > 0;
}

struct IVec3Hash {
    size_t operator()(const glm::ivec3& v) const {
        size_t h = 0;
        h ^= std::hash<int>()(v.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(v.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

glm::ivec3 toCell(const glm::vec3& pos) {
    return {static_cast<int>(std::floor(pos.x * kInvCellSize)), static_cast<int>(std::floor(pos.y * kInvCellSize)),
            static_cast<int>(std::floor(pos.z * kInvCellSize))};
}
} // namespace

void ItemMergeSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    const float dt = ctx.dt;

    if (dt <= 0.0f) {
        return;
    }

    auto view = registry.view<DropItemTag, DropEntityIdComponent, TransformComponent, ItemComponent, VelocityComponent,
                              LifetimeComponent, GroundedStateComponent>();

    std::vector<entt::entity> entities;
    for (const entt::entity e : view) {
        if (!simulation::isEntityTicking(ctx, e)) {
            continue;
        }
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

    // Build spatial hash grid
    std::unordered_map<glm::ivec3, std::vector<entt::entity>, IVec3Hash> grid;
    grid.reserve(entities.size());
    for (const entt::entity e : entities) {
        const auto& pos = view.get<TransformComponent>(e).position;
        grid[toCell(pos)].push_back(e);
    }

    std::vector<entt::entity> removed;
    for (const entt::entity baseEntity : entities) {
        auto& baseItem = view.get<ItemComponent>(baseEntity);
        if (baseItem.stackCount == 0) {
            continue;
        }

        auto& baseTransform = view.get<TransformComponent>(baseEntity);
        auto& baseVelocity = view.get<VelocityComponent>(baseEntity);
        auto& baseLifetime = view.get<LifetimeComponent>(baseEntity);
        auto& baseGrounded = view.get<GroundedStateComponent>(baseEntity);
        const glm::ivec3 baseCell = toCell(baseTransform.position);

        // Check 3x3x3 neighborhood
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    auto it = grid.find(baseCell + glm::ivec3(dx, dy, dz));
                    if (it == grid.end()) {
                        continue;
                    }
                    for (const entt::entity candidateEntity : it->second) {
                        if (candidateEntity == baseEntity) {
                            continue;
                        }
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

                        baseTransform.position =
                            (baseTransform.position * baseWeight + candidateTransform.position * candidateWeight) *
                            invTotal;
                        baseVelocity.velocity =
                            (baseVelocity.velocity * baseWeight + candidateVelocity.velocity * candidateWeight) *
                            invTotal;
                        baseLifetime.ageSeconds = std::min(baseLifetime.ageSeconds, candidateLifetime.ageSeconds);
                        baseGrounded.grounded = baseGrounded.grounded || candidateGrounded.grounded;
                        baseItem.stackCount = totalCount;
                        candidateItem.stackCount = 0;
                        removed.push_back(candidateEntity);
                    }
                }
            }
        }
    }

    for (const entt::entity e : removed) {
        if (registry.registry().valid(e)) {
            registry.destroy(e);
        }
    }
}

} // namespace ecs
