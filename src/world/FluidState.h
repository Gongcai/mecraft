#pragma once

#include <cstdint>

#include "BlockStateRegistry.h"

namespace FluidState {

bool isWater(BlockID id);
bool isSource(BlockID id);
bool isFalling(BlockID id);
uint8_t level(BlockID id);
float surfaceHeight(BlockID id);
StateID makeWater(uint8_t level, bool falling);
bool canWaterReplace(BlockID id);
bool isSameWater(BlockID a, BlockID b);

}
