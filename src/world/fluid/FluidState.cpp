#include "FluidState.h"

#include <algorithm>

#include "../block/Block.h"
#include "FluidRegistry.h"
#include "../block/PropIndices.h"

namespace {

uint8_t clampFluidLevel(const FluidDesc& desc, const uint8_t level) {
    return static_cast<uint8_t>(std::min<uint8_t>(level, desc.maxLevel));
}

uint16_t resolveLevelValue(const uint8_t level, const FluidDesc& desc) {
    switch (clampFluidLevel(desc, level)) {
        case 0: return PropIndices::LEVEL_0;
        case 1: return PropIndices::LEVEL_1;
        case 2: return PropIndices::LEVEL_2;
        case 3: return PropIndices::LEVEL_3;
        case 4: return PropIndices::LEVEL_4;
        case 5: return PropIndices::LEVEL_5;
        case 6: return PropIndices::LEVEL_6;
        case 7:
        default:
            return PropIndices::LEVEL_7;
    }
}

}

namespace FluidState {

DecodedFluid decode(const StateID id) {
    const FluidKind kind = FluidRegistry::kindForBlock(BlockStateRegistry::getBlockId(id));
    if (kind == FluidKind::None) {
        return {};
    }

    DecodedFluid fluid;
    fluid.kind = kind;

    if (PropIndices::LEVEL != PropIndices::INVALID) {
        const uint16_t value = BlockStateRegistry::getPropertyIndex(id, PropIndices::LEVEL);
        if (value == PropIndices::LEVEL_1) fluid.level = 1;
        else if (value == PropIndices::LEVEL_2) fluid.level = 2;
        else if (value == PropIndices::LEVEL_3) fluid.level = 3;
        else if (value == PropIndices::LEVEL_4) fluid.level = 4;
        else if (value == PropIndices::LEVEL_5) fluid.level = 5;
        else if (value == PropIndices::LEVEL_6) fluid.level = 6;
        else if (value == PropIndices::LEVEL_7) fluid.level = 7;
    }

    if (PropIndices::FALLING != PropIndices::INVALID) {
        const uint16_t value = BlockStateRegistry::getPropertyIndex(id, PropIndices::FALLING);
        fluid.falling = value == PropIndices::FALLING_TRUE;
    }

    fluid.isSource = !fluid.falling && fluid.level == 0;
    return fluid;
}

StateID encode(const DecodedFluid& fluid) {
    if (fluid.kind == FluidKind::None) {
        return RUNTIME_ID_NULL;
    }

    const FluidDesc& desc = FluidRegistry::get(fluid.kind);
    if (desc.blockId == RUNTIME_ID_NULL) {
        return RUNTIME_ID_NULL;
    }

    if (PropIndices::LEVEL == PropIndices::INVALID || PropIndices::FALLING == PropIndices::INVALID) {
        return desc.blockId;
    }

    StateID state = BlockStateRegistry::getDefaultState(desc.blockId);
    state = BlockStateRegistry::withProperty(state, PropIndices::LEVEL, resolveLevelValue(fluid.level, desc));
    state = BlockStateRegistry::withProperty(
        state,
        PropIndices::FALLING,
        fluid.falling ? PropIndices::FALLING_TRUE : PropIndices::FALLING_FALSE);
    return state;
}

bool isFluidOf(const StateID id, const FluidKind kind) {
    return decode(id).kind == kind;
}

bool isWater(const BlockID id) {
    return isFluidOf(id, FluidKind::Water);
}

bool isFalling(const BlockID id) {
    return decode(id).falling;
}

uint8_t level(const BlockID id) {
    return decode(id).level;
}

bool isSource(const BlockID id) {
    return decode(id).isSource;
}

float surfaceHeight(const BlockID id) {
    const DecodedFluid fluid = decode(id);
    if (fluid.kind == FluidKind::None) {
        return 0.0f;
    }

    const FluidDesc& desc = FluidRegistry::get(fluid.kind);
    if (fluid.falling && fluid.level == 0) {
        return 1.0f;
    }

    return 1.0f - static_cast<float>(fluid.level) / static_cast<float>(desc.maxLevel + 1);
}

StateID makeWater(const uint8_t requestedLevel, const bool falling) {
    return encode(DecodedFluid{FluidKind::Water, requestedLevel, falling, !falling && requestedLevel == 0});
}

bool canReplace(const FluidDesc& desc, const StateID occupant) {
    return occupant == RUNTIME_ID_NULL || decode(occupant).kind == desc.kind;
}

bool canCoexist(const FluidDesc& desc, const StateID occupant) {
    if (desc.kind == FluidKind::None || occupant == RUNTIME_ID_NULL) {
        return false;
    }
    if (decode(occupant).kind != FluidKind::None) {
        return false;
    }
    return BlockRegistry::getFast(occupant).allowsFluidCoexistence;
}

bool canWaterReplace(const BlockID id) {
    return canReplace(FluidRegistry::get(FluidKind::Water), id);
}

bool isSameWater(const BlockID a, const BlockID b) {
    return isFluidOf(a, FluidKind::Water) && isFluidOf(b, FluidKind::Water);
}

StateID getFluidState(const StateID cellState) {
    return decode(cellState).kind == FluidKind::None ? RUNTIME_ID_NULL : cellState;
}

FluidCellView getCombinedCell(const StateID cellState) {
    if (decode(cellState).kind != FluidKind::None) {
        return FluidCellView{RUNTIME_ID_NULL, cellState};
    }
    return FluidCellView{cellState, RUNTIME_ID_NULL};
}

}
