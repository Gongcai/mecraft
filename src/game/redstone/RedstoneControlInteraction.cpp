#include "RedstoneControlInteraction.h"

#include "../interaction/BlockInteractionDispatcher.h"

namespace game::redstone {

bool isControlBlock(const BlockID blockId) {
    return game::interaction::hasBlockInteraction(blockId);
}

BlockStateId nextControlState(const BlockStateId currentState) {
    return game::interaction::nextBlockInteractionState(currentState);
}

} // namespace game::redstone
