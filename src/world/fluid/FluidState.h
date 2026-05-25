#pragma once

#include <cstdint>

#include "../block/BlockStateRegistry.h"
#include "FluidRegistry.h"

struct DecodedFluid {
    FluidKind kind = FluidKind::None;
    uint8_t level = 0;
    bool falling = false;
    bool isSource = false;
};

struct FluidCellView {
    StateID blockState = 0;  // AIR
    StateID fluidState = 0;  // AIR

    [[nodiscard]] bool hasBlock() const { return blockState != 0; }
    [[nodiscard]] bool hasFluid() const { return fluidState != 0; }
    [[nodiscard]] bool isEmpty() const { return !hasBlock() && !hasFluid(); }
};

namespace FluidState {

DecodedFluid decode(StateID id);
StateID encode(const DecodedFluid& fluid);
bool isFluidOf(StateID id, FluidKind kind);
bool isWater(BlockID id);
bool isSource(BlockID id);
bool isFalling(BlockID id);
uint8_t level(BlockID id);
float surfaceHeight(BlockID id);
StateID makeWater(uint8_t level, bool falling);
bool canReplace(const FluidDesc& desc, StateID occupant);
bool canCoexist(const FluidDesc& desc, StateID occupant);
bool canWaterReplace(BlockID id);
bool isSameWater(BlockID a, BlockID b);
StateID getFluidState(StateID cellState);
FluidCellView getCombinedCell(StateID cellState);

}
