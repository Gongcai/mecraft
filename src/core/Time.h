//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_TIME_H
#define MECRAFT_TIME_H
#include <iostream>
#include <ostream>
#include <GLFW/glfw3.h>


class Time {
public:
    static double currentGameTime;
    static double lastRawTime;
    static double deltaTime;
    static void init() {
        currentGameTime = 0.0;
        lastRawTime = getRawTime();
        deltaTime = 0.0;
        timeSpeed = 1.0;
    }
    static void setTimeSpeed(double speed) {
        timeSpeed = speed;
    }
    static double getRawTime() {
        return glfwGetTime();
    }
    static void update() {
        const double now = getRawTime();
        double rawDelta = now - lastRawTime;
        lastRawTime = now;

        if (rawDelta <= 0) rawDelta = 0.0;

        deltaTime = rawDelta * timeSpeed;
        currentGameTime += deltaTime;


    }
private:
    static double timeSpeed;
};


#endif //MECRAFT_TIME_H