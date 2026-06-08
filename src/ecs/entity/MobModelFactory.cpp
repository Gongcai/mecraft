#include "MobModelFactory.h"
#include "../components/Components.h"
#include "../components/NetworkComponents.h"
#include "../../physics/PhysicsInfo.h"

namespace ecs {

entt::entity MobModelFactory::createZombie(GameplayRegistry& registry,
                                            const glm::vec3& worldPosition,
                                            const bool gameplayControlled) {
    auto& reg = registry.registry();

    // Root entity: feet anchor
    auto root = reg.create();
    reg.emplace<MobTag>(root);
    reg.emplace<SkinTypeComponent>(root, SkinTypeComponent::Type::Mob);
    reg.emplace<TransformComponent>(root, worldPosition, 1.62f);
    reg.emplace<SteveAnimationStateComponent>(root);
    reg.emplace<WorldTransformComponent>(root);
    reg.emplace<MobAIComponent>(root);
    if (gameplayControlled) {
        reg.emplace<MoveIntentComponent>(root);
        reg.emplace<GroundedStateComponent>(root);
        reg.emplace<HealthComponent>(root, 20, 20);
        if (ItemIds::COAL != 0) {
            reg.emplace<DropTableComponent>(root, ItemIds::COAL, 1u, 1u);
        }
        reg.emplace<NetworkSyncTag>(root);
    }
    auto& rootChildren = reg.emplace<ChildrenComponent>(root);

    // Physics body for gravity and collision
    if (gameplayControlled) {
        auto& physBody = reg.emplace<PhysicsBodyComponent>(root);
        physBody.body.position = worldPosition;
        physBody.body.halfExtents = glm::vec3(0.3f, 0.9f, 0.3f);
        physBody.body.colliderOffset = glm::vec3(0.0f, 0.9f, 0.0f);
        physBody.body.eyeOffsetY = 1.62f;
    }

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

entt::entity MobModelFactory::createZombieReplica(GameplayRegistry& registry,
                                                   const glm::vec3& worldPosition,
                                                   const float yaw) {
    const entt::entity root = createZombie(registry, worldPosition, false);
    if (auto* ai = registry.try_get<MobAIComponent>(root)) {
        ai->yaw = yaw;
    }
    return root;
}

void MobModelFactory::destroyMob(GameplayRegistry& registry, entt::entity mobRoot) {
    auto& reg = registry.registry();
    std::vector<entt::entity> toDestroy;

    auto collect = [&reg, &toDestroy](auto&& self, const entt::entity entity) -> void {
        if (entity == entt::null || !reg.valid(entity)) {
            return;
        }
        toDestroy.push_back(entity);
        if (const auto* children = reg.try_get<ChildrenComponent>(entity)) {
            for (const entt::entity child : children->children) {
                self(self, child);
            }
        }
    };

    collect(collect, mobRoot);

    for (auto e : toDestroy) {
        if (reg.valid(e)) {
            reg.destroy(e);
        }
    }
}

} // namespace ecs
