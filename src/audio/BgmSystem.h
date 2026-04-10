#ifndef MECRAFT_BGMSYSTEM_H
#define MECRAFT_BGMSYSTEM_H

#include <random>
#include <string>
#include <vector>

class AudioEngine;
class AudioSource;

class BgmSystem {
public:
    void init(AudioEngine& audioEngine);
    void update(float deltaTime);
    void shutdown();

    void setVolume(float volume);

private:
    void buildPlaylist();
    void tryStartPlayback();
    float getRandomDelaySeconds();

    AudioEngine* m_audioEngine = nullptr;
    std::vector<std::string> m_playlist;
    AudioSource* m_activeSource = nullptr;
    bool m_hasStartedPlayback = false;
    float m_cooldownRemaining = 0.0f;
    float m_volume = 0.35f;
    int m_lastIndex = -1;
    std::mt19937 m_rng{std::random_device{}()};
};

#endif //MECRAFT_BGMSYSTEM_H

