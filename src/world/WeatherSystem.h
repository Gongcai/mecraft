#pragma once

// Weather state management for the world.
// Currently a debug/placeholder shell — provides the architectural entry point
// for weather state (mist, wetness, storm) that flows from World -> Renderer -> shader.
// Real weather simulation (rain particles, cloud coverage, biome precipitation)
// will extend this system later.

enum class WeatherType {
    Clear,
    Mist,
    Rain,
    Storm
};

struct WeatherState {
    WeatherType type = WeatherType::Clear;
    float mist = 0.0f;           // [0, 1] fog/mist density
    float wetness = 0.0f;        // [0, 1] surface wetness (affects specular, albedo)
    float storm = 0.0f;          // [0, 1] storm intensity (lightning, wind)
    float aerialReduction = 0.55f; // [0, 1] reduces aerial perspective in rain
};

class WeatherSystem {
public:
    WeatherSystem();

    void update(float dt);

    // Current render state — read by Renderer to feed shader uniforms.
    [[nodiscard]] const WeatherState& getRenderState() const { return m_state; }

    // Debug preset: set weather type directly (for Dashboard UI).
    void setDebugWeatherPreset(WeatherType type);

    // Debug: set individual parameters.
    void setDebugMist(float v) { m_state.mist = v; }
    void setDebugWetness(float v) { m_state.wetness = v; }
    void setDebugStorm(float v) { m_state.storm = v; }

private:
    WeatherState m_state;
};
