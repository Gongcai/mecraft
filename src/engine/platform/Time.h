//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_TIME_H
#define MECRAFT_TIME_H
#include <GLFW/glfw3.h>

#include <cstdint>


class Time {
public:
    static double currentGameTime;
    static double lastRawTime;
    static double deltaTime;
    static double rawDeltaTime;
    static uint32_t frameIndex;
    static void init() {
        currentGameTime = 0.0;
        lastRawTime = getRawTime();
        deltaTime = 0.0;
        rawDeltaTime = 0.0;
        frameIndex = 0u;
        timeSpeed = 1.0;
    }
    static uint32_t beginFrame() {
        return ++frameIndex;
    }
    static uint32_t getFrameIndex() {
        return frameIndex;
    }
    static void setTimeSpeed(double speed) {
        timeSpeed = speed < 0.0 ? 0.0 : speed;
    }
    static double getTimeSpeed() {
        return timeSpeed;
    }
    static double getRawTime() {
        return glfwGetTime();
    }
    static double getGameTime() {
        return currentGameTime;
    }
    static double getRawDeltaTime() {
        return rawDeltaTime;
    }
    static void advanceGameTime(double dt) {
        if (dt <= 0.0) {
            deltaTime = 0.0;
            return;
        }
        deltaTime = dt;
        currentGameTime += deltaTime;
    }
    static void update() {
        const double now = getRawTime();
        double rawDelta = now - lastRawTime;
        lastRawTime = now;

        if (rawDelta <= 0) rawDelta = 0.0;
        rawDeltaTime = rawDelta;
        deltaTime = 0.0;
    }
private:
    static double timeSpeed;
};


#endif //MECRAFT_TIME_H
