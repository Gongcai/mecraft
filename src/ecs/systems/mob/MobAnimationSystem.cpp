#include "MobAnimationSystem.h"

#include "../../components/Components.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace ecs {

void MobAnimationSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    const float dt = ctx.dt;

    if (dt <= 0.0f) return;

    auto& reg = registry.registry();
    auto view = reg.view<MobTag, SteveAnimationStateComponent, ChildrenComponent>();

    for (auto entity : view) {
        auto& anim = view.get<SteveAnimationStateComponent>(entity);

        // Detect walking from TransformComponent position change
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

        float swing = glm::sin(anim.walkCyclePhase) * 40.0f;

        // Update torso rotation from MobAIComponent yaw
        auto& rootChildren = reg.get<ChildrenComponent>(entity);
        for (auto child : rootChildren.children) {
            if (!reg.all_of<StevePartComponent, LocalTransformComponent>(child)) continue;
            auto& torsoPart = reg.get<StevePartComponent>(child);
            auto& torsoLocal = reg.get<LocalTransformComponent>(child);
            if (torsoPart.partType == StevePartType::Torso) {
                if (reg.all_of<MobAIComponent>(entity)) {
                    auto& ai = reg.get<MobAIComponent>(entity);
                    torsoLocal.localRotation.y = -ai.yaw + 90.0f;
                }
                break;
            }
        }

        // Update each child part's local rotation
        for (auto child : rootChildren.children) {
            if (!reg.all_of<ChildrenComponent>(child)) continue;
            auto& partChildren = reg.get<ChildrenComponent>(child);
            for (auto partEntity : partChildren.children) {
                if (!reg.all_of<StevePartComponent, LocalTransformComponent>(partEntity)) continue;
                auto& part = reg.get<StevePartComponent>(partEntity);
                auto& local = reg.get<LocalTransformComponent>(partEntity);

                switch (part.partType) {
                case StevePartType::LeftArm:
                    local.localRotation.x = swing;
                    break;
                case StevePartType::RightArm:
                    local.localRotation.x = -swing;
                    break;
                case StevePartType::LeftLeg:
                    local.localRotation.x = -swing;
                    break;
                case StevePartType::RightLeg:
                    local.localRotation.x = swing;
                    break;
                case StevePartType::Head:
                    // Mob head stays upright (no camera pitch)
                    local.localRotation.x = 0.0f;
                    local.localRotation.y = 0.0f;
                    break;
                default:
                    break;
                }
            }
        }
    }
}

} // namespace ecs
