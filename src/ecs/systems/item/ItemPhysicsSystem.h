#ifndef MECRAFT_ECS_ITEM_PHYSICS_SYSTEM_H
#define MECRAFT_ECS_ITEM_PHYSICS_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

class ItemPhysicsSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_ITEM_PHYSICS_SYSTEM_H
