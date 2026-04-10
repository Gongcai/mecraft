//
// Created by Caiwe on 2026/3/29.
//

#include "AudioEngine.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace {
std::string pathToUtf8(const fs::path& p) {
    // Avoid locale-dependent narrowing failures for Unicode file names on Windows.
    return p.u8string();
}
}

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
#ifndef NDEBUG
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

#ifndef NDEBUG
    std::cout << "[Audio] AudioEngine initialized" << std::endl;
#endif

    // 初始化设备切换扩展
    initDeviceSwitchExtension();

    // 加载所有音频文件
    getAllSounds();
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

#ifndef NDEBUG
    std::cout << "[Audio] AudioEngine shutdown" << std::endl;
#endif
}

AudioClip* AudioEngine::loadClip(const std::string& name) {
    // 检查是否已加载
    auto it = m_clips.find(name);
    if (it != m_clips.end()) {
        return it->second.get();
    }

    // 尝试加载 WAV 文件
    fs::path soundPath = fs::path("assets/sounds") / (name + ".wav");
    if (!fs::exists(soundPath)) {
        std::cerr << "[Audio] Sound file not found: " << soundPath << std::endl;
        return nullptr;
    }

    auto clip = std::make_unique<AudioClip>(pathToUtf8(soundPath));
    if (!clip->isValid()) {
        return nullptr;
    }

    AudioClip* ptr = clip.get();
    m_clips[name] = std::move(clip);
    return ptr;
}

AudioClip* AudioEngine::getClip(const std::string& name) {
    auto it = m_clips.find(name);
    if (it != m_clips.end()) {
        return it->second.get();
    }
    return nullptr;
}

AudioSource* AudioEngine::playClip(const std::string& clipName, glm::vec3 position, bool loop, float volume, bool spatial) {
    AudioClip* clip = getClip(clipName);
    if (!clip) {
        // 尝试加载
#ifndef NDEBUG
        std::cout << "[Audio] Sound file not found in Cache: " << clipName << std::endl;
#endif
        clip = loadClip(clipName);
        if (!clip) {
            return nullptr;
        }
    }

    AudioSource* source = acquireSource();
    if (!source) {
        return nullptr;
    }

    source->setClip(clip);
    source->setPosition(position);
    source->setVolume(volume * m_masterVolume);
    source->setLooping(loop);
    if (!spatial) {
        // 2D 音效：禁用衰减
        source->setRolloffFactor(0.0f);
    }
    source->play();

    return source;
}

bool AudioEngine::registerClipFromPath(const std::string& name, const std::string& filePath) {
    if (name.empty() || filePath.empty()) {
        return false;
    }

    if (m_clips.find(name) != m_clips.end()) {
        return true;
    }

    auto clip = std::make_unique<AudioClip>(filePath);
    if (!clip->isValid()) {
        return false;
    }

    m_clips[name] = std::move(clip);
    return true;
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

void AudioEngine::getAllSounds() {
    fs::path soundsDir = "../assets/sounds";

    if (!fs::exists(soundsDir)) {
        std::cerr << "[Audio] Sounds directory not found: " << soundsDir << std::endl;
        return;
    }

    int loadedCount = 0;

    // 遍历 sounds 目录下的所有 .wav 文件
    for (const auto& entry : fs::directory_iterator(soundsDir)) {
        if (entry.is_regular_file()) {
            const fs::path& path = entry.path();
            std::string ext = path.extension().string();

            // 转换为小写比较
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == ".wav") {
                // 提取文件名（不含扩展名）作为 clip 名称
                std::string clipName = pathToUtf8(path.stem());

                // 创建并加载 AudioClip
                auto clip = std::make_unique<AudioClip>(pathToUtf8(path));
                if (clip->isValid()) {
                    m_clips[clipName] = std::move(clip);
                    loadedCount++;
                }
            }
        }
    }

#ifndef NDEBUG
    std::cout << "[Audio] Loaded " << loadedCount << " sound(s) from " << soundsDir << std::endl;
#endif
}

// BGM 调度已抽离到独立系统。

bool AudioEngine::initDeviceSwitchExtension() {
    if (!alcIsExtensionPresent(_device, "ALC_SOFT_system_events") ||
        !alcIsExtensionPresent(_device, "ALC_SOFT_reopen_device")) {
#ifndef NDEBUG
        std::cout << u8"[Audio] 设备自动切换扩展不可用" << std::endl;
#endif
        return false;
    }

    alcEventCallbackSOFT = (LPALCEVENTCALLBACKSOFT)alcGetProcAddress(_device, "alcEventCallbackSOFT");
    alcEventControlSOFT = (LPALCEVENTCONTROLSOFT)alcGetProcAddress(_device, "alcEventControlSOFT");
    alcReopenDeviceSOFT = (LPALCREOPENDEVICESOFT)alcGetProcAddress(_device, "alcReopenDeviceSOFT");

    if (!alcEventCallbackSOFT || !alcEventControlSOFT || !alcReopenDeviceSOFT) {
        std::cerr << u8"[Audio] 获取扩展函数指针失败" << std::endl;
        return false;
    }

    // 注册回调
    alcEventCallbackSOFT(OnDeviceEvent, nullptr);

    // 启用默认设备变更事件监听
    ALCenum eventToListen = ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT;
    alcEventControlSOFT(1, &eventToListen, ALC_TRUE);

    m_deviceSwitchSupported = true;
#ifndef NDEBUG
    std::cout << "[Audio] 设备自动切换已启用" << std::endl;
#endif
    return true;
}

void AudioEngine::checkDeviceSwitch() {
    if (!m_deviceSwitchSupported) return;

    if (s_needDeviceReopen.exchange(false)) {
#ifndef NDEBUG
        std::cout << "[Audio] 正在迁移音频上下文到新设备..." << std::endl;
#endif

        // alcReopenDeviceSOFT 会保留所有 AL 对象（Buffer, Source, State）
        if (!alcReopenDeviceSOFT(_device, nullptr, nullptr)) {
            std::cerr << "[Audio] 设备迁移失败！" << std::endl;
        } else {
#ifndef NDEBUG
            std::cout << "[Audio] 设备迁移成功，音频已无缝切换" << std::endl;
#endif
        }
    }
}
