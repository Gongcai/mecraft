#ifndef MECRAFT_ECS_ENTITY_DEFINITION_REGISTRY_H
#define MECRAFT_ECS_ENTITY_DEFINITION_REGISTRY_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../../engine/registry/NamespacedId.h"
#include "../../item/Item.h"

namespace ecs {

struct MobAIDefinition {
    float wanderInterval = 3.0f;
    float wanderSpeed = 0.45f;
    float pursueSpeed = 0.85f;
    float acquisitionRange = 14.0f;
    float loseTargetRange = 20.0f;
    float attackRange = 1.35f;
    float attackCooldownSeconds = 1.1f;
    int attackDamage = 3;
};

struct MobPhysicsDefinition {
    glm::vec3 halfExtents{0.3f, 0.9f, 0.3f};
    glm::vec3 colliderOffset{0.0f, 0.9f, 0.0f};
    float eyeOffsetY = 1.62f;
};

struct MobDropDefinition {
    ItemID itemId = 0;
    uint32_t minCount = 1;
    uint32_t maxCount = 1;
};

struct MobEntityDefinition {
    NamespacedId id;
    std::string model = "zombie_humanoid";
    int health = 20;
    int maxHealth = 20;
    float eyeHeight = 1.62f;
    MobPhysicsDefinition physics;
    MobAIDefinition ai;
    std::vector<MobDropDefinition> drops;
};

class EntityDefinitionRegistry {
public:
    static EntityDefinitionRegistry& instance();

    bool ensureLoaded(std::string* error = nullptr);
    bool loadFromFile(const std::string& path, std::string* error = nullptr);
    void clear();

    [[nodiscard]] const MobEntityDefinition* findMob(std::string_view id) const;
    [[nodiscard]] const MobEntityDefinition* findMob(const NamespacedId& id) const;
    [[nodiscard]] std::size_t mobCount() const { return m_mobs.size(); }

private:
    bool m_loaded = false;
    std::unordered_map<NamespacedId, MobEntityDefinition> m_mobs;
};

} // namespace ecs

#endif // MECRAFT_ECS_ENTITY_DEFINITION_REGISTRY_H
