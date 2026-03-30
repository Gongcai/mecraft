//
// Created by Caiwe on 2026/3/29.
//

#ifndef MECRAFT_AUDIOCLIP_H
#define MECRAFT_AUDIOCLIP_H

#include <string>
#include <AL/al.h>

class AudioClip {
public:
    explicit AudioClip(const std::string& filepath);
    ~AudioClip();

    // 禁止拷贝
    AudioClip(const AudioClip&) = delete;
    AudioClip& operator=(const AudioClip&) = delete;

    [[nodiscard]] ALuint getBufferID() const { return m_buffer; }
    [[nodiscard]] const std::string& getName() const { return m_name; }
    [[nodiscard]] float getDuration() const { return m_duration; }
    [[nodiscard]] bool isValid() const { return m_valid; }

private:
    bool loadWAV(const std::string& filepath);

    ALuint m_buffer = 0;
    std::string m_name;
    std::string m_filepath;
    float m_duration = 0.0f;
    bool m_valid = false;
};


#endif //MECRAFT_AUDIOCLIP_H