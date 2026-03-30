//
// Created by Caiwe on 2026/3/29.
//

#ifndef MECRAFT_AUDIOENGINE_H
#define MECRAFT_AUDIOENGINE_H
#include <memory>
#include <string>
#include <unordered_map>
#include <AL/al.h>
#include <AL/alc.h>
#include <glm/vec3.hpp>

#include "AudioClip.h"
#include "AudioSource.h"


class AudioEngine {
public:
    void init();
    void update();
    void shutdown();

    AudioClip* loadClip(const std::string& name);
    AudioClip* getClip(const std::string& name);

    AudioSource* playClip(const std::string& clipName,
                        glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f),
                        bool loop = false,
                        float volume = 1.0f,
                        bool spatial = true
                        );
    void playSound2D(const std::string& clipName,
                 float volume = 1.0f);

    void stopAll();
    void setMasterVolume(float volume);
private:
    ALCdevice* _device = nullptr;
    ALCcontext* m_context = nullptr;
    float m_masterVolume = 1.0f;
    float m_pitch = 1.0f;

    std::unordered_map<std::string, std::unique_ptr<AudioClip>> m_clips;
    std::vector<std::unique_ptr<AudioSource>> m_sources;

    AudioSource* acquireSource();
    void releaseSource(AudioSource* source);

    void getAllSounds();
};


#endif //MECRAFT_AUDIOENGINE_H