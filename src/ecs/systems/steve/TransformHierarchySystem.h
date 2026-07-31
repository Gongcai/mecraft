#ifndef MECRAFT_TRANSFORM_HIERARCHY_SYSTEM_H
#define MECRAFT_TRANSFORM_HIERARCHY_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class TransformHierarchySystem : public ISystem {
public:
    using Dependencies = SystemDependency<std::tuple<WorldTransformComponent, LocalTransformComponent,
                                                     TransformComponent, ParentComponent, ChildrenComponent>,
                                          std::tuple<WorldTransformComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_TRANSFORM_HIERARCHY_SYSTEM_H
