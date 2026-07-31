#include "AudioListenerSyncSystem.h"
#include "../../audio/BgmSystem.h"
#include "../../audio/AudioEngine.h"
#include "../../audio/AudioListener.h"
#include "../../ecs/util/PlayerQuery.h"

void AudioListenerSyncSystem::update(float deltaTime, ecs::GameplayRegistry& reg) {
    // Update BGM before AudioEngine cleanup so track-end detection keeps a valid source pointer.
    m_bgmSystem.update(deltaTime);
    m_audioEngine.update(deltaTime);

    // Sync listener position/orientation from player ECS state
    ecs::PlayerQuery query(reg);
    AudioListener::setPosition(query.getEyePosition());
    AudioListener::setOrientation(query.getCameraFront(), query.getCameraUp());
}
