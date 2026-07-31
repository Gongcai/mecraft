#include "MobAnimationSystem.h"

#include "../../components/Components.h"
#include "../HumanoidAnimationHelper.h"

#include <cmath>
#include <vector>

#include <glm/gtc/constants.hpp>

namespace ecs {
namespace {

void updateWalkState(entt::registry& reg, const entt::entity entity, SteveAnimationStateComponent& anim,
                     const float dt) {
    if (!reg.all_of<TransformComponent>(entity)) {
        return;
    }

    const auto& transform = reg.get<TransformComponent>(entity);
    const glm::vec3 delta = transform.position - anim.lastPosition;
    const float horizontalSpeed = glm::length(glm::vec2(delta.x, delta.z)) / dt;
    anim.isWalking = horizontalSpeed > 0.5f && anim.isOnGround;
    anim.lastPosition = transform.position;

    if (anim.isWalking) {
        anim.walkCyclePhase += anim.walkCycleSpeed * dt;
        if (anim.walkCyclePhase > glm::two_pi<float>()) {
            anim.walkCyclePhase -= glm::two_pi<float>();
        }
    } else {
        const float swing = glm::sin(anim.walkCyclePhase);
        if (std::abs(swing) < 0.01f) {
            anim.walkCyclePhase = 0.0f;
        } else {
            anim.walkCyclePhase *= 0.85f;
        }
    }
}

LocalTransformComponent* findGenericPartTransform(entt::registry& reg, const entt::entity root,
                                                  const std::string& partName) {
    const auto* rootChildren = reg.try_get<ChildrenComponent>(root);
    if (rootChildren == nullptr) {
        return nullptr;
    }

    std::vector<entt::entity> queue;
    queue.reserve(rootChildren->children.size());
    for (const entt::entity child : rootChildren->children) {
        queue.push_back(child);
    }

    std::size_t index = 0;
    while (index < queue.size()) {
        const entt::entity entity = queue[index++];
        if (const auto* children = reg.try_get<ChildrenComponent>(entity)) {
            for (const entt::entity child : children->children) {
                queue.push_back(child);
            }
        }

        const auto* part = reg.try_get<EntityModelPartComponent>(entity);
        if (part == nullptr || part->partName != partName) {
            continue;
        }
        return reg.try_get<LocalTransformComponent>(entity);
    }

    return nullptr;
}

void setGenericPartRotationX(entt::registry& reg, const entt::entity root, const char* partName, const float degrees) {
    if (LocalTransformComponent* transform = findGenericPartTransform(reg, root, partName)) {
        transform->localRotation.x = degrees;
    }
}

void updateGenericModelAnimation(entt::registry& reg, const entt::entity entity, const EntityModelComponent& model,
                                 SteveAnimationStateComponent& anim, const float dt, const float yaw) {
    updateWalkState(reg, entity, anim, dt);

    if (!model.yawPartName.empty()) {
        if (LocalTransformComponent* yawPart = findGenericPartTransform(reg, entity, model.yawPartName)) {
            yawPart->localRotation.y = -yaw + 90.0f;
        }
    }

    if (model.animationId == "minecraft:creeper_walk" || model.animationId == "minecraft:quadruped_walk") {
        const float swing = glm::sin(anim.walkCyclePhase) * 35.0f;
        setGenericPartRotationX(reg, entity, "right_hind_leg", swing);
        setGenericPartRotationX(reg, entity, "left_front_leg", swing);
        setGenericPartRotationX(reg, entity, "left_hind_leg", -swing);
        setGenericPartRotationX(reg, entity, "right_front_leg", -swing);
    }
}

} // namespace

void MobAnimationSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    const float dt = ctx.dt;

    if (dt <= 0.0f)
        return;

    auto& reg = registry.registry();
    auto view = reg.view<MobTag, SteveAnimationStateComponent, ChildrenComponent>();

    for (auto entity : view) {
        auto& anim = reg.get<SteveAnimationStateComponent>(entity);

        float torsoYaw = 0.0f;
        if (reg.all_of<MobAIComponent>(entity)) {
            torsoYaw = reg.get<MobAIComponent>(entity).yaw;
        }

        if (const auto* model = reg.try_get<EntityModelComponent>(entity)) {
            updateGenericModelAnimation(reg, entity, *model, anim, dt, torsoYaw);
        } else {
            // Mob head stays upright; only player view pitch drives humanoid heads.
            HumanoidAnimationHelper::update(reg, entity, anim, dt, torsoYaw, 0.0f);
        }
    }
}

} // namespace ecs
