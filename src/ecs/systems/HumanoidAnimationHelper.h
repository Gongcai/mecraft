#ifndef MECRAFT_ECS_HUMANOID_ANIMATION_HELPER_H
#define MECRAFT_ECS_HUMANOID_ANIMATION_HELPER_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

#include "../components/SteveComponents.h"
#include "../components/TransformComponents.h"

namespace ecs {

/// Shared humanoid walk-cycle and limb-animation logic.
/// Used by both SteveAnimationSystem (player) and MobAnimationSystem.
struct HumanoidAnimationHelper {
    /// @param torsoYaw   World-space yaw for the torso rotation (degrees).
    /// @param headPitch  Pitch applied to the head (degrees). 0 for mobs.
    static void update(entt::registry& reg, entt::entity entity, SteveAnimationStateComponent& anim, float dt,
                       float torsoYaw, float headPitch) {
        // Detect walking from position change
        if (reg.all_of<TransformComponent>(entity)) {
            auto& transform = reg.get<TransformComponent>(entity);
            glm::vec3 delta = transform.position - anim.lastPosition;
            float horizontalSpeed = glm::length(glm::vec2(delta.x, delta.z)) / dt;
            anim.isWalking = horizontalSpeed > 0.5f && anim.isOnGround;
            anim.lastPosition = transform.position;
        }

        // Advance walk cycle
        if (anim.isWalking) {
            anim.walkCyclePhase += anim.walkCycleSpeed * dt;
            if (anim.walkCyclePhase > glm::two_pi<float>()) {
                anim.walkCyclePhase -= glm::two_pi<float>();
            }
        } else {
            float swing = glm::sin(anim.walkCyclePhase);
            if (std::abs(swing) < 0.01f) {
                anim.walkCyclePhase = 0.0f;
            } else {
                anim.walkCyclePhase *= 0.85f;
            }
        }

        const float swing = glm::sin(anim.walkCyclePhase) * 40.0f;

        // Rotate torso to face yaw direction
        auto& rootChildren = reg.get<ChildrenComponent>(entity);
        for (auto child : rootChildren.children) {
            if (!reg.all_of<StevePartComponent, LocalTransformComponent>(child))
                continue;
            auto& part = reg.get<StevePartComponent>(child);
            auto& local = reg.get<LocalTransformComponent>(child);
            if (part.partType == StevePartType::Torso) {
                local.localRotation.y = -torsoYaw + 90.0f;
                break;
            }
        }

        // Update each child part's local rotation
        for (auto child : rootChildren.children) {
            if (!reg.all_of<ChildrenComponent>(child))
                continue;
            auto& partChildren = reg.get<ChildrenComponent>(child);
            for (auto partEntity : partChildren.children) {
                if (!reg.all_of<StevePartComponent, LocalTransformComponent>(partEntity))
                    continue;
                auto& part = reg.get<StevePartComponent>(partEntity);
                auto& local = reg.get<LocalTransformComponent>(partEntity);

                switch (part.partType) {
                case StevePartType::LeftArm: local.localRotation.x = swing; break;
                case StevePartType::RightArm: local.localRotation.x = -swing; break;
                case StevePartType::LeftLeg: local.localRotation.x = -swing; break;
                case StevePartType::RightLeg: local.localRotation.x = swing; break;
                case StevePartType::Head:
                    local.localRotation.x = -headPitch;
                    local.localRotation.y = 0.0f;
                    break;
                default: break;
                }
            }
        }
    }
};

} // namespace ecs

#endif // MECRAFT_ECS_HUMANOID_ANIMATION_HELPER_H
