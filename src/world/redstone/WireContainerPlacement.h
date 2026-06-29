#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

#include "../block/BlockStateRegistry.h"

class World;

namespace WireContainerPlacement {

enum class ApplyResult : uint8_t {
    NotWirePlacement,
    Rejected,
    Applied,
};

/// Returns true when `incomingWireState` can add a redstone wire part at `position`.
[[nodiscard]] bool isContainerPlacementTarget(BlockStateId existingState,
                                              BlockStateId incomingWireState);

/// Returns true when `incomingWireState` can add a redstone wire part to `existingState`.
[[nodiscard]] bool canApplyToBlockState(BlockStateId existingState,
                                        BlockStateId incomingWireState);

/// Returns true when `incomingWireState` can add a redstone wire part at `position`.
[[nodiscard]] bool canApply(const World& world,
                            const glm::ivec3& position,
                            BlockStateId incomingWireState);

/// Adds a redstone wire part at `position`, upgrading plain wires to a wire container when needed.
[[nodiscard]] ApplyResult apply(World& world,
                                const glm::ivec3& position,
                                BlockStateId incomingWireState);

} // namespace WireContainerPlacement
