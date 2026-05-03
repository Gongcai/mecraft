#ifndef MECRAFT_ECS_VIEW_BOB_SYSTEM_H
#define MECRAFT_ECS_VIEW_BOB_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

/// Update view bob blend and offsets based on movement state.
class ViewBobSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_VIEW_BOB_SYSTEM_H
