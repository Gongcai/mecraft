#ifndef MECRAFT_TRANSFORM_HIERARCHY_SYSTEM_H
#define MECRAFT_TRANSFORM_HIERARCHY_SYSTEM_H

#include "../../GameplayRegistry.h"

namespace ecs {

class TransformHierarchySystem {
public:
    static void update(GameplayRegistry& registry);
};

} // namespace ecs

#endif // MECRAFT_TRANSFORM_HIERARCHY_SYSTEM_H
