#ifndef MECRAFT_ECS_BLOCK_SUPPORT_SYSTEM_H
#define MECRAFT_ECS_BLOCK_SUPPORT_SYSTEM_H

#include "../../ISystem.h"
#include <cstddef>

class World;

namespace ecs {

class GameplayRegistry;

/// Tick-rate system that validates block support rules.
/// When a block's supportRule is violated (e.g. torch on removed wall,
/// flower on removed ground), the block is broken and drops its item.
class BlockSupportSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;

    /// Validate and break unsupported blocks from a world neighbor-update queue.
    static size_t processWorldQueue(World& world, size_t budget = 1024);
    static size_t processWorldQueue(World& world, GameplayRegistry& registry, size_t budget = 1024);
};

} // namespace ecs

#endif // MECRAFT_ECS_BLOCK_SUPPORT_SYSTEM_H
