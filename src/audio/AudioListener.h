//
// Created by Caiwe on 2026/3/29.
//

#ifndef MECRAFT_AUDIOLISTENER_H
#define MECRAFT_AUDIOLISTENER_H

#include <AL/al.h>
#include <glm/vec3.hpp>

class AudioListener {
public:
    static void setPosition(const glm::vec3& pos);
    static void setOrientation(const glm::vec3& front, const glm::vec3& up);
    static void setVelocity(const glm::vec3& vel);
    static void setGain(float gain);
};


#endif //MECRAFT_AUDIOLISTENER_H