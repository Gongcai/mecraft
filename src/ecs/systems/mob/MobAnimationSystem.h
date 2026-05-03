#ifndef MECRAFT_MOB_ANIMATION_SYSTEM_H
#define MECRAFT_MOB_ANIMATION_SYSTEM_H

namespace ecs {
class GameplayRegistry;
}

namespace ecs {

class MobAnimationSystem {
public:
    static void update(GameplayRegistry& registry, float dt);
};

} // namespace ecs

#endif // MECRAFT_MOB_ANIMATION_SYSTEM_H
