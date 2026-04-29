#include "SteveModelFactory.h"
#include "../components/Components.h"

namespace ecs {

entt::entity SteveModelFactory::createSteve(GameplayRegistry& registry,
                                             const glm::vec3& worldPosition) {
    // Steve root entity: holds world position, animation state, and children list.
    // The torso is a child of this root, so the root acts as the "feet" anchor.
    auto root = registry.create();
    registry.emplace<SteveTag>(root);
    registry.emplace<TransformComponent>(root, worldPosition, 1.62f);
    registry.emplace<SteveAnimationStateComponent>(root);
    registry.emplace<WorldTransformComponent>(root);
    registry.emplace<CameraStateComponent>(root);  // synced from player camera each frame
    auto& rootChildren = registry.emplace<ChildrenComponent>(root);

    // ── Torso ──
    // Torso is the first child of root. Its local position places it at the body center.
    // Body center is at y = 0.75 + 0.375 = 1.125 above feet.
    auto torso = registry.create();
    registry.emplace<StevePartComponent>(torso, StevePartType::Torso);
    registry.emplace<LocalTransformComponent>(torso,
        glm::vec3(0.0f, 1.125f, 0.0f),
        glm::vec3(0.0f),
        glm::vec3(1.0f));
    registry.emplace<WorldTransformComponent>(torso);
    registry.emplace<ParentComponent>(torso, root);
    auto& torsoChildren = registry.emplace<ChildrenComponent>(torso);
    rootChildren.children.push_back(torso);

    // ── Head ──
    // Head pivot (neck) is at the top of the torso: y = 0.375 above torso center.
    // Head mesh origin is at its bottom center (neck joint), extends upward.
    auto head = registry.create();
    registry.emplace<StevePartComponent>(head, StevePartType::Head);
    registry.emplace<LocalTransformComponent>(head,
        glm::vec3(0.0f, 0.375f, 0.0f),
        glm::vec3(0.0f),
        glm::vec3(1.0f));
    registry.emplace<WorldTransformComponent>(head);
    registry.emplace<ParentComponent>(head, torso);
    torsoChildren.children.push_back(head);

    // ── Right Arm ──
    // Shoulder pivot at torso top, right side.
    // Arm mesh origin is at top center (shoulder joint), extends downward.
    auto rightArm = registry.create();
    registry.emplace<StevePartComponent>(rightArm, StevePartType::RightArm);
    registry.emplace<LocalTransformComponent>(rightArm,
        glm::vec3(-0.3125f, 0.375f, 0.0f),
        glm::vec3(0.0f),
        glm::vec3(1.0f));
    registry.emplace<WorldTransformComponent>(rightArm);
    registry.emplace<ParentComponent>(rightArm, torso);
    torsoChildren.children.push_back(rightArm);

    // ── Left Arm ──
    auto leftArm = registry.create();
    registry.emplace<StevePartComponent>(leftArm, StevePartType::LeftArm);
    registry.emplace<LocalTransformComponent>(leftArm,
        glm::vec3(0.3125f, 0.375f, 0.0f),
        glm::vec3(0.0f),
        glm::vec3(1.0f));
    registry.emplace<WorldTransformComponent>(leftArm);
    registry.emplace<ParentComponent>(leftArm, torso);
    torsoChildren.children.push_back(leftArm);

    // ── Right Leg ──
    // Hip pivot at torso bottom, slightly right of center.
    // Leg mesh origin is at top center (hip joint), extends downward.
    auto rightLeg = registry.create();
    registry.emplace<StevePartComponent>(rightLeg, StevePartType::RightLeg);
    registry.emplace<LocalTransformComponent>(rightLeg,
        glm::vec3(-0.125f, -0.375f, 0.0f),
        glm::vec3(0.0f),
        glm::vec3(1.0f));
    registry.emplace<WorldTransformComponent>(rightLeg);
    registry.emplace<ParentComponent>(rightLeg, torso);
    torsoChildren.children.push_back(rightLeg);

    // ── Left Leg ──
    auto leftLeg = registry.create();
    registry.emplace<StevePartComponent>(leftLeg, StevePartType::LeftLeg);
    registry.emplace<LocalTransformComponent>(leftLeg,
        glm::vec3(0.125f, -0.375f, 0.0f),
        glm::vec3(0.0f),
        glm::vec3(1.0f));
    registry.emplace<WorldTransformComponent>(leftLeg);
    registry.emplace<ParentComponent>(leftLeg, torso);
    torsoChildren.children.push_back(leftLeg);

    return root;
}

void SteveModelFactory::destroySteve(GameplayRegistry& registry, entt::entity steveRoot) {
    if (!registry.has<ChildrenComponent>(steveRoot)) {
        registry.destroy(steveRoot);
        return;
    }

    // Recursively destroy children
    std::vector<entt::entity> toDestroy;
    toDestroy.push_back(steveRoot);

    auto& rootChildren = registry.get<ChildrenComponent>(steveRoot);
    for (auto child : rootChildren.children) {
        toDestroy.push_back(child);
        if (registry.has<ChildrenComponent>(child)) {
            auto& subChildren = registry.get<ChildrenComponent>(child);
            for (auto sub : subChildren.children) {
                toDestroy.push_back(sub);
            }
        }
    }

    for (auto e : toDestroy) {
        registry.destroy(e);
    }
}

} // namespace ecs
