#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

#include "Block.h"
#include "BlockStateRegistry.h"

class World;

struct BlockRandomTickContext {
    World& world;
    glm::ivec3 pos{};
    BlockStateId state = NULL_BLOCK_STATE;
    uint64_t tickIndex = 0;
    uint32_t randomBits = 0;
};

namespace BlockRandomTick {

bool dispatch(const BlockRandomTickRule& rule, const BlockRandomTickContext& ctx);

} // namespace BlockRandomTick
