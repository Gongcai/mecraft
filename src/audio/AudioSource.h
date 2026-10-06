//
// Created by Caiwe on 2026/3/29.
//

#ifndef MECRAFT_AUDIOSOURCE_H
#define MECRAFT_AUDIOSOURCE_H

#include <AL/al.h>
#include <glm/vec3.hpp>

class AudioClip;

class AudioSource {
public:
    AudioSource();
    ~AudioSource();

    // Audio sources own OpenAL handles and must not be copied.
    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;

    void setClip(const AudioClip* clip);
    void play();
    void pause();
    void stop();

    // Spatial playback properties.
    void setPosition(const glm::vec3& pos);
    void setVolume(float vol);
    /// Set the gameplay pitch multiplier applied to the selected sound event pitch.
    /// @param pitch Positive playback-rate multiplier controlled by gameplay.
    void setPitch(float pitch);
    /// Set the base pitch supplied by the selected sound event variant.
    /// @param pitch Positive pitch value loaded from the sound catalog.
    void setBasePitch(float pitch);
    void setLooping(bool loop);
    void setRolloffFactor(float rolloff);
    void setReferenceDistance(float dist);

    // Playback state queries.
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool isStopped() const;
    [[nodiscard]] bool isValid() const { return m_source != 0; }
    [[nodiscard]] ALuint getSourceID() const { return m_source; }

private:
    ALuint m_source = 0;
    const AudioClip* m_clip = nullptr;
    float m_basePitch = 1.0f;
    float m_pitchMultiplier = 1.0f;
};

#endif //MECRAFT_AUDIOSOURCE_H
