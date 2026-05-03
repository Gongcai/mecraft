#ifndef MECRAFT_ECS_ITEM_MERGE_SYSTEM_H
#define MECRAFT_ECS_ITEM_MERGE_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

class ItemMergeSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_ITEM_MERGE_SYSTEM_H
