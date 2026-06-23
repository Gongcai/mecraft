#include "ItemPlacementResolveSystem.h"

#include "../../components/Components.h"
#include "../../util/DropPhysicsHelpers.h"
#include "../../../world/World.h"

namespace ecs {

void ItemPlacementResolveSystem::update(GameplayRegistry& registry,
                                        const World& world,
                                        const glm::ivec3& blockPos) {
    if (!drop_detail::hasCollisionBlock(world, blockPos.x, blockPos.y, blockPos.z)) {
        return;
    }

    auto view = registry.view<DropItemTag,
                              TransformComponent,
                              VelocityComponent,
                              BoundsComponent,
                              GroundedStateComponent>();
    for (const entt::entity e : view) {
        auto& transform = view.get<TransformComponent>(e);
        auto& velocity = view.get<VelocityComponent>(e);
        const auto& bounds = view.get<BoundsComponent>(e);
        auto& grounded = view.get<GroundedStateComponent>(e);

        if (!drop_detail::overlapsBlockCollision(world, transform.position, bounds.halfExtents, blockPos)) {
            continue;
        }

        const float baseY = drop_detail::collisionTopY(world, blockPos) + bounds.halfExtents.y +
                            drop_detail::kContactEpsilon;
        glm::vec3 resolvedPos = transform.position;
        resolvedPos.y = baseY;

        constexpr int kMaxLiftSteps = 8;
        int liftSteps = 0;
        while (liftSteps < kMaxLiftSteps && drop_detail::overlapsCollision(world, resolvedPos, bounds.halfExtents)) {
            resolvedPos.y += 1.0f;
            ++liftSteps;
        }

        transform.position = resolvedPos;
        velocity.velocity.y = 0.0f;
        grounded.grounded = true;
    }
}

} // namespace ecs
