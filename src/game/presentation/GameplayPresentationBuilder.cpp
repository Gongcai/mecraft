#include "GameplayPresentationBuilder.h"
#include "../../ecs/GameplayScene.h"
#include "../../ecs/util/PlayerQuery.h"
#include "../../ecs/util/GameplayRuntimeContext.h"
#include "../../ecs/components/Components.h"
#include "../../ecs/components/NetworkComponents.h"
#include "../../world/IWorldView.h"
#include "../../world/block/Block.h"
#include "../../world/chunk/Chunk.h"
#include "../modes/GameplayModeRules.h"
#include "../camera/CameraController.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

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
    const glm::vec3 front = {std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch)), std::sin(glm::radians(pitch)),
                             std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch))};
    return glm::normalize(front);
}

glm::vec3 cameraRightFromFront(const glm::vec3& front) {
    return glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
}

renderer::contracts::GameplayRenderObjectKey renderObjectKey(const entt::entity entity) {
    return static_cast<renderer::contracts::GameplayRenderObjectKey>(entt::to_integral(entity));
}

renderer::contracts::HumanoidBodyPart humanoidBodyPart(const ecs::StevePartType partType) {
    using RenderPart = renderer::contracts::HumanoidBodyPart;
    switch (partType) {
    case ecs::StevePartType::Torso: return RenderPart::Torso;
    case ecs::StevePartType::Head: return RenderPart::Head;
    case ecs::StevePartType::RightArm: return RenderPart::RightArm;
    case ecs::StevePartType::LeftArm: return RenderPart::LeftArm;
    case ecs::StevePartType::RightLeg: return RenderPart::RightLeg;
    case ecs::StevePartType::LeftLeg: return RenderPart::LeftLeg;
    }
    std::abort();
}

renderer::contracts::HumanoidSkinLayout humanoidSkinLayout(const ecs::EntitySkinLayoutKind layout) {
    using RenderLayout = renderer::contracts::HumanoidSkinLayout;
    switch (layout) {
    case ecs::EntitySkinLayoutKind::Steve64x64: return RenderLayout::Steve64x64;
    case ecs::EntitySkinLayoutKind::Classic64x64: return RenderLayout::Classic64x64;
    case ecs::EntitySkinLayoutKind::Classic64x32: return RenderLayout::Classic64x32;
    }
    std::abort();
}

glm::vec2 queryWorldLight(const IWorldView& worldView, const glm::vec3& position) {
    const int blockX = static_cast<int>(std::floor(position.x));
    const int blockY = static_cast<int>(std::floor(position.y));
    const int blockZ = static_cast<int>(std::floor(position.z));
    if (!worldView.isChunkLoadedForBlock(blockX, blockY, blockZ)) {
        return {1.0f, 0.0f};
    }

    const glm::ivec2 chunkCoords = worldView.getChunkCoords(blockX, blockZ);
    const auto& chunks = worldView.getActiveChunks();
    const auto chunk = chunks.find(IWorldView::chunkKey(chunkCoords.x, chunkCoords.y));
    if (chunk == chunks.end()) {
        return {1.0f, 0.0f};
    }

    const glm::ivec3 local = Chunk::worldToLocal(blockX, blockY, blockZ);
    const uint8_t sunlight = chunk->second->getSunlight(local.x, local.y, local.z);
    const uint8_t blockLight = chunk->second->getBlockLight(local.x, local.y, local.z);
    return {sunlight / 15.0f, blockLight / 15.0f};
}

float hurtFlashForRoot(const entt::registry& registry, const entt::entity root) {
    const auto* hurt = registry.try_get<ecs::HurtEffectComponent>(root);
    if (hurt == nullptr || hurt->flashDurationSeconds <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(hurt->flashSecondsRemaining / hurt->flashDurationSeconds, 0.0f, 1.0f);
}

glm::mat4 applyMobVisualScale(const glm::mat4& model, const glm::vec3& pivot, const float scale) {
    assert(scale > 0.0f);
    if (std::abs(scale - 1.0f) <= 0.0001f) {
        return model;
    }
    return glm::translate(glm::mat4(1.0f), pivot) * glm::scale(glm::mat4(1.0f), glm::vec3(scale)) *
           glm::translate(glm::mat4(1.0f), -pivot) * model;
}

void appendHumanoidPart(renderer::contracts::GameplayRenderSnapshot& snapshot, const entt::entity partEntity,
                        const glm::mat4& model, const ecs::StevePartType partType,
                        const std::string* modelPartName = nullptr) {
    renderer::contracts::HumanoidPartRenderData part;
    part.partKey = renderObjectKey(partEntity);
    part.model = model;
    part.bodyPart = humanoidBodyPart(partType);
    if (modelPartName != nullptr) {
        part.modelPartName = *modelPartName;
    }
    snapshot.humanoidParts.push_back(std::move(part));
}

void buildHumanoidSnapshot(entt::registry& registry, const IWorldView& worldView,
                           renderer::contracts::GameplayRenderSnapshot& snapshot) {
    auto steveView = registry.view<ecs::SteveTag, ecs::ChildrenComponent>();
    for (const entt::entity root : steveView) {
        const auto& rootChildren = steveView.get<ecs::ChildrenComponent>(root);
        glm::vec3 entityCenter(0.0f);
        bool hasCenter = false;
        for (const entt::entity child : rootChildren.children) {
            if (!registry.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) {
                continue;
            }
            const auto& part = registry.get<ecs::StevePartComponent>(child);
            if (part.partType == ecs::StevePartType::Torso) {
                entityCenter = glm::vec3(registry.get<ecs::WorldTransformComponent>(child).worldMatrix[3]);
                hasCenter = true;
                break;
            }
        }
        if (!hasCenter) {
            continue;
        }

        renderer::contracts::HumanoidActorRenderData actor;
        actor.objectKey = renderObjectKey(root);
        actor.firstPart = snapshot.humanoidParts.size();
        actor.localPlayerModel = !registry.all_of<ecs::EntityNetIdComponent>(root);
        actor.textureKey = "steve";
        actor.skinLayout = renderer::contracts::HumanoidSkinLayout::Steve64x64;
        actor.center = entityCenter;
        actor.light = queryWorldLight(worldView, entityCenter);
        actor.hurtFlash = hurtFlashForRoot(registry, root);

        for (const entt::entity child : rootChildren.children) {
            if (registry.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(child)) {
                const auto& part = registry.get<ecs::StevePartComponent>(child);
                appendHumanoidPart(snapshot, child, registry.get<ecs::WorldTransformComponent>(child).worldMatrix,
                                   part.partType);
            }
            const auto* children = registry.try_get<ecs::ChildrenComponent>(child);
            if (children == nullptr) {
                continue;
            }
            for (const entt::entity partEntity : children->children) {
                if (!registry.all_of<ecs::StevePartComponent, ecs::WorldTransformComponent>(partEntity)) {
                    continue;
                }
                const auto& part = registry.get<ecs::StevePartComponent>(partEntity);
                appendHumanoidPart(snapshot, partEntity,
                                   registry.get<ecs::WorldTransformComponent>(partEntity).worldMatrix, part.partType);
            }
        }
        actor.partCount = snapshot.humanoidParts.size() - actor.firstPart;
        snapshot.humanoidActors.push_back(std::move(actor));
    }

    auto mobView =
        registry.view<ecs::MobTag, ecs::ChildrenComponent, ecs::MobVisualComponent, ecs::TransformComponent>();
    for (const entt::entity root : mobView) {
        const auto& visual = mobView.get<ecs::MobVisualComponent>(root);
        const auto& rootTransform = mobView.get<ecs::TransformComponent>(root);
        const auto& rootChildren = mobView.get<ecs::ChildrenComponent>(root);

        renderer::contracts::HumanoidActorRenderData actor;
        actor.objectKey = renderObjectKey(root);
        actor.firstPart = snapshot.humanoidParts.size();
        actor.textureKey = visual.textureKey;
        actor.skinLayout = humanoidSkinLayout(visual.skinLayout);
        actor.center = rootTransform.position + glm::vec3(0.0f, rootTransform.eyeHeight * 0.5f, 0.0f);
        actor.light = queryWorldLight(worldView, actor.center);
        actor.hurtFlash = hurtFlashForRoot(registry, root);
        const auto* model = registry.try_get<ecs::EntityModelComponent>(root);
        if (model != nullptr) {
            actor.modelId = model->modelId;
        }

        std::vector<entt::entity> queue(rootChildren.children.begin(), rootChildren.children.end());
        for (std::size_t index = 0u; index < queue.size(); ++index) {
            const entt::entity partEntity = queue[index];
            if (const auto* children = registry.try_get<ecs::ChildrenComponent>(partEntity)) {
                queue.insert(queue.end(), children->children.begin(), children->children.end());
            }
            const auto* transform = registry.try_get<ecs::WorldTransformComponent>(partEntity);
            if (transform == nullptr) {
                continue;
            }
            const glm::mat4 partModel =
                applyMobVisualScale(transform->worldMatrix, rootTransform.position, visual.scale);
            if (model != nullptr) {
                const auto* part = registry.try_get<ecs::EntityModelPartComponent>(partEntity);
                if (part != nullptr) {
                    appendHumanoidPart(snapshot, partEntity, partModel, ecs::StevePartType::Torso, &part->partName);
                }
            } else {
                const auto* part = registry.try_get<ecs::StevePartComponent>(partEntity);
                if (part != nullptr) {
                    appendHumanoidPart(snapshot, partEntity, partModel, part->partType);
                }
            }
        }
        actor.partCount = snapshot.humanoidParts.size() - actor.firstPart;
        snapshot.humanoidActors.push_back(std::move(actor));
    }
}

void buildFallingBlockSnapshot(entt::registry& registry, const IWorldView& worldView,
                               renderer::contracts::GameplayRenderSnapshot& snapshot) {
    auto append = [&](const std::size_t objectKey, const BlockStateId stateId, const glm::vec3& position) {
        snapshot.fallingBlocks.push_back({static_cast<renderer::contracts::GameplayRenderObjectKey>(objectKey), stateId,
                                          position, queryWorldLight(worldView, position)});
    };

    auto fallingView = registry.view<ecs::FallingBlockTag, ecs::FallingBlockComponent, ecs::TransformComponent,
                                     ecs::DropEntityIdComponent>();
    for (const entt::entity entity : fallingView) {
        const auto& block = fallingView.get<ecs::FallingBlockComponent>(entity);
        append(fallingView.get<ecs::DropEntityIdComponent>(entity).dropId,
               BlockStateRegistry::getDefaultState(block.blockId),
               fallingView.get<ecs::TransformComponent>(entity).position);
    }

    auto movingView = registry.view<ecs::MovingBlockTag, ecs::MovingBlockComponent, ecs::TransformComponent,
                                    ecs::DropEntityIdComponent>();
    for (const entt::entity entity : movingView) {
        append(movingView.get<ecs::DropEntityIdComponent>(entity).dropId,
               movingView.get<ecs::MovingBlockComponent>(entity).stateId,
               movingView.get<ecs::TransformComponent>(entity).position);
    }
}

template <typename Tag>
void appendDropSnapshot(entt::registry& registry, const IWorldView& worldView,
                        renderer::contracts::GameplayRenderSnapshot& snapshot) {
    auto view = registry.view<Tag, ecs::DropEntityIdComponent, ecs::TransformComponent, ecs::ItemComponent,
                              ecs::BoundsComponent, ecs::SpinVisualComponent>();
    for (const entt::entity entity : view) {
        const auto& id = view.template get<ecs::DropEntityIdComponent>(entity);
        const auto& transform = view.template get<ecs::TransformComponent>(entity);
        const auto& item = view.template get<ecs::ItemComponent>(entity);
        const auto& bounds = view.template get<ecs::BoundsComponent>(entity);
        const auto& spin = view.template get<ecs::SpinVisualComponent>(entity);
        snapshot.drops.push_back({static_cast<renderer::contracts::GameplayRenderObjectKey>(id.dropId), item.itemId,
                                  transform.position, bounds.halfExtents, spin.yawRadians,
                                  queryWorldLight(worldView, transform.position)});
    }
}

void buildParticleSnapshot(entt::registry& registry, renderer::contracts::GameplayRenderSnapshot& snapshot) {
    auto view = registry.view<ecs::ParticleTag, ecs::TransformComponent, ecs::ParticleComponent>();
    snapshot.particles.reserve(view.size_hint());
    for (const entt::entity entity : view) {
        const auto& transform = view.get<ecs::TransformComponent>(entity);
        const auto& particle = view.get<ecs::ParticleComponent>(entity);
        snapshot.particles.push_back({transform.position, particle.life, particle.maxLife, particle.size,
                                      particle.biomeTintFactor, particle.layer, particle.uvMin, particle.uvMax});
    }
}

void buildGameplayRenderSnapshot(entt::registry& registry, const IWorldView& worldView,
                                 renderer::contracts::GameplayRenderSnapshot& snapshot) {
    buildHumanoidSnapshot(registry, worldView, snapshot);
    buildFallingBlockSnapshot(registry, worldView, snapshot);
    appendDropSnapshot<ecs::DropItemTag>(registry, worldView, snapshot);
    appendDropSnapshot<ecs::ProjectileTag>(registry, worldView, snapshot);
    std::sort(snapshot.drops.begin(), snapshot.drops.end(),
              [](const auto& left, const auto& right) { return left.objectKey < right.objectKey; });
    buildParticleSnapshot(registry, snapshot);
    snapshot.hasDynamicShadowCasters =
        !snapshot.humanoidParts.empty() || !snapshot.fallingBlocks.empty() || !snapshot.drops.empty();
}

} // namespace

GameplayPresentationSnapshot GameplayPresentationBuilder::build(ecs::GameplayRegistry& reg,
                                                                const CameraController& cameraController,
                                                                const IWorldView& worldView,
                                                                const float interpolationAlpha) {

    GameplayPresentationSnapshot snap;
    auto& registry = reg.registry();
    const float alpha = std::clamp(interpolationAlpha, 0.0f, 1.0f);

    buildGameplayRenderSnapshot(registry, worldView, snap.renderScene);

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
            eyePosition = renderPosition + glm::vec3(0.0f, renderEyeHeight + viewBob.verticalOffset, 0.0f);

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
