#include "WeatherSystem.h"
#include <cmath>
#include <algorithm>
#include <random>

WeatherSystem::WeatherSystem() = default;

// Exponential decay interpolation: moves current toward target.
// halflife is the time for half the remaining distance to be closed.
static float expDecay(float current, float target, float dt, float halflife) {
    if (halflife <= 0.0f) return target;
    float lambda = 0.69314718f / halflife; // ln(2) / halflife
    return target + (current - target) * std::exp(-lambda * dt);
}

void WeatherSystem::update(float dt) {
    // Select halflife based on whether we're ramping up or down.
    float wetnessHalflife = (m_targetWetness > m_state.wetness) ? kWetnessRiseHalflife : kWetnessFallHalflife;
    float stormHalflife = (m_targetStorm > m_state.storm) ? kStormRiseHalflife : kStormFallHalflife;

    m_state.wetness = expDecay(m_state.wetness, m_targetWetness, dt, wetnessHalflife);
    m_state.storm = expDecay(m_state.storm, m_targetStorm, dt, stormHalflife);
    m_state.aerialReduction = expDecay(m_state.aerialReduction, m_targetAerialReduction, dt, 30.0f);

    // Snap to target when very close to avoid perpetual drift.
    if (std::abs(m_state.wetness - m_targetWetness) < 0.001f) m_state.wetness = m_targetWetness;
    if (std::abs(m_state.storm - m_targetStorm) < 0.001f) m_state.storm = m_targetStorm;
    if (std::abs(m_state.aerialReduction - m_targetAerialReduction) < 0.001f) m_state.aerialReduction = m_targetAerialReduction;

    const bool reachedTarget =
        std::abs(m_state.wetness - m_targetWetness) < 0.001f &&
        std::abs(m_state.storm - m_targetStorm) < 0.001f &&
        std::abs(m_state.aerialReduction - m_targetAerialReduction) < 0.001f;
    if (reachedTarget) {
        m_state.type = m_targetType;
    }

    updateLightning(dt);
    computeDerived();
}

void WeatherSystem::computeDerived() {
    float w = m_state.wetness;
    float s = m_state.storm;
    bool isSnow = (m_state.type == WeatherType::Snow);

    // precipitation: combined rain+snow intensity (0..1)
    m_derived.precipitation = std::clamp(w + s, 0.0f, 1.0f);
    // rainStrength / snowStrength: mutually exclusive based on precipitation type
    if (isSnow) {
        m_derived.rainStrength = 0.0f;
        m_derived.snowStrength = std::clamp(w * 0.7f + s * 0.3f, 0.0f, 1.0f);
    } else {
        m_derived.rainStrength = std::clamp(w * 0.7f + s * 0.3f, 0.0f, 1.0f);
        m_derived.snowStrength = 0.0f;
    }
    // thunderStrength: only significant during storms (not snow)
    m_derived.thunderStrength = isSnow ? 0.0f : std::clamp(s * 1.2f - 0.15f, 0.0f, 1.0f);
    // surfaceWetness: drives albedo darkening, roughness reduction, specular boost
    // Snow still wets surfaces (melt), but less aggressively than rain.
    m_derived.surfaceWetness = isSnow
        ? std::clamp((w + s * 0.3f) * 0.5f, 0.0f, 1.0f)
        : std::clamp(w + s * 0.3f, 0.0f, 1.0f);
    // skyWetness: DerivativeMain rain/overcast occlusion for sky, post, and direct cloud shadow.
    m_derived.skyWetness = std::clamp(w + s, 0.0f, 1.0f);
    // fogWetness: historical Mecraft haze weighting used by aerial and volumetric fog.
    m_derived.fogWetness = std::clamp(w * 0.35f + s * 0.65f, 0.0f, 1.0f);
    // cloudWetness: cloud coverage should retain the old storm boost curve.
    m_derived.cloudWetness = std::clamp(w + s * (4.0f / 3.0f), 0.0f, 1.0f);
    // lightningFlash: no lightning during snow
    m_derived.lightningFlash = isSnow ? 0.0f : m_lightningFlash;
}

// [Phase 0] Lightning: random flash generator with simple decay.
// TODO Phase 1: route lightningColor through skylight SH, cloud shadow, volumetric fog.
void WeatherSystem::updateLightning(float dt) {
    // Decay existing flash quickly (attack is instant, decay is ~0.4s).
    if (m_lightningFlash > 0.0f) {
        m_lightningFlash = std::max(0.0f, m_lightningFlash - dt * 2.8f);
    }

    // No lightning outside storms.
    if (m_state.storm < 0.3f) {
        m_lightningCooldown = 0.0f;
        return;
    }

    m_lightningCooldown -= dt;
    if (m_lightningCooldown > 0.0f) return;

    // Random chance to trigger a flash, scaled by storm intensity.
    static thread_local std::mt19937 rng{42};
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float chance = dist(rng);
    // Higher storm = more frequent flashes (every 3-12s at full storm).
    float threshold = 1.0f - m_state.storm * 0.015f;
    if (chance > threshold) {
        m_lightningFlash = 0.6f + dist(rng) * 0.4f; // 0.6-1.0 brightness
        m_lightningCooldown = 3.0f + dist(rng) * 9.0f;
    } else {
        // Check again next frame.
        m_lightningCooldown = 0.1f;
    }
}

WeatherState WeatherSystem::stateForPreset(WeatherType type) {
    WeatherState state;
    state.type = type;
    switch (type) {
        case WeatherType::Clear:
            state.wetness = 0.0f;
            state.storm = 0.0f;
            state.aerialReduction = 0.55f;
            break;
        case WeatherType::Rain:
            state.wetness = 0.75f;
            state.storm = 0.0f;
            state.aerialReduction = 0.25f;
            break;
        case WeatherType::Storm:
            state.wetness = 0.95f;
            state.storm = 0.80f;
            state.aerialReduction = 0.15f;
            break;
        case WeatherType::Snow:
            state.wetness = 0.75f;
            state.storm = 0.0f;
            state.aerialReduction = 0.25f;
            break;
    }
    return state;
}

WeatherState WeatherSystem::getTargetState() const {
    return WeatherState{m_targetType, m_targetWetness, m_targetStorm, m_targetAerialReduction};
}

void WeatherSystem::setTargetState(const WeatherState& state) {
    m_targetType = state.type;
    m_targetWetness = std::clamp(state.wetness, 0.0f, 1.0f);
    m_targetStorm = std::clamp(state.storm, 0.0f, 1.0f);
    m_targetAerialReduction = std::clamp(state.aerialReduction, 0.0f, 1.0f);
}

void WeatherSystem::applyStateInstant(const WeatherState& state) {
    setTargetState(state);
    m_state = getTargetState();
    if (m_state.storm < 0.3f) {
        m_lightningFlash = 0.0f;
        m_lightningCooldown = 0.0f;
    }
    computeDerived();
}

void WeatherSystem::setDebugWeatherPreset(WeatherType type, bool instant) {
    const WeatherState state = stateForPreset(type);
    if (instant) {
        applyStateInstant(state);
    } else {
        setTargetState(state);
    }
}

void WeatherSystem::setDebugWetness(float v, bool instant) {
    WeatherState state = getTargetState();
    state.wetness = std::clamp(v, 0.0f, 1.0f);
    if (instant) {
        state.type = m_state.type;
        state.storm = m_state.storm;
        state.aerialReduction = m_state.aerialReduction;
        applyStateInstant(state);
    } else {
        setTargetState(state);
    }
}

void WeatherSystem::setDebugStorm(float v, bool instant) {
    WeatherState state = getTargetState();
    state.storm = std::clamp(v, 0.0f, 1.0f);
    if (instant) {
        state.type = m_state.type;
        state.wetness = m_state.wetness;
        state.aerialReduction = m_state.aerialReduction;
        applyStateInstant(state);
    } else {
        setTargetState(state);
    }
}
