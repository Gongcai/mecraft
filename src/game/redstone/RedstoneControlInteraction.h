#ifndef MECRAFT_GAME_REDSTONE_CONTROL_INTERACTION_H
#define MECRAFT_GAME_REDSTONE_CONTROL_INTERACTION_H

#include "../../world/block/Block.h"
#include "../../world/block/BlockStateRegistry.h"

namespace game::redstone {

/// Return true when right-clicking the block should change its redstone control state.
[[nodiscard]] bool isControlBlock(BlockID blockId);

/// Compute the next state for a redstone control block after a player interaction.
/// @param currentState  Current block state containing the required interaction property.
/// @return Updated block state after the interaction.
[[nodiscard]] StateID nextControlState(StateID currentState);

} // namespace game::redstone

#endif // MECRAFT_GAME_REDSTONE_CONTROL_INTERACTION_H
