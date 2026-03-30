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

    // 禁止拷贝
    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;

    void setClip(const AudioClip* clip);
    void play();
    void pause();
    void stop();

    // 3D 空间属性
    void setPosition(const glm::vec3& pos);
    void setVolume(float vol);
    void setPitch(float pitch);
    void setLooping(bool loop);
    void setRolloffFactor(float rolloff);
    void setReferenceDistance(float dist);

    // 状态查询
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool isStopped() const;
    [[nodiscard]] bool isValid() const { return m_source != 0; }
    [[nodiscard]] ALuint getSourceID() const { return m_source; }

private:
    ALuint m_source = 0;
    const AudioClip* m_clip = nullptr;
};


#endif //MECRAFT_AUDIOSOURCE_H