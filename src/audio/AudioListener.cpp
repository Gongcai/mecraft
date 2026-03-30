//
// Created by Caiwe on 2026/3/29.
//

#include "AudioListener.h"

void AudioListener::setPosition(const glm::vec3& pos) {
    alListener3f(AL_POSITION, pos.x, pos.y, pos.z);
}

void AudioListener::setOrientation(const glm::vec3& front, const glm::vec3& up) {
    float orientation[6] = {
        front.x, front.y, front.z,
        up.x, up.y, up.z
    };
    alListenerfv(AL_ORIENTATION, orientation);
}

void AudioListener::setVelocity(const glm::vec3& vel) {
    alListener3f(AL_VELOCITY, vel.x, vel.y, vel.z);
}

void AudioListener::setGain(float gain) {
    alListenerf(AL_GAIN, gain);
}