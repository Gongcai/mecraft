#include "ContainerStateFactory.h"

#include <stdexcept>

#include "ChestInventoryState.h"
#include "FurnaceState.h"
#include "WorkbenchState.h"
#include "../../ui/inventory/ContainerUiRegistry.h"

std::unique_ptr<IGameState> ContainerStateFactory::create(InventoryStateContext deps,
                                                          const std::string& containerUiId,
                                                          const glm::ivec3& blockPosition) {
    const ui::ContainerUiDef& def = ui::ContainerUiRegistry::require(containerUiId);
    if (def.behavior == "crafting_table") {
        return std::make_unique<WorkbenchState>(deps, def.id);
    }
    if (def.behavior == "furnace") {
        return std::make_unique<FurnaceState>(deps, def.id, blockPosition);
    }
    if (def.behavior == "chest") {
        return std::make_unique<ChestInventoryState>(deps, def.id, blockPosition);
    }
    throw std::runtime_error("Unknown container UI behavior for block interaction: " + def.behavior);
}
