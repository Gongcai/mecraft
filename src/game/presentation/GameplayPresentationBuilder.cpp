#include "GameplayPresentationBuilder.h"
#include "../../ecs/GameplayScene.h"
#include "../../ecs/util/PlayerQuery.h"
#include "../../ecs/util/GameplayRuntimeContext.h"
#include "../../ecs/components/Components.h"
#include "../../world/block/Block.h"
#include "../modes/GameplayModeRules.h"
#include "../camera/CameraController.h"

#include <algorithm>
#include <cmath>

namespace {

float lerp(const float a, const float b, const float t) {
    return a + (b - a) * t;
}

glm::vec3 lerpVec3(const glm::vec3& a, const glm::vec3& b, const float t) {
    return a + (b - a) * t;
}

float angleDeltaDegrees(const float from, const float to) {
    float delta = std::fmod(to - from + 180.0f, 360.0f);
    if (delta < 0.0f) {
        delta += 360.0f;
    }
    return delta - 180.0f;
}

float lerpAngleDegrees(const float from, const float to, const float t) {
    return from + angleDeltaDegrees(from, to) * t;
}

glm::vec3 cameraFrontFromYawPitch(const float yaw, const float pitch) {
    const glm::vec3 front = {
        std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch)),
        std::sin(glm::radians(pitch)),
        std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch))
    };
    return glm::normalize(front);
}

glm::vec3 cameraRightFromFront(const glm::vec3& front) {
    return glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
}

} // namespace

GameplayPresentationSnapshot GameplayPresentationBuilder::build(
    ecs::GameplayRegistry& reg,
    const CameraController& cameraController,
    const IWorldView& worldView,
    const float interpolationAlpha) {

    GameplayPresentationSnapshot snap;
    auto& registry = reg.registry();
    const float alpha = std::clamp(interpolationAlpha, 0.0f, 1.0f);

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
            const auto* transformInterpolation = registry.try_get<ecs::TransformInterpolationComponent>(e);
            const auto* cameraInterpolation = registry.try_get<ecs::CameraInterpolationComponent>(e);

            glm::vec3 renderPosition = transform.position;
            float renderEyeHeight = transform.eyeHeight;
            if (transformInterpolation != nullptr && transformInterpolation->initialized) {
                renderPosition = lerpVec3(transformInterpolation->previousPosition, transform.position, alpha);
                renderEyeHeight = lerp(transformInterpolation->previousEyeHeight, transform.eyeHeight, alpha);
            }

            float renderYaw = cam.yaw;
            float renderPitch = cam.pitch;
            float renderFov = cam.fov;
            if (cameraInterpolation != nullptr && cameraInterpolation->initialized) {
                renderYaw = lerpAngleDegrees(cameraInterpolation->previousYaw, cam.yaw, alpha);
                renderPitch = lerp(cameraInterpolation->previousPitch, cam.pitch, alpha);
                renderFov = lerp(cameraInterpolation->previousFov, cam.fov, alpha);
            }
            const glm::vec3 renderFront = cameraFrontFromYawPitch(renderYaw, renderPitch);
            glm::vec3 renderRight = cameraRightFromFront(renderFront);

            renderCamera.setYawPitch(renderYaw, renderPitch);
            renderCamera.setFOV(renderFov);

            // Eye position with view bob offsets
            eyePosition = renderPosition +
                glm::vec3(0.0f, renderEyeHeight + viewBob.verticalOffset, 0.0f);

            // Apply horizontal bob
            renderRight.y = 0.0f;
            if (glm::length(renderRight) > 0.001f) {
                renderRight = glm::normalize(renderRight);
            } else {
                renderRight = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            eyePosition += renderRight * viewBob.horizontalOffset;

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
    snap.blockTarget.hitNormal = playerQuery.getTargetHitNormal();
    snap.blockBreak.active = playerQuery.hasBlockBreakProgress();
    snap.blockBreak.progress01 = playerQuery.getBlockBreakProgress();
    snap.blockBreak.blockPos = playerQuery.getBreakTargetBlock();
    snap.blockBreak.hitNormal = playerQuery.getBreakTargetHitNormal();

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
