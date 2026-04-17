#ifndef MECRAFT_ECS_AUDIO_SYNC_SYSTEM_H
#define MECRAFT_ECS_AUDIO_SYNC_SYSTEM_H

#include "../../GameplayRegistry.h"

class AudioEngine;

namespace ecs {

class AudioSyncSystem {
public:
    static void update(GameplayRegistry& registry, AudioEngine& audioEngine);
};

} // namespace ecs

#endif // MECRAFT_ECS_AUDIO_SYNC_SYSTEM_H
