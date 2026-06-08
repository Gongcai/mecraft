//
// Created by Caiwe on 2026/3/29.
//

#include "AudioEngine.h"
#include "AudioFileDiscovery.h"
#include "Paths.h"
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

// OpenAL 扩展函数指针定义
LPALCEVENTCALLBACKSOFT alcEventCallbackSOFT = nullptr;
LPALCEVENTCONTROLSOFT alcEventControlSOFT = nullptr;
LPALCREOPENDEVICESOFT alcReopenDeviceSOFT = nullptr;

// 静态成员定义
std::atomic<bool> AudioEngine::s_needDeviceReopen{false};

// 设备切换回调（OpenAL 内部线程调用）
void ALC_APIENTRY OnDeviceEvent(ALCenum eventType, ALCenum deviceType,
                                 ALCdevice* device, ALCsizei length,
                                 const ALCchar* message, void* userPtr) noexcept{
    if (eventType == ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT) {
#ifdef MECRAFT_DEBUG
        std::cout << "[Audio] 系统默认音频设备已更改: " << (message ? message : "unknown") << std::endl;
#endif
        AudioEngine::s_needDeviceReopen = true;
    }
}

void AudioEngine::init() {
    // 打开默认音频设备
    _device = alcOpenDevice(nullptr);
    if (!_device) {
        std::cerr << "[Audio] Failed to open audio device" << std::endl;
        return;
    }

    // 创建上下文
    m_context = alcCreateContext(_device, nullptr);
    if (!m_context) {
        std::cerr << "[Audio] Failed to create audio context" << std::endl;
        alcCloseDevice(_device);
        _device = nullptr;
        return;
    }

    // 激活上下文
    if (!alcMakeContextCurrent(m_context)) {
        std::cerr << "[Audio] Failed to make context current" << std::endl;
        alcDestroyContext(m_context);
        alcCloseDevice(_device);
        m_context = nullptr;
        _device = nullptr;
        return;
    }

#ifdef MECRAFT_DEBUG
    std::cout << "[Audio] AudioEngine initialized" << std::endl;
#endif

    // 初始化设备切换扩展
    initDeviceSwitchExtension();

    // 加载音效 catalog；运行时音效名只来自 manifest。
    loadDefaultCatalog();
}

void AudioEngine::update(const float deltaTime) {
    // 检查并处理设备切换
    checkDeviceSwitch();

    // AudioEngine 只负责设备与 source 生命周期，BGM 逻辑由外部系统驱动。
    (void)deltaTime;

    // 清理已停止的 source
    for (auto it = m_sources.begin(); it != m_sources.end();) {
        if ((*it)->isStopped()) {
            it = m_sources.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioEngine::shutdown() {
    // 先停止所有声音并解绑 buffer
    for (auto& source : m_sources) {
        source->stop();
        source->setClip(nullptr);
    }

    // 清理所有 source
    m_sources.clear();

    // 清理所有 clip
    m_clips.clear();

    // 销毁 OpenAL 上下文
    if (m_context) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(m_context);
        m_context = nullptr;
    }

    // 关闭设备
    if (_device) {
        alcCloseDevice(_device);
        _device = nullptr;
    }

#ifdef MECRAFT_DEBUG
    std::cout << "[Audio] AudioEngine shutdown" << std::endl;
#endif
}

AudioClip* AudioEngine::loadClip(const std::string& name) {
    const audio::SoundEntry* entry = m_catalog.find(name);
    if (entry == nullptr) {
        std::cerr << "[Audio] Sound event not found in catalog: " << name << std::endl;
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

bool AudioEngine::loadCatalog(const std::string& catalogPath,
                              const std::string& rootDirectory,
                              const std::string& defaultGroup,
                              const bool defaultPreload) {
    const fs::path manifestPath = catalogPath;
    const std::string catalogKey = audio::pathToUtf8(manifestPath.lexically_normal());
    if (m_loadedCatalogs.find(catalogKey) != m_loadedCatalogs.end()) {
        return true;
    }

    std::string error;
    if (!m_catalog.loadFromFile(manifestPath, rootDirectory, defaultGroup, defaultPreload, error)) {
        std::cerr << "[Audio] " << error << std::endl;
        return false;
    }

    m_loadedCatalogs.insert(catalogKey);
    preloadCatalogSounds();
#ifdef MECRAFT_DEBUG
    std::cout << "[Audio] Loaded audio catalog: " << catalogKey
              << " (" << m_catalog.size() << " sound event(s))" << std::endl;
#endif
    return true;
}

std::vector<std::string> AudioEngine::getSoundNamesByGroup(const std::string& group) const {
    return m_catalog.soundIdsByGroup(group);
}

AudioSource* AudioEngine::playClip(const std::string& clipName, glm::vec3 position, bool loop, float volume, bool spatial) {
    const audio::SoundEntry* entry = m_catalog.find(clipName);
    if (entry == nullptr) {
        std::cerr << "[Audio] Sound event not found in catalog: " << clipName << std::endl;
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
        // 2D 音效：禁用衰减
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
    const size_t loadedBefore = m_clips.size();
    for (const std::string& soundName : m_catalog.soundIds()) {
        const audio::SoundEntry* entry = m_catalog.find(soundName);
        if (entry == nullptr || !entry->preload) {
            continue;
        }

        for (size_t i = 0; i < entry->variants.size(); ++i) {
            loadClipVariant(soundName, i);
        }
    }

#ifdef MECRAFT_DEBUG
    const size_t loadedCount = m_clips.size() - loadedBefore;
    std::cout << "[Audio] Preloaded " << loadedCount << " audio clip variant(s)" << std::endl;
#endif
}

AudioClip* AudioEngine::loadClipVariant(const std::string& soundName, const size_t variantIndex) {
    const audio::SoundEntry* entry = m_catalog.find(soundName);
    if (entry == nullptr) {
        std::cerr << "[Audio] Sound event not found in catalog: " << soundName << std::endl;
        return nullptr;
    }
    if (variantIndex >= entry->variants.size()) {
        std::cerr << "[Audio] Sound event variant out of range: " << soundName << std::endl;
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

// BGM 调度已抽离到独立系统。

bool AudioEngine::initDeviceSwitchExtension() {
    if (!alcIsExtensionPresent(_device, "ALC_SOFT_system_events") ||
        !alcIsExtensionPresent(_device, "ALC_SOFT_reopen_device")) {
#ifdef MECRAFT_DEBUG
        std::cout << u8"[Audio] 设备自动切换扩展不可用" << std::endl;
#endif
        return false;
    }

    alcEventCallbackSOFT = (LPALCEVENTCALLBACKSOFT)alcGetProcAddress(_device, "alcEventCallbackSOFT");
    alcEventControlSOFT = (LPALCEVENTCONTROLSOFT)alcGetProcAddress(_device, "alcEventControlSOFT");
    alcReopenDeviceSOFT = (LPALCREOPENDEVICESOFT)alcGetProcAddress(_device, "alcReopenDeviceSOFT");

    if (!alcEventCallbackSOFT || !alcEventControlSOFT || !alcReopenDeviceSOFT) {
        std::cerr << "[Audio] 获取扩展函数指针失败" << std::endl;
        return false;
    }

    // 注册回调
    alcEventCallbackSOFT(OnDeviceEvent, nullptr);

    // 启用默认设备变更事件监听
    ALCenum eventToListen = ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT;
    alcEventControlSOFT(1, &eventToListen, ALC_TRUE);

    m_deviceSwitchSupported = true;
#ifdef MECRAFT_DEBUG
    std::cout << "[Audio] 设备自动切换已启用" << std::endl;
#endif
    return true;
}

void AudioEngine::checkDeviceSwitch() {
    if (!m_deviceSwitchSupported) return;

    if (s_needDeviceReopen.exchange(false)) {
#ifdef MECRAFT_DEBUG
        std::cout << "[Audio] 正在迁移音频上下文到新设备..." << std::endl;
#endif

        // alcReopenDeviceSOFT 会保留所有 AL 对象（Buffer, Source, State）
        if (!alcReopenDeviceSOFT(_device, nullptr, nullptr)) {
            std::cerr << "[Audio] 设备迁移失败！" << std::endl;
        } else {
#ifdef MECRAFT_DEBUG
            std::cout << "[Audio] 设备迁移成功，音频已无缝切换" << std::endl;
#endif
        }
    }
}
