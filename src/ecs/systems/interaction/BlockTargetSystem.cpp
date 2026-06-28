#include "BlockTargetSystem.h"

#include "../../components/Components.h"
#include "../../../world/WorldRaycast.h"

namespace ecs {

namespace {
constexpr float kPickDistance = 6.0f;
} // namespace

void BlockTargetSystem::update(SystemContext& ctx) {
    if (!ctx.services.worldView) return;
    const auto& worldView = *ctx.services.worldView;
    auto& registry = ctx.registry;

    auto view = registry.view<LocalPlayerTag,
                              BlockActionIntentComponent,
                              TransformComponent,
                              CameraStateComponent,
                              BlockTargetComponent>();
    for (auto e : view) {
        auto& target = view.get<BlockTargetComponent>(e);
        const auto& transform = view.get<TransformComponent>(e);
        const auto& camera = view.get<CameraStateComponent>(e);

        const PhysicsInfo pickRay = {
            transform.position + glm::vec3(0.0f, transform.eyeHeight, 0.0f),
            camera.front
        };
        const RayHit hit = raycastWorldView(worldView, pickRay, kPickDistance);
        target.hasTarget = hit.hit && hit.kind == RayHitKind::Block;
        target.targetState = target.hasTarget
            ? worldView.getBlockState(hit.blockPos.x, hit.blockPos.y, hit.blockPos.z)
            : NULL_BLOCK_STATE;
        target.targetBlock = target.hasTarget ? hit.blockPos : glm::ivec3{};
        target.placeBlock = target.hasTarget ? hit.blockPos + hit.normal : glm::ivec3{};
        target.hitNormal = target.hasTarget ? hit.normal : glm::ivec3{};
        target.hitPosition = target.hasTarget ? hit.position : glm::vec3{};

        const RayHit fluidHit = raycastWorldView(worldView, pickRay, kPickDistance, RaycastFluidMode::Include);
        target.hasFluidTarget = fluidHit.hit && fluidHit.kind == RayHitKind::Fluid;
        target.fluidTargetState = target.hasFluidTarget
            ? worldView.getFluidState(fluidHit.blockPos.x, fluidHit.blockPos.y, fluidHit.blockPos.z)
            : NULL_BLOCK_STATE;
        target.fluidTargetBlock = target.hasFluidTarget ? fluidHit.blockPos : glm::ivec3{};
        target.fluidPlaceBlock = target.hasFluidTarget ? fluidHit.blockPos + fluidHit.normal : glm::ivec3{};
        target.fluidHitNormal = target.hasFluidTarget ? fluidHit.normal : glm::ivec3{};
        target.fluidHitPosition = target.hasFluidTarget ? fluidHit.position : glm::vec3{};
    }
}

} // namespace ecs
