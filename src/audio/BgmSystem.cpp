#include "BgmSystem.h"

#include "AudioEngine.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace {
std::string pathToUtf8(const fs::path& p) {
    return p.u8string();
}
}

void BgmSystem::init(AudioEngine& audioEngine) {
    m_audioEngine = &audioEngine;
    m_activeSource = nullptr;
    m_hasStartedPlayback = false;
    m_cooldownRemaining = 0.0f;
    m_lastIndex = -1;

    buildPlaylist();
    tryStartPlayback();
}

void BgmSystem::update(const float deltaTime) {
    if (!m_audioEngine) {
        return;
    }

    if (m_activeSource && m_activeSource->isStopped()) {
        m_activeSource = nullptr;
        m_cooldownRemaining = getRandomDelaySeconds();
    }

    if (!m_activeSource && m_hasStartedPlayback) {
        m_cooldownRemaining -= std::max(deltaTime, 0.0f);
        if (m_cooldownRemaining <= 0.0f) {
            tryStartPlayback();
        }
    }
}

void BgmSystem::shutdown() {
    m_activeSource = nullptr;
    m_playlist.clear();
    m_hasStartedPlayback = false;
    m_cooldownRemaining = 0.0f;
    m_lastIndex = -1;
    m_audioEngine = nullptr;
}

void BgmSystem::setVolume(const float volume) {
    m_volume = std::clamp(volume, 0.0f, 1.0f);
}

void BgmSystem::buildPlaylist() {
    m_playlist.clear();

    const fs::path preferredDir = "assets/bgm";
    const fs::path fallbackDir = "../assets/bgm";
    const fs::path bgmDir = fs::exists(preferredDir) ? preferredDir : fallbackDir;
    if (!fs::exists(bgmDir)) {
        std::cerr << "[Audio] BGM directory not found: " << preferredDir
                  << " or " << fallbackDir << std::endl;
        return;
    }

    for (const auto& entry : fs::directory_iterator(bgmDir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const fs::path path = entry.path();
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".wav") {
            continue;
        }

        const std::string clipName = "bgm/" + pathToUtf8(path.stem());
        if (m_audioEngine->registerClipFromPath(clipName, pathToUtf8(path))) {
            m_playlist.push_back(clipName);
        }
    }

    std::sort(m_playlist.begin(), m_playlist.end());

#ifndef NDEBUG
    std::cout << "[Audio] Loaded " << m_playlist.size() << " BGM track(s) from " << bgmDir << std::endl;
#endif
}

void BgmSystem::tryStartPlayback() {
    if (!m_audioEngine || m_playlist.empty()) {
        return;
    }

    std::uniform_int_distribution<int> dist(0, static_cast<int>(m_playlist.size()) - 1);
    int index = dist(m_rng);
    if (m_playlist.size() > 1 && index == m_lastIndex) {
        index = (index + 1) % static_cast<int>(m_playlist.size());
    }

    AudioSource* source = m_audioEngine->playClip(m_playlist[index], glm::vec3(0.0f), false, m_volume, false);
    if (!source) {
        return;
    }

    m_activeSource = source;
    m_hasStartedPlayback = true;
    m_lastIndex = index;
    m_cooldownRemaining = 0.0f;
}

float BgmSystem::getRandomDelaySeconds() {
    std::uniform_real_distribution<float> dist(600.0f, 1200.0f);
    return dist(m_rng);
}

