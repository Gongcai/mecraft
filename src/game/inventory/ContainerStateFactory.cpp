#include "ContainerStateFactory.h"

#include <stdexcept>
#include <unordered_map>

#include "ContainerBehaviorRegistry.h"
#include "ChestInventoryState.h"
#include "FurnaceState.h"
#include "WorkbenchState.h"
#include "../../ui/inventory/ContainerUiRegistry.h"

namespace {
using ContainerStateCreator = std::unique_ptr<IGameState> (*)(
    InventoryStateContext,
    const ui::ContainerUiDef&,
    const ContainerBehaviorDef&,
    const glm::ivec3&);

std::unique_ptr<IGameState> createCraftingTableState(InventoryStateContext deps,
                                                     const ui::ContainerUiDef& def,
                                                     const ContainerBehaviorDef& /*behavior*/,
                                                     const glm::ivec3& /*blockPosition*/) {
    return std::make_unique<WorkbenchState>(deps, def.id);
}

std::unique_ptr<IGameState> createFurnaceState(InventoryStateContext deps,
                                               const ui::ContainerUiDef& def,
                                               const ContainerBehaviorDef& /*behavior*/,
                                               const glm::ivec3& blockPosition) {
    return std::make_unique<FurnaceState>(deps, def.id, blockPosition);
}

std::unique_ptr<IGameState> createChestState(InventoryStateContext deps,
                                             const ui::ContainerUiDef& def,
                                             const ContainerBehaviorDef& /*behavior*/,
                                             const glm::ivec3& blockPosition) {
    return std::make_unique<ChestInventoryState>(deps, def.id, blockPosition);
}

const std::unordered_map<std::string, ContainerStateCreator>& containerStateCreators() {
    static const std::unordered_map<std::string, ContainerStateCreator> creators = {
        {"crafting_table", &createCraftingTableState},
        {"furnace", &createFurnaceState},
        {"chest", &createChestState},
    };
    return creators;
}
}

std::unique_ptr<IGameState> ContainerStateFactory::create(InventoryStateContext deps,
                                                          const std::string& containerUiId,
                                                          const glm::ivec3& blockPosition) {
    const ui::ContainerUiDef& def = ui::ContainerUiRegistry::require(containerUiId);
    const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(def.behavior);
    const auto& creators = containerStateCreators();
    const auto it = creators.find(behavior.handler);
    if (it == creators.end()) {
        throw std::runtime_error("Unknown container behavior handler for block interaction: " + behavior.handler);
    }
    return it->second(deps, def, behavior, blockPosition);
}
