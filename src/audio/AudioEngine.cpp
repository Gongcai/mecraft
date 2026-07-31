//
// Created by Caiwe on 2026/3/29.
//

#include "AudioEngine.h"
#include "../Diagnostics.h"
#include "AudioFileDiscovery.h"
#include "Paths.h"
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace {

LPALCEVENTCALLBACKSOFT g_alcEventCallbackSoft = nullptr;
LPALCEVENTCONTROLSOFT g_alcEventControlSoft = nullptr;
LPALCREOPENDEVICESOFT g_alcReopenDeviceSoft = nullptr;

// Receives OpenAL device events from the OpenAL internal thread.
void ALC_APIENTRY onDeviceEvent(ALCenum eventType, ALCenum deviceType, ALCdevice* device, ALCsizei length,
                                const ALCchar* message, void* userPtr) noexcept {
    (void)deviceType;
    (void)device;
    (void)length;
    (void)message;
    (void)userPtr;

    if (eventType == ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT) {
#ifdef MECRAFT_ENABLE_CONSOLE_OUTPUT
        MECRAFT_LOG_STREAM(std::cout << "[Audio] 系统默认音频设备已更改: " << (message ? message : "unknown")
                                     << std::endl);
#endif
        AudioEngine::s_needDeviceReopen = true;
    }
}

} // namespace

std::atomic<bool> AudioEngine::s_needDeviceReopen{false};

void AudioEngine::init() {
    // Open the default audio device.
    _device = alcOpenDevice(nullptr);
    if (!_device) {
        MECRAFT_LOG_STREAM(std::cerr << "[Audio] Failed to open audio device" << std::endl);
        return;
    }

    // Create an OpenAL context for all game audio sources.
    m_context = alcCreateContext(_device, nullptr);
    if (!m_context) {
        MECRAFT_LOG_STREAM(std::cerr << "[Audio] Failed to create audio context" << std::endl);
        alcCloseDevice(_device);
        _device = nullptr;
        return;
    }

    // Make the context current before creating buffers or sources.
    if (!alcMakeContextCurrent(m_context)) {
        MECRAFT_LOG_STREAM(std::cerr << "[Audio] Failed to make context current" << std::endl);
        alcDestroyContext(m_context);
        alcCloseDevice(_device);
        m_context = nullptr;
        _device = nullptr;
        return;
    }

#ifdef MECRAFT_ENABLE_CONSOLE_OUTPUT
    MECRAFT_LOG_STREAM(std::cout << "[Audio] AudioEngine initialized" << std::endl);
#endif

    // Enable automatic device switching when the runtime supports it.
    initDeviceSwitchExtension();

    // Load the sound catalog; runtime sound identifiers are provided only by the manifest.
    loadDefaultCatalog();
}

void AudioEngine::update(const float deltaTime) {
    // Handle pending default device changes before pruning stopped sources.
    checkDeviceSwitch();

    // AudioEngine owns device and source lifetimes; BGM scheduling is driven by external systems.
    (void)deltaTime;

    // Remove sources that have finished playback.
    for (auto it = m_sources.begin(); it != m_sources.end();) {
        if ((*it)->isStopped()) {
            it = m_sources.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioEngine::shutdown() {
    // Stop all sources and detach their buffers before destroying clips.
    for (auto& source : m_sources) {
        source->stop();
        source->setClip(nullptr);
    }

    // Release all source objects.
    m_sources.clear();

    // Release all decoded clips.
    m_clips.clear();

    // Destroy the OpenAL context after all project-owned audio objects are released.
    if (m_context) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(m_context);
        m_context = nullptr;
    }

    // Close the audio device last.
    if (_device) {
        alcCloseDevice(_device);
        _device = nullptr;
    }

#ifdef MECRAFT_ENABLE_CONSOLE_OUTPUT
    MECRAFT_LOG_STREAM(std::cout << "[Audio] AudioEngine shutdown" << std::endl);
#endif
}

AudioClip* AudioEngine::loadClip(const std::string& name) {
    const audio::SoundEntry* entry = m_catalog.find(name);
    if (entry == nullptr) {
        MECRAFT_LOG_STREAM(std::cerr << "[Audio] Sound event not found in catalog: " << name << std::endl);
        return nullptr;
    }
    return loadClipVariant(name, 0);
}

AudioClip* AudioEngine::getClip(const std::string& name) {
    const audio::SoundEntry* entry = m_catalog.find(name);
    if (entry == nullptr || entry->variants.empty()) {
        return nullptr;
    }

    const std::string cacheKey = audio::pathToUtf8(entry->variants[0].filePath.lexically_normal());
    auto it = m_clips.find(cacheKey);
    if (it != m_clips.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool AudioEngine::loadCatalog(const std::string& catalogPath, const std::string& rootDirectory,
                              const std::string& defaultGroup, const bool defaultPreload) {
    const fs::path manifestPath = catalogPath;
    const std::string catalogKey = audio::pathToUtf8(manifestPath.lexically_normal());
    if (m_loadedCatalogs.find(catalogKey) != m_loadedCatalogs.end()) {
        return true;
    }

    std::string error;
    if (!m_catalog.loadFromFile(manifestPath, rootDirectory, defaultGroup, defaultPreload, error)) {
        MECRAFT_LOG_STREAM(std::cerr << "[Audio] " << error << std::endl);
        return false;
    }

    m_loadedCatalogs.insert(catalogKey);
    preloadCatalogSounds();
#ifdef MECRAFT_ENABLE_CONSOLE_OUTPUT
    MECRAFT_LOG_STREAM(std::cout << "[Audio] Loaded audio catalog: " << catalogKey << " (" << m_catalog.size()
                                 << " sound event(s))" << std::endl);
#endif
    return true;
}

std::vector<std::string> AudioEngine::getSoundNamesByGroup(const std::string& group) const {
    return m_catalog.soundIdsByGroup(group);
}

AudioSource* AudioEngine::playClip(const std::string& clipName, glm::vec3 position, bool loop, float volume,
                                   bool spatial) {
    const audio::SoundEntry* entry = m_catalog.find(clipName);
    if (entry == nullptr) {
        MECRAFT_LOG_STREAM(std::cerr << "[Audio] Sound event not found in catalog: " << clipName << std::endl);
        return nullptr;
    }

    AudioClip* clip = loadClipVariant(clipName, chooseVariantIndex(*entry));
    if (clip == nullptr) {
        return nullptr;
    }

    AudioSource* source = acquireSource();
    if (!source) {
        return nullptr;
    }

    source->setClip(clip);
    source->setPosition(position);
    source->setVolume(volume * entry->volume * m_masterVolume);
    source->setLooping(loop);
    if (!spatial) {
        // Non-spatial sounds must not attenuate with listener distance.
        source->setRolloffFactor(0.0f);
    }
    source->play();

    return source;
}

void AudioEngine::playSound2D(const std::string& clipName, float volume) {
    playClip(clipName, glm::vec3(0.0f), false, volume, false);
}

void AudioEngine::stopAll() {
    for (auto& source : m_sources) {
        source->stop();
    }
}

void AudioEngine::setMasterVolume(float volume) {
    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
}

AudioSource* AudioEngine::acquireSource() {
    auto source = std::make_unique<AudioSource>();
    if (!source->isValid()) {
        return nullptr;
    }
    AudioSource* ptr = source.get();
    m_sources.push_back(std::move(source));
    return ptr;
}

void AudioEngine::releaseSource(AudioSource* source) {
    for (auto it = m_sources.begin(); it != m_sources.end(); ++it) {
        if (it->get() == source) {
            m_sources.erase(it);
            return;
        }
    }
}

void AudioEngine::loadDefaultCatalog() {
    loadCatalog(SOUNDS_CATALOG_PATH, SOUNDS_DIR, "sfx", true);
}

void AudioEngine::preloadCatalogSounds() {
#ifdef MECRAFT_ENABLE_CONSOLE_OUTPUT
    const size_t loadedBefore = m_clips.size();
#endif
    for (const std::string& soundName : m_catalog.soundIds()) {
        const audio::SoundEntry* entry = m_catalog.find(soundName);
        if (entry == nullptr || !entry->preload) {
            continue;
        }

        for (size_t i = 0; i < entry->variants.size(); ++i) {
            loadClipVariant(soundName, i);
        }
    }

#ifdef MECRAFT_ENABLE_CONSOLE_OUTPUT
    const size_t loadedCount = m_clips.size() - loadedBefore;
    MECRAFT_LOG_STREAM(std::cout << "[Audio] Preloaded " << loadedCount << " audio clip variant(s)" << std::endl);
#endif
}

AudioClip* AudioEngine::loadClipVariant(const std::string& soundName, const size_t variantIndex) {
    const audio::SoundEntry* entry = m_catalog.find(soundName);
    if (entry == nullptr) {
        MECRAFT_LOG_STREAM(std::cerr << "[Audio] Sound event not found in catalog: " << soundName << std::endl);
        return nullptr;
    }
    if (variantIndex >= entry->variants.size()) {
        MECRAFT_LOG_STREAM(std::cerr << "[Audio] Sound event variant out of range: " << soundName << std::endl);
        return nullptr;
    }

    const audio::SoundVariant& variant = entry->variants[variantIndex];
    const std::string cacheKey = audio::pathToUtf8(variant.filePath.lexically_normal());
    auto it = m_clips.find(cacheKey);
    if (it != m_clips.end()) {
        return it->second.get();
    }

    auto clip = std::make_unique<AudioClip>(audio::pathToUtf8(variant.filePath));
    if (!clip->isValid()) {
        return nullptr;
    }

    AudioClip* ptr = clip.get();
    m_clips.emplace(cacheKey, std::move(clip));
    return ptr;
}

size_t AudioEngine::chooseVariantIndex(const audio::SoundEntry& entry) {
    if (entry.variants.size() <= 1) {
        return 0;
    }

    float totalWeight = 0.0f;
    for (const audio::SoundVariant& variant : entry.variants) {
        totalWeight += std::max(0.0f, variant.weight);
    }
    if (totalWeight <= 0.0f) {
        return 0;
    }

    std::uniform_real_distribution<float> dist(0.0f, totalWeight);
    float cursor = dist(m_rng);
    for (size_t i = 0; i < entry.variants.size(); ++i) {
        cursor -= std::max(0.0f, entry.variants[i].weight);
        if (cursor <= 0.0f) {
            return i;
        }
    }
    return entry.variants.size() - 1;
}

// BGM scheduling is implemented by a separate system.

bool AudioEngine::initDeviceSwitchExtension() {
    if (!alcIsExtensionPresent(_device, "ALC_SOFT_system_events") ||
        !alcIsExtensionPresent(_device, "ALC_SOFT_reopen_device")) {
#ifdef MECRAFT_ENABLE_CONSOLE_OUTPUT
        MECRAFT_LOG_STREAM(std::cout << u8"[Audio] 设备自动切换扩展不可用" << std::endl);
#endif
        return false;
    }

    g_alcEventCallbackSoft = (LPALCEVENTCALLBACKSOFT)alcGetProcAddress(_device, "alcEventCallbackSOFT");
    g_alcEventControlSoft = (LPALCEVENTCONTROLSOFT)alcGetProcAddress(_device, "alcEventControlSOFT");
    g_alcReopenDeviceSoft = (LPALCREOPENDEVICESOFT)alcGetProcAddress(_device, "alcReopenDeviceSOFT");

    if (!g_alcEventCallbackSoft || !g_alcEventControlSoft || !g_alcReopenDeviceSoft) {
        MECRAFT_LOG_STREAM(std::cerr << "[Audio] 获取扩展函数指针失败" << std::endl);
        return false;
    }

    // Register the callback that marks device reopen requests.
    g_alcEventCallbackSoft(onDeviceEvent, nullptr);

    // Listen only for default output device changes.
    ALCenum eventToListen = ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT;
    g_alcEventControlSoft(1, &eventToListen, ALC_TRUE);

    m_deviceSwitchSupported = true;
#ifdef MECRAFT_ENABLE_CONSOLE_OUTPUT
    MECRAFT_LOG_STREAM(std::cout << "[Audio] 设备自动切换已启用" << std::endl);
#endif
    return true;
}

void AudioEngine::checkDeviceSwitch() {
    if (!m_deviceSwitchSupported)
        return;

    if (s_needDeviceReopen.exchange(false)) {
#ifdef MECRAFT_ENABLE_CONSOLE_OUTPUT
        MECRAFT_LOG_STREAM(std::cout << "[Audio] 正在迁移音频上下文到新设备..." << std::endl);
#endif

        // alcReopenDeviceSOFT preserves existing AL objects including buffers and sources.
        if (!g_alcReopenDeviceSoft(_device, nullptr, nullptr)) {
            MECRAFT_LOG_STREAM(std::cerr << "[Audio] 设备迁移失败！" << std::endl);
        } else {
#ifdef MECRAFT_ENABLE_CONSOLE_OUTPUT
            MECRAFT_LOG_STREAM(std::cout << "[Audio] 设备迁移成功，音频已无缝切换" << std::endl);
#endif
        }
    }
}
