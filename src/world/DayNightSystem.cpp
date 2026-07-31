#include "DayNightSystem.h"
#include <algorithm>
#include <cmath>

DayNightSystem::DayNightSystem() : m_totalGameTime(0.0), m_timeOfDay(0.0f), m_elapsedDays(0), m_skyIntensity(1.0f) {}

void DayNightSystem::update(float dt) {
    m_totalGameTime += dt;
    m_timeOfDay += dt;

    if (m_timeOfDay >= SECONDS_PER_DAY) {
        m_timeOfDay -= SECONDS_PER_DAY;
        m_elapsedDays++;
    }

    // Calculate sky intensity
    // 0 to 600 is "Day time" functionally, 600 to 1200 is "Night time"
    // Let's create a smooth transition:
    // Sunrise: 1140s to 60s
    // Full Day: 60s to 540s
    // Sunset: 540s to 660s
    // Full Night: 660s to 1140s

    if (m_timeOfDay >= 60.0f && m_timeOfDay <= 540.0f) {
        m_skyIntensity = 1.0f; // Full day
    } else if (m_timeOfDay >= 660.0f && m_timeOfDay <= 1140.0f) {
        m_skyIntensity = 0.0f; // Full night (use night lightmap entirely)
    } else if (m_timeOfDay > 540.0f && m_timeOfDay < 660.0f) {
        // Sunset transition (540 to 660 -> duration 120s)
        float t = (m_timeOfDay - 540.0f) / 120.0f;
        // Smoothstep transition 1.0 -> 0.0
        float factor = t * t * (3.0f - 2.0f * t);
        m_skyIntensity = 1.0f - factor;
    } else {
        // Sunrise transition (1140 to 1200, and 0 to 60 -> duration 120s)
        float t;
        if (m_timeOfDay >= 1140.0f) {
            t = (m_timeOfDay - 1140.0f) / 120.0f; // 0.0 to 0.5
        } else {
            t = (m_timeOfDay + 60.0f) / 120.0f; // 0.5 to 1.0
        }
        // Smoothstep transition 0.0 -> 1.0
        float factor = t * t * (3.0f - 2.0f * t);
        m_skyIntensity = factor;
    }
}

void DayNightSystem::setTimeOfDay(float time) {
    m_timeOfDay = std::fmod(std::max(0.0f, time), SECONDS_PER_DAY);
    update(0.0f); // Recalculate intensity
}

float DayNightSystem::getTimeOfDay() const {
    return m_timeOfDay;
}

double DayNightSystem::getTotalGameTime() const {
    return m_totalGameTime;
}

int DayNightSystem::getElapsedDays() const {
    return m_elapsedDays;
}

float DayNightSystem::getSkyIntensity() const {
    return m_skyIntensity;
}

bool DayNightSystem::isNightTime() const {
    return m_skyIntensity < 0.5f;
}

bool DayNightSystem::isFullDaytime() const {
    return m_skyIntensity >= 1.0f;
}

float DayNightSystem::getDayProgress01() const {
    return std::clamp(m_timeOfDay / SECONDS_PER_DAY, 0.0f, 0.999999f);
}

float DayNightSystem::getCelestialAngleRadians() const {
    constexpr float kTwoPi = 6.28318530717958647692f;
    return getDayProgress01() * kTwoPi;
}

int DayNightSystem::getMoonPhaseIndex() const {
    return m_elapsedDays % 8;
}
