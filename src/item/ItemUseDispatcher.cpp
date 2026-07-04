#include "ItemUseDispatcher.h"

#include <cstdlib>
#include <iostream>

#include "../world/IWorldView.h"
#include "../world/fluid/FluidRegistry.h"
#include "../world/fluid/FluidState.h"

namespace {
bool hasEmptySpaceAbove(const IWorldView& worldView, const glm::ivec3& pos) {
    const glm::ivec3 above = pos + glm::ivec3(0, 1, 0);
    return worldView.getBlockState(above.x, above.y, above.z) == NULL_BLOCK_STATE &&
           worldView.getFluidState(above.x, above.y, above.z) == NULL_BLOCK_STATE;
}

bool canPlaceFluidAt(const IWorldView& worldView, const glm::ivec3& pos, const FluidDesc& fluidDesc) {
    if (!worldView.isChunkLoadedForBlock(pos.x, pos.y, pos.z)) {
        return false;
    }

    const BlockStateId blockState = worldView.getBlockState(pos.x, pos.y, pos.z);
    return FluidState::canReplace(fluidDesc, blockState) ||
           FluidState::canCoexist(fluidDesc, blockState);
}
}

namespace ItemUseDispatcher {
bool isWithinReach(const glm::vec3& actorPosition,
                   const glm::ivec3& blockPosition,
                   const float maxDistance) {
    const glm::vec3 blockCenter = glm::vec3(blockPosition) + glm::vec3(0.5f);
    const glm::vec3 diff = actorPosition - blockCenter;
    const float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
    return distSq <= maxDistance * maxDistance;
}

bool canApplyBlockRule(const IWorldView& worldView,
                       const glm::ivec3& blockPosition,
                       const ItemUseRule& rule) {
    const BlockStateId targetState =
        worldView.getBlockState(blockPosition.x, blockPosition.y, blockPosition.z);
    const BlockID targetBlock = BlockStateRegistry::getBlockId(targetState);
    if (!ItemUseRules::matchesBlock(rule, targetBlock)) {
        return false;
    }
    return !rule.requiresEmptyAbove || hasEmptySpaceAbove(worldView, blockPosition);
}

bool canPickupFluid(const IWorldView& worldView,
                    const glm::ivec3& blockPosition,
                    const ItemUseRule& rule) {
    const BlockStateId fluidState =
        worldView.getFluidState(blockPosition.x, blockPosition.y, blockPosition.z);
    const DecodedFluid fluid = FluidState::decode(fluidState);
    if (fluid.kind == FluidKind::None) {
        return false;
    }
    if (rule.requiresSourceFluid && !fluid.isSource) {
        return false;
    }

    const BlockID fluidBlock = BlockStateRegistry::getBlockId(fluidState);
    return ItemUseRules::matchesBlock(rule, fluidBlock);
}

bool canPlaceFluid(const IWorldView& worldView,
                   const glm::ivec3& blockPosition,
                   const ItemUseRule& rule) {
    if (!rule.requiresFluidPlacement) {
        return true;
    }

    const FluidKind fluidKind = FluidRegistry::kindForBlock(rule.resultBlock);
    const FluidDesc* fluidDesc = FluidRegistry::tryGet(fluidKind);
    if (fluidDesc == nullptr) {
        std::cerr << "Item use rule references unsupported fluid block.\n";
        return false;
    }
    return canPlaceFluidAt(worldView, blockPosition, *fluidDesc);
}

BlockStateId makeSourceFluidState(const BlockID fluidBlock) {
    const FluidKind fluidKind = FluidRegistry::kindForBlock(fluidBlock);
    if (FluidRegistry::tryGet(fluidKind) == nullptr) {
        std::cerr << "Item use rule references unsupported fluid block.\n";
        std::abort();
    }
    return FluidState::encode(DecodedFluid{fluidKind, 0, false, true});
}
}
