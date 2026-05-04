#pragma once

#include <array>
#include <algorithm>

#include "Easing.h"

template<typename T>
class Tween {
public:
    Tween() = default;

    void start(T from, T to, float duration, EasingType easing = EasingType::Linear) {
        m_from = from;
        m_to = to;
        m_duration = duration;
        m_easing = easing;
        m_elapsed = 0.0f;
        m_running = true;
        m_forward = true;
    }

    void tick(float dt) {
        if (!m_running) return;
        if (m_forward) {
            m_elapsed += dt;
            if (m_elapsed >= m_duration) {
                if (m_pingPong) {
                    m_elapsed = m_duration;
                    m_forward = false;
                } else if (m_loop) {
                    m_elapsed -= m_duration;
                } else {
                    m_elapsed = m_duration;
                    m_running = false;
                }
            }
        } else {
            m_elapsed -= dt;
            if (m_elapsed <= 0.0f) {
                if (m_pingPong) {
                    m_elapsed = 0.0f;
                    m_forward = true;
                } else {
                    m_elapsed = 0.0f;
                    m_running = false;
                }
            }
        }
    }

    [[nodiscard]] T value() const;

    [[nodiscard]] float progress() const {
        if (m_duration <= 0.0f) return 1.0f;
        return std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
    }

    [[nodiscard]] bool isDone() const { return !m_running; }
    [[nodiscard]] bool isRunning() const { return m_running; }

    void reset() {
        m_elapsed = 0.0f;
        m_running = false;
        m_forward = true;
    }

    void setLoop(bool loop) { m_loop = loop; }
    void setPingPong(bool pingPong) { m_pingPong = pingPong; }
    void setEasing(EasingType easing) { m_easing = easing; }

    void setImmediate(T value) {
        m_from = value;
        m_to = value;
        m_elapsed = 0.0f;
        m_duration = 0.0f;
        m_running = false;
    }

private:
    T m_from{};
    T m_to{};
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;
    EasingType m_easing = EasingType::Linear;
    bool m_running = false;
    bool m_forward = true;
    bool m_loop = false;
    bool m_pingPong = false;
};

template<>
inline float Tween<float>::value() const {
    float t = applyEasing(progress(), m_easing);
    return m_from + (m_to - m_from) * t;
}

template<>
inline std::array<float, 4> Tween<std::array<float, 4>>::value() const {
    float t = applyEasing(progress(), m_easing);
    return {
        m_from[0] + (m_to[0] - m_from[0]) * t,
        m_from[1] + (m_to[1] - m_from[1]) * t,
        m_from[2] + (m_to[2] - m_from[2]) * t,
        m_from[3] + (m_to[3] - m_from[3]) * t
    };
}
