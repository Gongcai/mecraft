#ifndef MECRAFT_MOB_ANIMATION_SYSTEM_H
#define MECRAFT_MOB_ANIMATION_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

class MobAnimationSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_MOB_ANIMATION_SYSTEM_H
