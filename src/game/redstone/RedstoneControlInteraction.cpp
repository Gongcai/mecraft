#include "RedstoneControlInteraction.h"

#include "../interaction/BlockInteractionDispatcher.h"

namespace game::redstone {

bool isControlBlock(const BlockID blockId) {
    return game::interaction::hasBlockInteraction(blockId);
}

StateID nextControlState(const StateID currentState) {
    return game::interaction::nextBlockInteractionState(currentState);
}

} // namespace game::redstone
