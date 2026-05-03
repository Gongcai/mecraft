#include "SteveSyncSystem.h"

#include "../../components/Components.h"
#include "../../util/InputFrameState.h"
#include "../../../core/CameraController.h"

namespace ecs {

void SteveSyncSystem::update(SystemContext& ctx) {
    if (!ctx.services.cameraController) return;
    auto& registry = ctx.registry;
    auto& cameraController = *ctx.services.cameraController;

    auto& reg = registry.registry();

    // 1. Handle view mode toggle from input
    if (registry.ctxHas<InputFrameState>()) {
        auto& frame = registry.ctxGet<InputFrameState>();
        if (frame.toggleViewMode) {
            cameraController.toggleViewMode();
        }
    }

    // 2. Sync player position and camera state to Steve entity
    auto playerView = reg.view<LocalPlayerTag, TransformComponent, CameraStateComponent>();
    if (playerView.begin() == playerView.end()) return;

    auto steveView = reg.view<SteveTag, TransformComponent>();
    if (steveView.begin() == steveView.end()) return;

    for (auto playerEntity : playerView) {
        auto& playerTransform = reg.get<TransformComponent>(playerEntity);
        auto& playerCam = reg.get<CameraStateComponent>(playerEntity);

        for (auto steveEntity : steveView) {
            auto& steveTransform = reg.get<TransformComponent>(steveEntity);
            steveTransform.position = playerTransform.position;

            // Sync camera state so the head follows the view direction
            if (reg.all_of<CameraStateComponent>(steveEntity)) {
                auto& steveCam = reg.get<CameraStateComponent>(steveEntity);
                steveCam.yaw   = playerCam.yaw;
                steveCam.pitch = playerCam.pitch;
            }
        }
    }
}

} // namespace ecs
