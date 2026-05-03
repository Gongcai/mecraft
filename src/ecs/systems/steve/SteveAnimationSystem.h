#ifndef MECRAFT_STEVE_ANIMATION_SYSTEM_H
#define MECRAFT_STEVE_ANIMATION_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

class SteveAnimationSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_STEVE_ANIMATION_SYSTEM_H
