#include "ContainerStateFactory.h"

#include <iostream>
#include <unordered_map>

#include "ContainerBehaviorRegistry.h"
#include "DataDrivenContainerState.h"
#include "NetworkContainerState.h"
#include "SmeltingContainerState.h"
#include "WorkbenchState.h"
#include "../../ui/inventory/ContainerUiRegistry.h"

namespace {
using ContainerStateCreator = std::unique_ptr<IGameState> (*)(InventoryStateContext, const ui::ContainerUiDef&,
                                                              const ContainerBehaviorDef&, const glm::ivec3&);

std::unique_ptr<IGameState> createStorageState(InventoryStateContext deps, const ui::ContainerUiDef& def,
                                               const ContainerBehaviorDef& behavior, const glm::ivec3& blockPosition) {
    return std::make_unique<DataDrivenContainerState>(deps, def.id, behavior.id, blockPosition);
}

std::unique_ptr<IGameState> createSmeltingState(InventoryStateContext deps, const ui::ContainerUiDef& def,
                                                const ContainerBehaviorDef& behavior, const glm::ivec3& blockPosition) {
    return std::make_unique<SmeltingContainerState>(deps, def.id, behavior.id, blockPosition);
}

std::unique_ptr<IGameState> createCraftingState(InventoryStateContext deps, const ui::ContainerUiDef& def,
                                                const ContainerBehaviorDef& /*behavior*/,
                                                const glm::ivec3& /*blockPosition*/) {
    return std::make_unique<WorkbenchState>(deps, def.id);
}

const std::unordered_map<std::string, ContainerStateCreator>& containerStateCreators() {
    static const std::unordered_map<std::string, ContainerStateCreator> creators = {
        {"storage", &createStorageState},
        {"smelting", &createSmeltingState},
        {"crafting", &createCraftingState},
    };
    return creators;
}
} // namespace

std::unique_ptr<IGameState> ContainerStateFactory::create(InventoryStateContext deps, const std::string& containerUiId,
                                                          const glm::ivec3& blockPosition) {
    const ui::ContainerUiDef& def = ui::ContainerUiRegistry::require(containerUiId);
    const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(def.behavior);
    if (deps.isMultiplayer && (behavior.handler == "storage" || behavior.handler == "smelting")) {
        return std::make_unique<NetworkContainerState>(deps, def.id, blockPosition);
    }
    const auto& creators = containerStateCreators();
    const auto it = creators.find(behavior.handler);
    if (it == creators.end()) {
        std::cerr << "Unknown container behavior handler for block interaction: " << behavior.handler << '\n';
        return nullptr;
    }
    return it->second(deps, def, behavior, blockPosition);
}
