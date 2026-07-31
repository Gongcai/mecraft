#ifndef MECRAFT_AUDIO_LISTENER_SYNC_SYSTEM_H
#define MECRAFT_AUDIO_LISTENER_SYNC_SYSTEM_H

class BgmSystem;
class AudioEngine;
namespace ecs {
class GameplayRegistry;
}

/// Synchronizes audio listener position/orientation from player ECS state.
/// Extracted from Game::syncAudioListener() to decouple audio from Game.
class AudioListenerSyncSystem {
public:
    AudioListenerSyncSystem(BgmSystem& bgm, AudioEngine& audio) : m_bgmSystem(bgm), m_audioEngine(audio) {}

    /// Update audio systems and sync listener position from ECS.
    /// @param deltaTime Time since last frame
    /// @param reg The gameplay ECS registry (for player position queries)
    void update(float deltaTime, ecs::GameplayRegistry& reg);

private:
    BgmSystem& m_bgmSystem;
    AudioEngine& m_audioEngine;
};

#endif // MECRAFT_AUDIO_LISTENER_SYNC_SYSTEM_H
