#pragma once

// Weather state management with smooth interpolation.
// Clear / Rain / Storm states with target->actual smoothing via exponential decay.
// Derived values (precipitation, surfaceWetness, skyWetness, lightningFlash)
// are computed each frame and consumed by Renderer to feed shader uniforms.

enum class WeatherType { Clear, Rain, Storm, Snow };

struct WeatherState {
    WeatherType type = WeatherType::Clear;
    float wetness = 0.0f; // [0, 1] surface wetness (affects specular, albedo)
    float storm = 0.0f; // [0, 1] storm intensity (lightning, wind)
    float aerialReduction = 0.55f; // [0, 1] reduces aerial perspective in rain
};

// Derived weather values consumed by shaders and gameplay systems.
struct WeatherDerived {
    float precipitation = 0.0f; // [0, 1] combined rain+snow intensity
    float rainStrength = 0.0f; // [0, 1] rain-only intensity (no snow)
    float snowStrength = 0.0f; // [0, 1] snow-only intensity
    float thunderStrength = 0.0f; // [0, 1] thunder/lightning intensity
    float surfaceWetness = 0.0f; // [0, 1] wetness for terrain surface effects
    float skyWetness = 0.0f; // [0, 1] sky/post rain occlusion, equivalent to wetness+storm
    float fogWetness = 0.0f; // [0, 1] aerial/volumetric haze weighting
    float cloudWetness = 0.0f; // [0, 1] cloud coverage/weather shaping
    float lightningFlash = 0.0f; // [0, 1] lightning brightness (quick attack, slow decay)
};

class WeatherSystem {
public:
    WeatherSystem();

    void update(float dt);

    // Current render state — read by Renderer to feed shader uniforms.
    [[nodiscard]] const WeatherState& getRenderState() const { return m_state; }
    [[nodiscard]] WeatherState getTargetState() const;
    [[nodiscard]] const WeatherDerived& getDerived() const { return m_derived; }

    // Debug preset: instant writes current+target, smooth writes target only.
    void setDebugWeatherPreset(WeatherType type, bool instant = true);
    void setDebugWeatherPresetSmooth(WeatherType type) { setDebugWeatherPreset(type, false); }
    void setDebugWeatherPresetInstant(WeatherType type) { setDebugWeatherPreset(type, true); }

    // Debug: set individual target parameters.
    void setDebugWetness(float v, bool instant = true);
    void setDebugStorm(float v, bool instant = true);

private:
    void computeDerived();
    void updateLightning(float dt);
    static WeatherState stateForPreset(WeatherType type);
    void setTargetState(const WeatherState& state);
    void applyStateInstant(const WeatherState& state);

    WeatherState m_state;
    WeatherDerived m_derived;

    // Target values — debug presets write here, update() interpolates toward them.
    float m_targetWetness = 0.0f;
    float m_targetStorm = 0.0f;
    float m_targetAerialReduction = 0.55f;
    WeatherType m_targetType = WeatherType::Clear;

    // Lightning state.
    float m_lightningFlash = 0.0f; // current flash brightness [0,1]
    float m_lightningCooldown = 0.0f; // seconds until next possible flash

    // Interpolation halflife in seconds (DerivativeMain: wetnessHalflife=180, drynessHalflife=60).
    static constexpr float kWetnessRiseHalflife = 180.0f;
    static constexpr float kWetnessFallHalflife = 60.0f;
    static constexpr float kStormRiseHalflife = 120.0f;
    static constexpr float kStormFallHalflife = 45.0f;
};
