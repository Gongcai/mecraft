#ifndef MECRAFT_CREATIVEMODESTATE_H
#define MECRAFT_CREATIVEMODESTATE_H

#include "../states/GameplayState.h"

namespace physics {
class PhysicsSystem;
}

namespace ecs {
class GameplayRegistry;
}

class CreativeModeState final : public GameplayState {
public:
    explicit CreativeModeState(StateDependencies deps)
        : GameplayState(deps, CreativeModeRules::instance(), GameplayMode::Creative) {}
};

#endif //MECRAFT_CREATIVEMODESTATE_H
