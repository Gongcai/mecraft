#include "WeatherSystem.h"

WeatherSystem::WeatherSystem() = default;

void WeatherSystem::update(float /*dt*/) {
    // Placeholder: no simulation yet.
    // Future: interpolate weather state, handle biome precipitation,
    // drive cloud coverage, sync with DayNightSystem time-of-day.
}

void WeatherSystem::setDebugWeatherPreset(WeatherType type) {
    m_state.type = type;
    switch (type) {
        case WeatherType::Clear:
            m_state.mist = 0.0f;
            m_state.wetness = 0.0f;
            m_state.storm = 0.0f;
            m_state.aerialReduction = 0.55f;
            break;
        case WeatherType::Mist:
            m_state.mist = 0.45f;
            m_state.wetness = 0.15f;
            m_state.storm = 0.0f;
            m_state.aerialReduction = 0.40f;
            break;
        case WeatherType::Rain:
            m_state.mist = 0.65f;
            m_state.wetness = 0.75f;
            m_state.storm = 0.0f;
            m_state.aerialReduction = 0.25f;
            break;
        case WeatherType::Storm:
            m_state.mist = 0.85f;
            m_state.wetness = 0.95f;
            m_state.storm = 0.80f;
            m_state.aerialReduction = 0.15f;
            break;
    }
}
