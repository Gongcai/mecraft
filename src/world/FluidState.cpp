#include "FluidState.h"

#include <algorithm>

#include "PropIndices.h"

namespace {

uint8_t clampWaterLevel(const uint8_t level) {
    return static_cast<uint8_t>(std::min<uint8_t>(level, 7));
}

uint16_t resolveLevelValue(const uint8_t level) {
    switch (clampWaterLevel(level)) {
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

bool isWater(const BlockID id) {
    return BlockStateRegistry::getBlockId(id) == BlockIds::WATER;
}

bool isFalling(const BlockID id) {
    if (!isWater(id) || PropIndices::FALLING == PropIndices::INVALID) {
        return false;
    }
    const uint16_t value = BlockStateRegistry::getPropertyIndex(id, PropIndices::FALLING);
    if (value == BlockStateRegistry::INVALID_INDEX) {
        return false;
    }
    return value == PropIndices::FALLING_TRUE;
}

uint8_t level(const BlockID id) {
    if (!isWater(id) || PropIndices::LEVEL == PropIndices::INVALID) {
        return 0;
    }

    const uint16_t value = BlockStateRegistry::getPropertyIndex(id, PropIndices::LEVEL);
    if (value == BlockStateRegistry::INVALID_INDEX) {
        return 0;
    }

    if (value == PropIndices::LEVEL_1) return 1;
    if (value == PropIndices::LEVEL_2) return 2;
    if (value == PropIndices::LEVEL_3) return 3;
    if (value == PropIndices::LEVEL_4) return 4;
    if (value == PropIndices::LEVEL_5) return 5;
    if (value == PropIndices::LEVEL_6) return 6;
    if (value == PropIndices::LEVEL_7) return 7;
    return 0;
}

bool isSource(const BlockID id) {
    return isWater(id) && !isFalling(id) && level(id) == 0;
}

float surfaceHeight(const BlockID id) {
    if (!isWater(id)) {
        return 0.0f;
    }
    if (isFalling(id) && level(id) == 0) {
        return 1.0f;
    }
    return 1.0f - static_cast<float>(level(id)) / 8.0f;
}

StateID makeWater(const uint8_t requestedLevel, const bool falling) {
    if (PropIndices::LEVEL == PropIndices::INVALID || PropIndices::FALLING == PropIndices::INVALID) {
        return BlockIds::WATER;
    }

    StateID state = BlockStateRegistry::getDefaultState(BlockIds::WATER);
    state = BlockStateRegistry::withProperty(state, PropIndices::LEVEL, resolveLevelValue(requestedLevel));
    state = BlockStateRegistry::withProperty(state,
                                             PropIndices::FALLING,
                                             falling ? PropIndices::FALLING_TRUE : PropIndices::FALLING_FALSE);
    return state;
}

bool canWaterReplace(const BlockID id) {
    return id == BlockIds::AIR || isWater(id);
}

bool isSameWater(const BlockID a, const BlockID b) {
    return isWater(a) && isWater(b);
}

}
