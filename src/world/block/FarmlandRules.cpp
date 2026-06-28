#include "FarmlandRules.h"

#include "../World.h"
#include "../fluid/FluidState.h"
#include "BlockStateRegistry.h"

namespace FarmlandRules {

bool hasHydrationWater(const World& world, const glm::ivec3& farmlandPos) {
    for (int dz = -HYDRATION_HORIZONTAL_RADIUS; dz <= HYDRATION_HORIZONTAL_RADIUS; ++dz) {
        for (int dx = -HYDRATION_HORIZONTAL_RADIUS; dx <= HYDRATION_HORIZONTAL_RADIUS; ++dx) {
            for (int dy = 0; dy <= HYDRATION_VERTICAL_SEARCH_TOP; ++dy) {
                const glm::ivec3 sample = farmlandPos + glm::ivec3(dx, dy, dz);
                if (FluidState::decode(world.getFluidState(sample.x, sample.y, sample.z)).kind == FluidKind::Water) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool hasCropAbove(const World& world, const glm::ivec3& farmlandPos) {
    const glm::ivec3 above = farmlandPos + glm::ivec3(0, 1, 0);
    const BlockStateId aboveState = world.getBlockState(above.x, above.y, above.z);
    if (aboveState == NULL_BLOCK_STATE) {
        return false;
    }

    const BlockID aboveBlock = BlockStateRegistry::getBlockId(aboveState);
    return BlockRegistry::getFast(aboveBlock).supportRule == "farmland";
}

} // namespace FarmlandRules
