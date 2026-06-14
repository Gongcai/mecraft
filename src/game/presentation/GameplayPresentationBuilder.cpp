#include "GameplayPresentationBuilder.h"
#include "../../ecs/GameplayScene.h"
#include "../../ecs/util/PlayerQuery.h"
#include "../../ecs/util/GameplayRuntimeContext.h"
#include "../../ecs/components/Components.h"
#include "../../world/block/Block.h"
#include "../modes/GameplayModeRules.h"
#include "../camera/CameraController.h"

GameplayPresentationSnapshot GameplayPresentationBuilder::build(
    ecs::GameplayRegistry& reg,
    const CameraController& cameraController,
    const IWorldView& worldView) {

    GameplayPresentationSnapshot snap;
    auto& registry = reg.registry();

    // Fall roll radians
    {
        auto view = registry.view<ecs::LocalPlayerTag, ecs::FallRollComponent>();
        for (auto e : view) {
            snap.fallRollRadians = registry.get<ecs::FallRollComponent>(e).currentRadians;
        }
    }

    // Camera state from ECS
    {
        Camera renderCamera;
        glm::vec3 eyePosition(0.0f);

        auto camView = registry.view<ecs::LocalPlayerTag, ecs::CameraStateComponent>();
        auto transformView = registry.view<ecs::LocalPlayerTag, ecs::TransformComponent>();
        auto viewBobView = registry.view<ecs::LocalPlayerTag, ecs::ViewBobComponent>();

        for (auto e : camView) {
            const auto& cam = camView.get<ecs::CameraStateComponent>(e);
            const auto& transform = transformView.get<ecs::TransformComponent>(e);
            const auto& viewBob = viewBobView.get<ecs::ViewBobComponent>(e);

            renderCamera.setYawPitch(cam.yaw, cam.pitch);
            renderCamera.setFOV(cam.fov);

            // Eye position with view bob offsets
            eyePosition = transform.position +
                glm::vec3(0.0f, transform.eyeHeight + viewBob.verticalOffset, 0.0f);

            // Apply horizontal bob
            glm::vec3 right = cam.right;
            right.y = 0.0f;
            if (glm::length(right) > 0.001f) {
                right = glm::normalize(right);
            } else {
                right = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            eyePosition += right * viewBob.horizontalOffset;

            renderCamera.setPosition(eyePosition);
            break;
        }

        // Compute final camera (first/third person)
        snap.renderCamera = cameraController.computeRenderCamera(renderCamera, eyePosition, worldView);
        snap.eyePosition = eyePosition;
        snap.shouldRenderPlayerModel = cameraController.shouldRenderPlayerModel();
        snap.renderLocalPlayerModel = snap.shouldRenderPlayerModel;
    }

    // Player state via PlayerQuery
    ecs::PlayerQuery playerQuery(reg);
    snap.eyeInWater = playerQuery.isEyesInWater();

    // Held block light
    {
        const BlockID heldBlock = playerQuery.getInventory().getSelectedBlock();
        snap.heldBlockLightLevel = BlockRegistry::getLightLevelFast(heldBlock);
    }

    // Block interaction
    snap.blockTarget.hasTarget = playerQuery.hasTargetBlock();
    snap.blockTarget.targetBlock = playerQuery.getTargetBlock();
    snap.blockBreak.active = playerQuery.hasBlockBreakProgress();
    snap.blockBreak.progress01 = playerQuery.getBlockBreakProgress();
    snap.blockBreak.blockPos = playerQuery.getBreakTargetBlock();

    // Held item motion
    snap.heldItemMotion.moving = playerQuery.isMoving();
    snap.heldItemMotion.sprinting = playerQuery.isSprinting();
    snap.heldItemMotion.bobFrequency = playerQuery.getEyeBobFrequency();
    snap.heldItemMotion.bobPhaseOffset = playerQuery.getEyeBobPhaseOffset();
    snap.heldItemMotion.cameraYawDegrees = playerQuery.getCameraYaw();
    snap.heldItemMotion.cameraPitchDegrees = playerQuery.getCameraPitch();
    {
        auto runtimeView = registry.view<ecs::LocalPlayerTag, ecs::BlockInteractionRuntimeComponent>();
        for (auto e : runtimeView) {
            snap.heldItemSwingSequence =
                runtimeView.get<ecs::BlockInteractionRuntimeComponent>(e).heldItemSwingSequence;
            break;
        }
    }

    // Inventory reference
    snap.inventory = &playerQuery.getInventory();

    // Player stats for HUD
    snap.playerStats.health = playerQuery.getHealth();
    snap.playerStats.maxHealth = playerQuery.getMaxHealth();
    snap.playerStats.armor = playerQuery.getArmor();
    snap.playerStats.maxArmor = playerQuery.getMaxArmor();
    snap.playerStats.food = playerQuery.getFood();
    snap.playerStats.maxFood = playerQuery.getMaxFood();
    snap.playerStats.isDead = snap.playerStats.health <= 0;

    // Check gameplay mode for survival stats visibility
    if (reg.ctxHas<ecs::GameplayRuntimeContext>()) {
        snap.playerStats.showSurvivalStats =
            reg.ctxGet<ecs::GameplayRuntimeContext>().gameplayMode != GameplayMode::Creative;
    }
    if (!snap.playerStats.showSurvivalStats) {
        snap.playerStats.isDead = false;
    }

    return snap;
}
