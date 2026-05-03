#ifndef MECRAFT_ECS_AUDIO_SYNC_SYSTEM_H
#define MECRAFT_ECS_AUDIO_SYNC_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

class AudioSyncSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_AUDIO_SYNC_SYSTEM_H
