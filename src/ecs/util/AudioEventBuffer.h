#ifndef MECRAFT_ECS_AUDIO_EVENT_BUFFER_H
#define MECRAFT_ECS_AUDIO_EVENT_BUFFER_H

#include <string>

#include <glm/vec3.hpp>

#include "EventBus.h"

namespace ecs {

struct PlaySoundEvent {
    std::string clipName;
    glm::vec3 position{0.0f};
    bool spatial = true;
    float volume = 1.0f;
};

/// Audio event bus — use EventBus<PlaySoundEvent> directly.
using AudioEventBus = EventBus<PlaySoundEvent>;

/// Helper: ensure an AudioEventBus exists in the registry context and return a reference.
inline AudioEventBus& ensureAudioEventBus(GameplayRegistry& registry) {
    return ensureEventBus<PlaySoundEvent>(registry);
}

} // namespace ecs

#endif // MECRAFT_ECS_AUDIO_EVENT_BUFFER_H
