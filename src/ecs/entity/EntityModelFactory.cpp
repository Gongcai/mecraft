#include "EntityModelFactory.h"

#include "../../Diagnostics.h"
#include "EntityDefinitionRegistry.h"
#include "EntityModelRegistry.h"
#include "MobModelFactory.h"
#include "../components/Components.h"
#include "../components/NetworkComponents.h"

#include <cstdio>
#include <string>
#include <unordered_map>

namespace ecs {
namespace {

constexpr float kEntityModelUnit = 1.0f / 16.0f;

void applyVisualDefinition(entt::registry& reg, const entt::entity entity, const MobEntityDefinition& definition) {
    auto* visual = reg.try_get<MobVisualComponent>(entity);
    if (visual == nullptr) {
        visual = &reg.emplace<MobVisualComponent>(entity);
    }
    visual->model = definition.model;
    visual->textureKey = definition.textureKey;
    visual->skinLayout = definition.skinLayout;
    visual->scale = definition.visualScale;
}

entt::entity createGenericMob(GameplayRegistry& registry, const MobEntityDefinition& definition,
                              const glm::vec3& worldPosition, const bool gameplayControlled) {
    std::string error;
    EntityModelRegistry& modelRegistry = EntityModelRegistry::instance();
    if (!modelRegistry.ensureLoaded(&error)) {
        MECRAFT_LOG_PRINTF("[EntityModelFactory] Failed to load entity models: %s\n", error.c_str());
        MECRAFT_LOG_FLUSH(stdout);
        return entt::null;
    }

    const EntityModelDefinition* model = modelRegistry.findModel(definition.model);
    if (model == nullptr) {
        MECRAFT_LOG_PRINTF("[EntityModelFactory] Unknown entity model: %s\n", definition.model.c_str());
        MECRAFT_LOG_FLUSH(stdout);
        return entt::null;
    }

    entt::registry& reg = registry.registry();
    const entt::entity root = reg.create();
    reg.emplace<MobTag>(root);
    reg.emplace<TransformComponent>(root, worldPosition, definition.eyeHeight);
    reg.emplace<WorldTransformComponent>(root);
    reg.emplace<MobAIComponent>(root);
    reg.emplace<SteveAnimationStateComponent>(root);
    reg.emplace<EntityModelComponent>(root, definition.model, model->animationId, model->yawPartName);
    applyVisualDefinition(reg, root, definition);
    auto& rootChildren = reg.emplace<ChildrenComponent>(root);

    if (gameplayControlled) {
        reg.emplace<MoveIntentComponent>(root);
        reg.emplace<GroundedStateComponent>(root);
        reg.emplace<HealthComponent>(root, definition.health, definition.maxHealth);
        reg.emplace<HurtEffectComponent>(root);
        auto& physicsBody = reg.emplace<PhysicsBodyComponent>(root);
        physicsBody.body.position = worldPosition;
        physicsBody.body.halfExtents = definition.physics.halfExtents;
        physicsBody.body.colliderOffset = definition.physics.colliderOffset;
        physicsBody.body.eyeOffsetY = definition.physics.eyeOffsetY;
        reg.emplace<NetworkSyncTag>(root);
    }

    std::unordered_map<std::string, entt::entity> partEntities;
    partEntities.reserve(model->parts.size());

    for (const EntityModelPartDefinition& part : model->parts) {
        const entt::entity partEntity = reg.create();
        reg.emplace<EntityModelPartComponent>(partEntity, part.name);
        reg.emplace<LocalTransformComponent>(partEntity, part.pivot * kEntityModelUnit, part.rotation, glm::vec3(1.0f));
        reg.emplace<WorldTransformComponent>(partEntity);
        reg.emplace<ChildrenComponent>(partEntity);

        if (part.parent.empty()) {
            reg.emplace<ParentComponent>(partEntity, root);
            rootChildren.children.push_back(partEntity);
        } else {
            const entt::entity parentEntity = partEntities.at(part.parent);
            reg.emplace<ParentComponent>(partEntity, parentEntity);
            reg.get<ChildrenComponent>(parentEntity).children.push_back(partEntity);
        }

        partEntities.emplace(part.name, partEntity);
    }

    return root;
}

} // namespace

entt::entity EntityModelFactory::createMob(GameplayRegistry& registry, const MobEntityDefinition& definition,
                                           const glm::vec3& worldPosition, const bool gameplayControlled) {
    if (definition.model == "humanoid") {
        const entt::entity entity = MobModelFactory::createHumanoidMob(registry, worldPosition, gameplayControlled);
        if (entity != entt::null) {
            applyVisualDefinition(registry.registry(), entity, definition);
        }
        return entity;
    }

    return createGenericMob(registry, definition, worldPosition, gameplayControlled);
}

entt::entity EntityModelFactory::createMobReplica(GameplayRegistry& registry, const MobEntityDefinition& definition,
                                                  const glm::vec3& worldPosition, const float yaw) {
    const entt::entity root = createMob(registry, definition, worldPosition, false);
    if (root == entt::null) {
        return entt::null;
    }
    if (auto* ai = registry.try_get<MobAIComponent>(root)) {
        ai->yaw = yaw;
    }
    return root;
}

} // namespace ecs
