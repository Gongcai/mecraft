#ifndef MECRAFT_TRANSFORM_HIERARCHY_SYSTEM_H
#define MECRAFT_TRANSFORM_HIERARCHY_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

class TransformHierarchySystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_TRANSFORM_HIERARCHY_SYSTEM_H
