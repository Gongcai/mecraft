#include "EntityFactory.h"

#include "MobModelFactory.h"
#include "../components/Components.h"
#include "../components/NetworkComponents.h"

#include <utility>

namespace ecs {
namespace {

template <typename Component, typename... Args>
void ensureComponent(entt::registry& registry, const entt::entity entity, Args&&... args) {
    if (!registry.all_of<Component>(entity)) {
        registry.emplace<Component>(entity, std::forward<Args>(args)...);
    }
}

template <typename Component, typename... Args>
Component& ensureComponentRef(entt::registry& registry, const entt::entity entity, Args&&... args) {
    ensureComponent<Component>(registry, entity, std::forward<Args>(args)...);
    return registry.get<Component>(entity);
}

} // namespace

entt::entity EntityFactory::createServerPlayerProxy(GameplayRegistry& registry,
                                                    const glm::vec3& position,
                                                    const glm::vec3& velocity) {
    entt::registry& reg = registry.registry();
    const entt::entity entity = reg.create();
    ensureServerPlayerProxy(registry, entity, position, velocity);
    return entity;
}

void EntityFactory::ensureServerPlayerProxy(GameplayRegistry& registry,
                                            const entt::entity entity,
                                            const glm::vec3& position,
                                            const glm::vec3& velocity) {
    entt::registry& reg = registry.registry();
    if (entity == entt::null || !reg.valid(entity)) {
        return;
    }

    ensureComponent<LocalPlayerTag>(reg, entity);

    auto& transform = ensureComponentRef<TransformComponent>(reg, entity, position, 1.62f);
    transform.position = position;
    transform.eyeHeight = 1.62f;

    auto& velocityComponent = ensureComponentRef<VelocityComponent>(reg, entity);
    velocityComponent.velocity = velocity;

    ensureComponent<CameraStateComponent>(reg, entity);
    ensureComponent<BlockActionIntentComponent>(reg, entity);
    ensureComponent<BlockTargetComponent>(reg, entity);
    ensureComponent<BlockInteractionRuntimeComponent>(reg, entity);
    ensureComponent<MeleeAttackComponent>(reg, entity);
    ensureComponent<HealthComponent>(reg, entity);
    ensureComponent<HurtEffectComponent>(reg, entity);
    ensureComponent<InventoryComponent>(reg, entity);

    if (!reg.all_of<InventoryDataComponent>(entity)) {
        auto& inventoryData = reg.emplace<InventoryDataComponent>(entity);
        inventoryData.inventory.initializeDefaultLoadout();
    }
}

entt::entity EntityFactory::createZombie(GameplayRegistry& registry, const glm::vec3& position) {
    return MobModelFactory::createZombie(registry, position);
}

entt::entity EntityFactory::createZombie(entt::registry& registry, const glm::vec3& position) {
    const entt::entity zombie = registry.create();
    registry.emplace<MobTag>(zombie);
    registry.emplace<SkinTypeComponent>(zombie, SkinTypeComponent::Type::Mob);
    registry.emplace<TransformComponent>(zombie, position, 1.62f);
    registry.emplace<MobAIComponent>(zombie);
    registry.emplace<MoveIntentComponent>(zombie);
    registry.emplace<HealthComponent>(zombie, 20, 20);
    registry.emplace<NetworkSyncTag>(zombie);
    return zombie;
}

} // namespace ecs
