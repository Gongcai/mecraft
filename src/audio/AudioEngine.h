//
// Created by Caiwe on 2026/3/29.
//

#ifndef MECRAFT_AUDIOENGINE_H
#define MECRAFT_AUDIOENGINE_H
#include <atomic>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <glm/vec3.hpp>

#include "AudioCatalog.h"
#include "AudioClip.h"
#include "AudioSource.h"

class AudioEngine {
public:
    void init();
    void update(float deltaTime);
    void shutdown();

    AudioClip* loadClip(const std::string& name);
    AudioClip* getClip(const std::string& name);
    bool loadCatalog(const std::string& catalogPath, const std::string& rootDirectory, const std::string& defaultGroup,
                     bool defaultPreload);
    [[nodiscard]] std::vector<std::string> getSoundNamesByGroup(const std::string& group) const;

    AudioSource* playClip(const std::string& clipName, glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f),
                          bool loop = false, float volume = 1.0f, bool spatial = true);
    void playSound2D(const std::string& clipName, float volume = 1.0f);

    void stopAll();
    void setMasterVolume(float volume);

    // Set by the OpenAL device event callback and consumed on the audio update thread.
    static std::atomic<bool> s_needDeviceReopen;

private:
    ALCdevice* _device = nullptr;
    ALCcontext* m_context = nullptr;
    float m_masterVolume = 1.0f;
    bool m_deviceSwitchSupported = false;

    audio::AudioCatalog m_catalog;
    std::unordered_map<std::string, std::unique_ptr<AudioClip>> m_clips;
    std::unordered_set<std::string> m_loadedCatalogs;
    std::vector<std::unique_ptr<AudioSource>> m_sources;
    std::mt19937 m_rng{std::random_device{}()};

    AudioSource* acquireSource();
    void releaseSource(AudioSource* source);

    void loadDefaultCatalog();
    void preloadCatalogSounds();
    AudioClip* loadClipVariant(const std::string& soundName, size_t variantIndex);
    [[nodiscard]] size_t chooseVariantIndex(const audio::SoundEntry& entry);
    bool initDeviceSwitchExtension();
    void checkDeviceSwitch();
};

#endif //MECRAFT_AUDIOENGINE_H
