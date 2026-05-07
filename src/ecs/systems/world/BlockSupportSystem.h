#ifndef MECRAFT_ECS_BLOCK_SUPPORT_SYSTEM_H
#define MECRAFT_ECS_BLOCK_SUPPORT_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

/// Tick-rate system that validates block support rules.
/// When a block's supportRule is violated (e.g. torch on removed wall,
/// flower on removed ground), the block is broken and drops its item.
class BlockSupportSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_BLOCK_SUPPORT_SYSTEM_H
