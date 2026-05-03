#ifndef MECRAFT_ECS_AUDIO_COMPONENTS_H
#define MECRAFT_ECS_AUDIO_COMPONENTS_H

#include <string>

namespace ecs {

struct AudioSourceComponent {
    std::string clipName;
    bool loop = false;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool spatial = true;
    float referenceDistance = 8.0f;
    float rolloff = 1.0f;
    bool desiredPlaying = false;
    bool followTransform = true;
};

} // namespace ecs

#endif // MECRAFT_ECS_AUDIO_COMPONENTS_H
