#pragma once

#include <cmath>

enum class EasingType {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    ElasticOut,
    BounceOut,
    BackOut,
};

namespace easing {

constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] inline float linear(float t) {
    return t;
}

[[nodiscard]] inline float easeIn(float t) {
    return t * t;
}

[[nodiscard]] inline float easeOut(float t) {
    return t * (2.0f - t);
}

[[nodiscard]] inline float easeInOut(float t) {
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
}

[[nodiscard]] inline float elasticOut(float t) {
    if (t == 0.0f || t == 1.0f)
        return t;
    return std::pow(2.0f, -10.0f * t) * std::sin((t - 0.075f) * (2.0f * kPi) / 0.3f) + 1.0f;
}

[[nodiscard]] inline float bounceOut(float t) {
    if (t < 1.0f / 2.75f) {
        return 7.5625f * t * t;
    }
    if (t < 2.0f / 2.75f) {
        t -= 1.5f / 2.75f;
        return 7.5625f * t * t + 0.75f;
    }
    if (t < 2.5f / 2.75f) {
        t -= 2.25f / 2.75f;
        return 7.5625f * t * t + 0.9375f;
    }
    t -= 2.625f / 2.75f;
    return 7.5625f * t * t + 0.984375f;
}

[[nodiscard]] inline float backOut(float t) {
    constexpr float s = 1.70158f;
    t -= 1.0f;
    return t * t * ((s + 1.0f) * t + s) + 1.0f;
}

} // namespace easing

[[nodiscard]] inline float applyEasing(float t, EasingType type) {
    switch (type) {
    case EasingType::Linear: return easing::linear(t);
    case EasingType::EaseIn: return easing::easeIn(t);
    case EasingType::EaseOut: return easing::easeOut(t);
    case EasingType::EaseInOut: return easing::easeInOut(t);
    case EasingType::ElasticOut: return easing::elasticOut(t);
    case EasingType::BounceOut: return easing::bounceOut(t);
    case EasingType::BackOut: return easing::backOut(t);
    }
    return t;
}
