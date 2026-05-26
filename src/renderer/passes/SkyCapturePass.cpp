#include "SkyCapturePass.h"
#include "../targets/DeferredRenderTargets.h"
#include "../renderers/GameplaySkyRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/World.h"
#include "../../world/DayNightSystem.h"
#include "../../world/WeatherSystem.h"

#include <algorithm>

void SkyCapturePass::init(ResourceMgr& /*resourceMgr*/) {
    // No shaders to load — GameplaySkyRenderer manages its own.
}

void SkyCapturePass::shutdown() {
    // Nothing to release.
}

void SkyCapturePass::execute(const World& world, DeferredRenderTargets& targets,
                              GameplaySkyRenderer& skyRenderer, ResourceMgr* resourceMgr,
                              float cameraY, float shaderTime, const glm::vec3& cameraPos,
                              float cloudTimeScale) {
    const float cameraAltitude = cameraY;
    const GLuint atmosphereLut = targets.atmosphereLutTexture();
    const int moonPhase = world.getDayNightSystem().getMoonPhaseIndex();

    // DerivativeMain MoonFlux: phase factor ranges 0.2 (full moon) to 1.2 (new moon).
    constexpr float kNightBrightness = 0.0005f;
    const float moonPhaseFlux = (static_cast<float>(std::abs(moonPhase - 4)) * 0.25f + 0.2f) * kNightBrightness;

    // Weather state for SkyCapture modulation
    const WeatherState& weather = world.getWeatherSystem().getRenderState();
    const float weatherWetness = weather.wetness;
    const float weatherStorm = weather.storm;

    auto illum = skyRenderer.computeSkyIlluminance(
        skyRenderer.computeSkyColors(world.getDayNightSystem()),
        weatherWetness, weatherStorm);

    // DerivativeMain worldTime: 24000 ticks/day, our timeOfDay is in seconds with 1200s/day.
    const int worldDay = world.getDayNightSystem().getElapsedDays();
    const int worldTime = static_cast<int>(world.getDayNightSystem().getTimeOfDay() * 20.0f);
    illum.cloudDynamicWeather = GameplaySkyRenderer::computeCloudDynamicWeather(worldDay, worldTime);

    const float cloudWetness = std::clamp(weatherWetness + weatherStorm * (4.0f / 3.0f), 0.0f, 1.0f);
    float cloudHeight = 1000.0f + cloudWetness * (800.0f - 1000.0f);
    const float cloudThickness = 1400.0f + cloudWetness * (3000.0f - 1400.0f);
    const float cloudCoverage = std::clamp(1.0f + cloudWetness * 0.2f, 0.0f, 1.5f);
    const float cloudDensity = 0.85f + weatherWetness * 0.35f + weatherStorm * 0.55f;
    const GLuint noiseTexture = resourceMgr != nullptr ? resourceMgr->getTexture2D("shader_noise2d") : 0;

    // Raw sky radiance (rows 0..257)
    skyRenderer.renderSkyCapture(world.getDayNightSystem(),
                                  targets.skyCaptureFramebuffer(),
                                  targets.skyCaptureWidth(),
                                  targets.skyCaptureHeight(),
                                  cameraAltitude, atmosphereLut, moonPhaseFlux,
                                  weatherWetness, weatherStorm);

    // Cloudy sky radiance (rows 258..513)
    skyRenderer.renderCloudySkyCapture(world.getDayNightSystem(),
                                        targets.skyCaptureFramebuffer(),
                                        targets.skyCaptureWidth(),
                                        targets.skyCaptureHeight(),
                                        cameraAltitude, atmosphereLut, moonPhaseFlux,
                                        noiseTexture, shaderTime,
                                        illum,
                                        cloudCoverage, cloudDensity,
                                        cloudHeight, cloudThickness,
                                        0.5f,
                                        1.0f,
                                        7000.0f,
                                        cloudTimeScale,
                                        cameraPos,
                                        weatherWetness, weatherStorm);

    skyRenderer.writeSkyCacheMetadata(illum,
                                       targets.skyCaptureFramebuffer(),
                                       targets.skyCaptureWidth(),
                                       cameraAltitude, atmosphereLut, moonPhaseFlux,
                                       weatherWetness, weatherStorm);
}
