#ifndef MECRAFT_ECS_AUDIO_EVENT_BUFFER_H
#define MECRAFT_ECS_AUDIO_EVENT_BUFFER_H

#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include "../GameplayRegistry.h"

namespace ecs {

struct PlaySoundEvent {
    std::string clipName;
    glm::vec3 position{0.0f};
    bool spatial = true;
    float volume = 1.0f;
};

struct AudioEventBuffer {
    std::vector<PlaySoundEvent> playSoundEvents;
};

inline AudioEventBuffer& ensureAudioEventBuffer(GameplayRegistry& registry) {
    if (!registry.ctxHas<AudioEventBuffer>()) {
        registry.ctxSet<AudioEventBuffer>();
    }
    return registry.ctxGet<AudioEventBuffer>();
}

} // namespace ecs

#endif // MECRAFT_ECS_AUDIO_EVENT_BUFFER_H
