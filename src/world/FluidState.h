#pragma once

#include <cstdint>

#include "BlockStateRegistry.h"
#include "FluidRegistry.h"

struct DecodedFluid {
    FluidKind kind = FluidKind::None;
    uint8_t level = 0;
    bool falling = false;
    bool isSource = false;
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

}
