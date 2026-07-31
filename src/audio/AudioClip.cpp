//
// Created by Caiwe on 2026/3/29.
//

#include "AudioClip.h"

#include "../Diagnostics.h"
#include "AudioDecoders.h"

#include <cstring>
#include <iostream>

namespace {

ALenum openAlFormatFor(const int channels, const int bitsPerSample) {
    if (channels == 1) {
        return bitsPerSample == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
    }
    if (channels == 2) {
        return bitsPerSample == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
    }
    return 0;
}

} // namespace

AudioClip::AudioClip(const std::string& filepath) : m_filepath(filepath) {
    const size_t lastSep = filepath.find_last_of("/\\");
    const size_t lastDot = filepath.find_last_of('.');
    if (lastSep != std::string::npos && lastDot != std::string::npos && lastDot > lastSep) {
        m_name = filepath.substr(lastSep + 1, lastDot - lastSep - 1);
    } else {
        m_name = filepath;
    }

    m_valid = load(filepath);
#ifdef MECRAFT_ENABLE_CONSOLE_OUTPUT
    if (m_valid) {
        MECRAFT_LOG_STREAM(std::cout << "[Audio] Loaded: " << m_name << " (" << m_duration << "s)" << std::endl);
    }
#endif
}

AudioClip::~AudioClip() {
    if (m_buffer != 0) {
        alDeleteBuffers(1, &m_buffer);
        m_buffer = 0;
    }
}

bool AudioClip::load(const std::string& filepath) {
    audio::DecodedAudio decoded;
    std::string error;
    if (!audio::AudioDecoderRegistry::instance().decodeFile(filepath, decoded, error)) {
        MECRAFT_LOG_STREAM(std::cerr << "[Audio] Failed to decode " << filepath << ": " << error << std::endl);
        return false;
    }

    const ALenum format = openAlFormatFor(decoded.channels, decoded.bitsPerSample);
    if (format == 0 || decoded.pcm.empty() || decoded.sampleRate <= 0) {
        MECRAFT_LOG_STREAM(std::cerr << "[Audio] Unsupported decoded audio format: " << filepath << std::endl);
        return false;
    }

    alGenBuffers(1, &m_buffer);
    alBufferData(m_buffer, format, decoded.pcm.data(), static_cast<ALsizei>(decoded.pcm.size()), decoded.sampleRate);

    const int bytesPerSample = decoded.bitsPerSample / 8;
    const int byteRate = decoded.sampleRate * decoded.channels * bytesPerSample;
    m_duration = byteRate > 0 ? static_cast<float>(decoded.pcm.size()) / static_cast<float>(byteRate) : 0.0f;

    const ALenum alError = alGetError();
    if (alError != AL_NO_ERROR) {
        MECRAFT_LOG_STREAM(std::cerr << "[Audio] OpenAL error: " << alError << std::endl);
        if (m_buffer != 0) {
            alDeleteBuffers(1, &m_buffer);
            m_buffer = 0;
        }
        return false;
    }

    return true;
}
