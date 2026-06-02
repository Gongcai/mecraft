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
        target.hasTarget = hit.hit;
        target.targetBlock = hit.hit ? hit.blockPos : glm::ivec3{};
        target.placeBlock = hit.hit ? hit.blockPos + hit.normal : glm::ivec3{};
        target.hitNormal = hit.hit ? hit.normal : glm::ivec3{};
    }
}

} // namespace ecs
