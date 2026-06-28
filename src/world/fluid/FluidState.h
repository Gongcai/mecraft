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
    BlockStateId blockState = NULL_BLOCK_STATE;
    BlockStateId fluidState = NULL_BLOCK_STATE;

    [[nodiscard]] bool hasBlock() const { return blockState != NULL_BLOCK_STATE; }
    [[nodiscard]] bool hasFluid() const { return fluidState != NULL_BLOCK_STATE; }
    [[nodiscard]] bool isEmpty() const { return !hasBlock() && !hasFluid(); }
};

namespace FluidState {

DecodedFluid decode(BlockStateId id);
BlockStateId encode(const DecodedFluid& fluid);
bool isFluidOf(BlockStateId id, FluidKind kind);
bool isWater(BlockStateId id);
bool isSource(BlockStateId id);
bool isFalling(BlockStateId id);
uint8_t level(BlockStateId id);
float surfaceHeight(BlockStateId id);
BlockStateId makeWater(uint8_t level, bool falling);
bool canReplace(const FluidDesc& desc, BlockStateId occupant);
bool canCoexist(const FluidDesc& desc, BlockStateId occupant);
bool canWaterReplace(BlockStateId id);
bool isSameWater(BlockStateId a, BlockStateId b);
BlockStateId getFluidState(BlockStateId cellState);
FluidCellView getCombinedCell(BlockStateId cellState);

}
