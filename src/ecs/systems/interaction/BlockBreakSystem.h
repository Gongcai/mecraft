#ifndef MECRAFT_ECS_BLOCK_BREAK_SYSTEM_H
#define MECRAFT_ECS_BLOCK_BREAK_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

/// Processes block breaking: creative instant-break and survival timed-break.
class BlockBreakSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<LocalPlayerTag, BlockActionIntentComponent, BlockTargetComponent>,
        std::tuple<BlockBreakComponent, BlockInteractionRuntimeComponent, InventoryComponent, InventoryDataComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_BLOCK_BREAK_SYSTEM_H
