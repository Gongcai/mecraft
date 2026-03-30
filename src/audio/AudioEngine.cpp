//
// Created by Caiwe on 2026/3/29.
//

#include "AudioEngine.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include "../core/Time.h"
namespace fs = std::filesystem;

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

    std::cout << "[Audio] AudioEngine initialized" << std::endl;

    // 加载所有音频文件
    getAllSounds();
}

void AudioEngine::update() {
    // 清理已停止的 source
    for (auto it = m_sources.begin(); it != m_sources.end();) {
        if ((*it)->isStopped()) {
            it = m_sources.erase(it);
        } else {
            ++it;
        }
    }
    m_pitch = Time::getTimeSpeed();
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

    std::cout << "[Audio] AudioEngine shutdown" << std::endl;
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

    auto clip = std::make_unique<AudioClip>(soundPath.string());
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
        std::cout<<"[Audio] Sound file not found in Cache: " << clipName << std::endl;
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
    source->setPitch(m_pitch);
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
            fs::path path = entry.path();
            std::string ext = path.extension().string();

            // 转换为小写比较
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == ".wav") {
                // 提取文件名（不含扩展名）作为 clip 名称
                std::string clipName = path.stem().string();

                // 创建并加载 AudioClip
                auto clip = std::make_unique<AudioClip>(path.string());
                if (clip->isValid()) {
                    m_clips[clipName] = std::move(clip);
                    loadedCount++;
                }
            }
        }
    }

    std::cout << "[Audio] Loaded " << loadedCount << " sound(s) from " << soundsDir << std::endl;
}
