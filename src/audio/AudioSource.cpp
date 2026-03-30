//
// Created by Caiwe on 2026/3/29.
//

#include "AudioSource.h"
#include "AudioClip.h"

AudioSource::AudioSource() {
    alGenSources(1, &m_source);
}

AudioSource::~AudioSource() {
    if (m_source != 0) {
        alSourceStop(m_source);
        // 先解绑 buffer，避免 "Deleting in-use buffer" 警告
        alSourcei(m_source, AL_BUFFER, 0);
        alDeleteSources(1, &m_source);
        m_source = 0;
    }
}

void AudioSource::setClip(const AudioClip* clip) {
    m_clip = clip;
    if (m_source != 0) {
        if (clip != nullptr) {
            alSourcei(m_source, AL_BUFFER, clip->getBufferID());
        } else {
            // 解绑 buffer
            alSourcei(m_source, AL_BUFFER, 0);
        }
    }
}

void AudioSource::play() {
    if (m_source != 0) {
        alSourcePlay(m_source);
    }
}

void AudioSource::pause() {
    if (m_source != 0) {
        alSourcePause(m_source);
    }
}

void AudioSource::stop() {
    if (m_source != 0) {
        alSourceStop(m_source);
    }
}

void AudioSource::setPosition(const glm::vec3& pos) {
    if (m_source != 0) {
        alSource3f(m_source, AL_POSITION, pos.x, pos.y, pos.z);
    }
}

void AudioSource::setVolume(float vol) {
    if (m_source != 0) {
        alSourcef(m_source, AL_GAIN, vol);
    }
}

void AudioSource::setPitch(float pitch) {
    if (m_source != 0) {
        alSourcef(m_source, AL_PITCH, pitch);
    }
}

void AudioSource::setLooping(bool loop) {
    if (m_source != 0) {
        alSourcei(m_source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
    }
}

void AudioSource::setRolloffFactor(float rolloff) {
    if (m_source != 0) {
        alSourcef(m_source, AL_ROLLOFF_FACTOR, rolloff);
    }
}

void AudioSource::setReferenceDistance(float dist) {
    if (m_source != 0) {
        alSourcef(m_source, AL_REFERENCE_DISTANCE, dist);
    }
}

bool AudioSource::isPlaying() const {
    if (m_source == 0) return false;
    ALint state;
    alGetSourcei(m_source, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

bool AudioSource::isStopped() const {
    if (m_source == 0) return true;
    ALint state;
    alGetSourcei(m_source, AL_SOURCE_STATE, &state);
    return state == AL_STOPPED || state == AL_INITIAL;
}