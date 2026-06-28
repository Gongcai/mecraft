#pragma once

#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

#include "BlockStateRegistry.h"

struct PlacementContext {
    BlockID blockId = 0;
    glm::ivec3 hitNormal{};
    glm::vec3 hitPosition{};
    float playerYaw = 0.0f;
    bool isSneaking = false;
};

using PlacementStrategyFn = BlockStateId(*)(const PlacementContext&);

[[nodiscard]] bool tryMergePlacementStates(BlockStateId existingState,
                                           BlockStateId incomingState,
                                           BlockStateId& mergedState);
[[nodiscard]] bool canReplaceWithMergedPlacementResult(BlockStateId existingState,
                                                       BlockStateId resultState);

class PlacementStrategyRegistry {
public:
    static void registerStrategy(const std::string& name, PlacementStrategyFn fn);
    static PlacementStrategyFn getStrategy(const std::string& name);
    static void initBuiltinStrategies();

private:
    static std::unordered_map<std::string, PlacementStrategyFn> s_strategies;
};
