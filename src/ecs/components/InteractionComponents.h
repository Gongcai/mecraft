#ifndef MECRAFT_ECS_INTERACTION_COMPONENTS_H
#define MECRAFT_ECS_INTERACTION_COMPONENTS_H

#include <cstdint>

#include <glm/glm.hpp>

#include "../../player/Inventory.h"

namespace ecs {

struct InventoryComponent {
    int selectedHotbarSlot = 0;
};

struct InventoryDataComponent {
    Inventory inventory;
};

struct BlockTargetComponent {
    bool hasTarget = false;
    glm::ivec3 targetBlock{};
    glm::ivec3 placeBlock{};
    glm::ivec3 hitNormal{};
};

struct BlockBreakComponent {
    bool active = false;
    glm::ivec3 blockPos{};
    float progress01 = 0.0f;
};

struct BlockInteractionRuntimeComponent {
    float placeCooldownRemaining = 0.0f;
    float creativeBreakCooldownRemaining = 0.0f;
    bool breakActive = false;
    glm::ivec3 breakBlockPos{};
    float breakElapsedMs = 0.0f;
    float breakRequiredMs = 0.0f;
    uint32_t heldItemSwingSequence = 0;
};

} // namespace ecs

#endif // MECRAFT_ECS_INTERACTION_COMPONENTS_H
