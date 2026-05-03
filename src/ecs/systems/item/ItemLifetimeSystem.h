#ifndef MECRAFT_ECS_ITEM_LIFETIME_SYSTEM_H
#define MECRAFT_ECS_ITEM_LIFETIME_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class ItemLifetimeSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<DropItemTag, LifetimeComponent>,
        std::tuple<LifetimeComponent>
    >;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_ITEM_LIFETIME_SYSTEM_H
