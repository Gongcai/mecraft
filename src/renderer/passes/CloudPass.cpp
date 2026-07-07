#include "CloudPass.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../../resource/ResourceMgr.h"

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

void CloudPass::init(ResourceMgr& resourceMgr) {
    m_cloudShader = resourceMgr.getShader("cloud_target");
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void CloudPass::shutdown() {
    m_cloudShader = nullptr;
    m_noiseTexture = {};
    m_hasRenderedClouds = false;
}

void CloudPass::invalidateHistory() {
    m_hasRenderedClouds = false;
}

bool CloudPass::shouldRenderClouds(const FrameContext& ctx, const RenderSettings& settings) {
    const int updateInterval = std::clamp(settings.cloud.updateInterval, 1, 8);
    if (!m_hasRenderedClouds || !ctx.hasPreviousFrame || updateInterval <= 1) {
        return true;
    }

    const float weatherSignal = ctx.weather.wetness + ctx.weather.storm + ctx.weather.lightningFlash * 4.0f;
    const bool weatherChanged = std::abs(weatherSignal - m_lastWeatherSignal) > 0.025f;
    const glm::vec3 cameraDelta = ctx.camera.position - m_lastCameraPos;
    const bool movedFar = glm::dot(cameraDelta, cameraDelta) > 36.0f;
    if (weatherChanged || movedFar) {
        return true;
    }

    return (ctx.frameIndex % static_cast<uint64_t>(updateInterval)) == 0;
}

void CloudPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                         DeferredRenderTargets& targets) {
    if (m_cloudShader == nullptr) return;

    if (!shouldRenderClouds(ctx, settings)) {
        targets.copyHistoryCloudToCloud();
        return;
    }

    m_lastCameraPos = ctx.camera.position;
    m_lastWeatherSignal = ctx.weather.wetness + ctx.weather.storm + ctx.weather.lightningFlash * 4.0f;
    m_hasRenderedClouds = true;

    targets.bindCloud();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_cloudShader->use();
    m_cloudShader->setInt("uDepthTex", 0);
    m_cloudShader->setInt("uSkyCaptureTex", 1);
    m_cloudShader->setInt("uNoiseTex", 2);
    m_cloudShader->setInt("uAtmosphereLut", 3);
    // Clouds are world-space ray-marched — use non-jittered matrix to avoid TAA-induced jitter.
    m_cloudShader->setMat4("uInvViewProj", ctx.camera.invViewProj);

    // Sky lighting (inlined)
    m_cloudShader->setVec3("uCameraPos", ctx.camera.position);
    m_cloudShader->setVec3("uSunDirection", ctx.skyColors.sunDirection);
    m_cloudShader->setVec3("uMoonDirection", ctx.skyColors.moonDirection);
    m_cloudShader->setVec3("uSunLightColor", ctx.skyColors.sunLightColor);
    m_cloudShader->setVec3("uMoonLightColor", ctx.skyColors.moonLightColor);
    m_cloudShader->setVec3("uSkyAmbientColor", ctx.skyColors.skyAmbientColor);
    m_cloudShader->setVec3("uShadowTintColor", ctx.skyColors.shadowTintColor);
    m_cloudShader->setVec3("uHorizonScatterColor", ctx.skyColors.horizonScatterColor);
    m_cloudShader->setFloat("uSkyIntensity", ctx.skyIntensity);
    m_cloudShader->setFloat("uMoonVisibility", ctx.skyColors.moonVisibility);
    m_cloudShader->setVec3("uDirectIlluminance", ctx.skyIlluminance.directIlluminance);
    m_cloudShader->setVec3("uSkyIlluminance", ctx.skyIlluminance.skyIlluminance);
    m_cloudShader->setVec3("uSunIlluminance", ctx.skyIlluminance.sunIlluminance);
    m_cloudShader->setVec3("uMoonIlluminance", ctx.skyIlluminance.moonIlluminance);
    m_cloudShader->setVec3("uCloudDynamicWeather", ctx.skyIlluminance.cloudDynamicWeather);

    // Atmosphere (inlined)
    m_cloudShader->setFloat("uAerialStrength", ctx.atmosphere.aerialStrength);
    m_cloudShader->setFloat("uHorizonScatterStrength", ctx.atmosphere.horizonScatterStrength);
    m_cloudShader->setFloat("uSunWarmth", ctx.atmosphere.sunWarmth);
    m_cloudShader->setFloat("uSkyCoolness", ctx.atmosphere.skyCoolness);
    m_cloudShader->setFloat("uWeatherWetness", ctx.weather.wetness);
    m_cloudShader->setFloat("uWeatherStorm", ctx.weather.storm);
    m_cloudShader->setFloat("uAerialReduction", ctx.weather.aerialReduction);
    m_cloudShader->setFloat("uLightningFlash", ctx.weather.lightningFlash);
    m_cloudShader->setFloat("uSurfaceWetness", ctx.weather.surfaceWetness);
    m_cloudShader->setFloat("uSkyWetness", ctx.weather.skyWetness);
    m_cloudShader->setFloat("uFogWetness", ctx.weather.fogWetness);
    m_cloudShader->setFloat("uCloudWetness", ctx.weather.cloudWetness);
    m_cloudShader->setFloat("uPrecipitation", ctx.weather.precipitation);
    m_cloudShader->setFloat("uDirectWeatherOcclusion", ctx.atmosphere.directWeatherOcclusion);
    m_cloudShader->setInt("uDirectWeatherOcclusionOverride", ctx.atmosphere.directWeatherOcclusionOverride);

    // Cloud (inlined)
    m_cloudShader->setInt("uCloudShadowsEnabled", ctx.cloud.shadowsEnabled ? 1 : 0);
    m_cloudShader->setFloat("uCloudShadowStrength", ctx.cloud.shadowStrength);
    m_cloudShader->setFloat("uCloudShadowScale", ctx.cloud.shadowScale);
    m_cloudShader->setFloat("uCloudShadowSpeed", ctx.cloud.shadowSpeed);
    m_cloudShader->setFloat("uCloudTimeScale", ctx.cloud.timeScale);
    m_cloudShader->setFloat("uCloudCoverage", ctx.cloud.coverage);
    m_cloudShader->setFloat("uCloudDensity", ctx.cloud.density);
    m_cloudShader->setFloat("uCloudHeight", ctx.cloud.height);
    m_cloudShader->setFloat("uCloudThickness", ctx.cloud.thickness);
    m_cloudShader->setFloat("uPlanarCloudCoverage", ctx.cloud.planarCoverage);
    m_cloudShader->setFloat("uPlanarCloudDensity", ctx.cloud.planarDensity);
    m_cloudShader->setFloat("uPlanarCloudAltitude", ctx.cloud.planarAltitude);

    m_cloudShader->setFloat("uTime", ctx.shaderTime);
    m_cloudShader->setBool("uNoiseEnabled", m_noiseTexture.isValid());
    // Temporal cloud reprojection
    m_cloudShader->setInt("uHistoryCloudTex", 4);
    m_cloudShader->setMat4("uPreviousViewProj", ctx.previousViewProj);
    m_cloudShader->setInt("uFrameIndex", static_cast<int>(ctx.frameIndex & 0x7fffffffULL));

    // Texture bindings
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.skyCaptureTextureHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(m_noiseTexture));
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, renderer::rhi::gl::textureId(targets.atmosphereLutTextureHandle()));
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.historyCloudTexturePrevHandle()));
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_cloudShader);

    for (int i = 4; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
