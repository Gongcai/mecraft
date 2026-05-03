#include "MobModelFactory.h"
#include "../components/Components.h"
#include "../../physics/PhysicsInfo.h"

namespace ecs {

entt::entity MobModelFactory::createZombie(GameplayRegistry& registry,
                                            const glm::vec3& worldPosition) {
    auto& reg = registry.registry();

    // Root entity: feet anchor
    auto root = reg.create();
    reg.emplace<MobTag>(root);
    reg.emplace<SkinTypeComponent>(root, SkinTypeComponent::Type::Mob);
    reg.emplace<TransformComponent>(root, worldPosition, 1.62f);
    reg.emplace<SteveAnimationStateComponent>(root);
    reg.emplace<WorldTransformComponent>(root);
    reg.emplace<MobAIComponent>(root);
    reg.emplace<MoveIntentComponent>(root);
    reg.emplace<GroundedStateComponent>(root);
    auto& rootChildren = reg.emplace<ChildrenComponent>(root);

    // Physics body for gravity and collision
    auto& physBody = reg.emplace<PhysicsBodyComponent>(root);
    physBody.body.position = worldPosition;
    physBody.body.eyeOffsetY = 1.62f;

    // ── Torso ──
    auto torso = reg.create();
    reg.emplace<StevePartComponent>(torso, StevePartType::Torso);
    reg.emplace<LocalTransformComponent>(torso,
        glm::vec3(0.0f, 1.125f, 0.0f),
        glm::vec3(0.0f),
        glm::vec3(1.0f));
    reg.emplace<WorldTransformComponent>(torso);
    reg.emplace<ParentComponent>(torso, root);
    auto& torsoChildren = reg.emplace<ChildrenComponent>(torso);
    rootChildren.children.push_back(torso);

    // ── Head ──
    auto head = reg.create();
    reg.emplace<StevePartComponent>(head, StevePartType::Head);
    reg.emplace<LocalTransformComponent>(head,
        glm::vec3(0.0f, 0.375f, 0.0f),
        glm::vec3(0.0f),
        glm::vec3(1.0f));
    reg.emplace<WorldTransformComponent>(head);
    reg.emplace<ParentComponent>(head, torso);
    torsoChildren.children.push_back(head);

    // ── Right Arm ──
    auto rightArm = reg.create();
    reg.emplace<StevePartComponent>(rightArm, StevePartType::RightArm);
    reg.emplace<LocalTransformComponent>(rightArm,
        glm::vec3(-0.3125f, 0.375f, 0.0f),
        glm::vec3(0.0f),
        glm::vec3(1.0f));
    reg.emplace<WorldTransformComponent>(rightArm);
    reg.emplace<ParentComponent>(rightArm, torso);
    torsoChildren.children.push_back(rightArm);

    // ── Left Arm ──
    auto leftArm = reg.create();
    reg.emplace<StevePartComponent>(leftArm, StevePartType::LeftArm);
    reg.emplace<LocalTransformComponent>(leftArm,
        glm::vec3(0.3125f, 0.375f, 0.0f),
        glm::vec3(0.0f),
        glm::vec3(1.0f));
    reg.emplace<WorldTransformComponent>(leftArm);
    reg.emplace<ParentComponent>(leftArm, torso);
    torsoChildren.children.push_back(leftArm);

    // ── Right Leg ──
    auto rightLeg = reg.create();
    reg.emplace<StevePartComponent>(rightLeg, StevePartType::RightLeg);
    reg.emplace<LocalTransformComponent>(rightLeg,
        glm::vec3(-0.125f, -0.375f, 0.0f),
        glm::vec3(0.0f),
        glm::vec3(1.0f));
    reg.emplace<WorldTransformComponent>(rightLeg);
    reg.emplace<ParentComponent>(rightLeg, torso);
    torsoChildren.children.push_back(rightLeg);

    // ── Left Leg ──
    auto leftLeg = reg.create();
    reg.emplace<StevePartComponent>(leftLeg, StevePartType::LeftLeg);
    reg.emplace<LocalTransformComponent>(leftLeg,
        glm::vec3(0.125f, -0.375f, 0.0f),
        glm::vec3(0.0f),
        glm::vec3(1.0f));
    reg.emplace<WorldTransformComponent>(leftLeg);
    reg.emplace<ParentComponent>(leftLeg, torso);
    torsoChildren.children.push_back(leftLeg);

    return root;
}

void MobModelFactory::destroyMob(GameplayRegistry& registry, entt::entity mobRoot) {
    auto& reg = registry.registry();
    if (!reg.all_of<ChildrenComponent>(mobRoot)) {
        reg.destroy(mobRoot);
        return;
    }

    std::vector<entt::entity> toDestroy;
    toDestroy.push_back(mobRoot);

    auto& rootChildren = reg.get<ChildrenComponent>(mobRoot);
    for (auto child : rootChildren.children) {
        toDestroy.push_back(child);
        if (reg.all_of<ChildrenComponent>(child)) {
            auto& subChildren = reg.get<ChildrenComponent>(child);
            for (auto sub : subChildren.children) {
                toDestroy.push_back(sub);
            }
        }
    }

    for (auto e : toDestroy) {
        reg.destroy(e);
    }
}

} // namespace ecs
