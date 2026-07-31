#ifndef MECRAFT_ECS_AUDIO_SYNC_SYSTEM_H
#define MECRAFT_ECS_AUDIO_SYNC_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class AudioSyncSystem : public ISystem {
public:
    using Dependencies =
        SystemDependency<std::tuple<AudioSourceComponent, TransformComponent>, std::tuple<AudioSourceComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_AUDIO_SYNC_SYSTEM_H
