#ifndef MECRAFT_MOB_AI_SYSTEM_H
#define MECRAFT_MOB_AI_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class MobAISystem : public ISystem {
public:
    using Dependencies = SystemDependency<std::tuple<MobTag, MobAIComponent, TransformComponent>,
                                          std::tuple<MobAIComponent, MoveIntentComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_MOB_AI_SYSTEM_H
