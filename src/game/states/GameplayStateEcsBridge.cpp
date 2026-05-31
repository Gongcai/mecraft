#include "GameplayStateEcsBridge.h"

#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/components/Components.h"
#include "../../player/Inventory.h"

#include <glm/vec3.hpp>

void GameplayStateEcsBridge::syncSelectedHotbarSlot(ecs::GameplayRegistry& registry, const Inventory& inventory) {
    auto view = registry.view<ecs::LocalPlayerTag, ecs::InventoryComponent>();
    for (auto e : view) {
        view.get<ecs::InventoryComponent>(e).selectedHotbarSlot = inventory.getSelectedSlot();
    }
}

void GameplayStateEcsBridge::resetBlockBreakSession(ecs::GameplayRegistry& registry) {
    auto view = registry.view<ecs::LocalPlayerTag>();
    for (auto e : view) {
        if (registry.has<ecs::BlockBreakComponent>(e)) {
            auto& blockBreak = registry.get<ecs::BlockBreakComponent>(e);
            blockBreak.active = false;
            blockBreak.blockPos = glm::ivec3{};
            blockBreak.progress01 = 0.0f;
        }
        if (registry.has<ecs::BlockInteractionRuntimeComponent>(e)) {
            auto& runtime = registry.get<ecs::BlockInteractionRuntimeComponent>(e);
            runtime.breakActive = false;
            runtime.breakBlockPos = glm::ivec3{};
            runtime.breakElapsedMs = 0.0f;
            runtime.breakRequiredMs = 0.0f;
        }
    }
}
