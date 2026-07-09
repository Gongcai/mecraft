#include "VolumetricPass.h"
#include "../core/RenderScene.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../../resource/ResourceMgr.h"
#include "../shadow/ShadowRenderer.h"

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

void VolumetricPass::init(ResourceMgr& resourceMgr) {
    m_volumetricFogShader = resourceMgr.getShader("volumetric_fog");
    m_volumetricTemporalShader = resourceMgr.getShader("volumetric_temporal");
    m_volumetricCompositeShader = resourceMgr.getShader("volumetric_composite");
    m_resourceMgr = &resourceMgr;
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void VolumetricPass::shutdown() {
    m_volumetricFogShader = nullptr;
    m_volumetricTemporalShader = nullptr;
    m_volumetricCompositeShader = nullptr;
    m_shadowRenderer = nullptr;
    m_resourceMgr = nullptr;
    m_noiseTexture = {};
    m_hasRenderedFog = false;
}

void VolumetricPass::invalidateHistory() {
    m_hasRenderedFog = false;
}

bool VolumetricPass::shouldRenderFog(const FrameContext& ctx, const RenderSettings& settings,
                                      const bool hasPreviousFrame) const {
    const bool underwaterVolumetricActive = ctx.eyeInWater && settings.volumetric.uwLightEnabled;
    if (underwaterVolumetricActive || !settings.volumetric.temporalEnabled || !hasPreviousFrame ||
        !m_hasRenderedFog) {
        return true;
    }

    const int updateInterval = std::clamp(settings.volumetric.updateInterval, 1, 8);
    if (updateInterval <= 1) {
        return true;
    }

    const glm::vec3 cameraDelta = ctx.camera.position - m_lastCameraPos;
    const bool movedFar = glm::dot(cameraDelta, cameraDelta) > 4.0f;
    const float weatherSignal = ctx.weather.wetness + ctx.weather.storm + ctx.weather.fogWetness +
                                ctx.weather.lightningFlash * 4.0f;
    const bool weatherChanged = std::abs(weatherSignal - m_lastWeatherSignal) > 0.02f;
    if (movedFar || weatherChanged) {
        return true;
    }

    return (ctx.frameIndex % static_cast<uint64_t>(updateInterval)) == 0;
}

void VolumetricPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                              DeferredRenderTargets& targets, bool hasPreviousFrame) {
    const bool renderCurrentFog = shouldRenderFog(ctx, settings, hasPreviousFrame);
    if (renderCurrentFog) {
        renderFog(ctx, settings, targets);
        m_lastCameraPos = ctx.camera.position;
        m_lastWeatherSignal = ctx.weather.wetness + ctx.weather.storm + ctx.weather.fogWetness +
                              ctx.weather.lightningFlash * 4.0f;
        m_hasRenderedFog = true;
    } else {
        targets.copyHistoryVolumetricToHalfRes();
    }

    // Temporal resolve (optional)
    if (renderCurrentFog && settings.volumetric.temporalEnabled && hasPreviousFrame &&
        m_volumetricTemporalShader != nullptr) {
        renderTemporal(ctx, settings, targets);
    }

    // Composite (always runs to ensure correct transmittance)
    if (m_volumetricCompositeShader != nullptr) {
        composite(ctx, settings, targets, hasPreviousFrame);
    }
}

void VolumetricPass::renderFog(const FrameContext& ctx, const RenderSettings& settings,
                                DeferredRenderTargets& targets) {
    if (m_volumetricFogShader == nullptr) return;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureHalfResTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.halfResTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "VolumetricFog";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.halfWidth())),
        static_cast<uint32_t>(std::max(1, targets.halfHeight()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    // Pre-bind CSM shadow array on unit 6
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY,
                  renderer::rhi::gl::textureId(targets.csmShadowDepthComparisonTextureHandle()));

    m_volumetricFogShader->use();
    m_volumetricFogShader->setInt("uDepthTex", 0);
    m_volumetricFogShader->setInt("uSkyCaptureTex", 1);
    m_volumetricFogShader->setInt("uNoiseTex", 2);
    m_volumetricFogShader->setInt("uShadowMapRaw", 3);
    m_volumetricFogShader->setInt("uShadowColorTex", 4);
    m_volumetricFogShader->setInt("uAtmosphereLut", 5);
    m_volumetricFogShader->setInt("uCsmShadowMap", 6);
    m_volumetricFogShader->setInt("uCsmShadowDepthRaw", 7);
    m_volumetricFogShader->setInt("uCsmShadowDepthAll", 8);
    m_volumetricFogShader->setInt("uCsmShadowDepthAllRaw", 9);
    m_volumetricFogShader->setInt("uCsmShadowColor0", 10);
    m_volumetricFogShader->setInt("uCsmShadowColor1", 11);
    m_volumetricFogShader->setMat4("uInvViewProj", ctx.camera.invViewProj);
    m_volumetricFogShader->setVec2("uJitter", ctx.jitter);

    // Shadow uniforms
    if (m_shadowRenderer) {
        const shadow::ShadowRenderer::BiasSettings bias{
            settings.shadow.constantBias,
            settings.shadow.slopeBias,
            settings.shadow.normalOffset
        };
        m_shadowRenderer->bindShadowUniforms(*m_volumetricFogShader, ctx.moonShadowActive, bias);
    }

    // Sky lighting (inlined)
    m_volumetricFogShader->setVec3("uCameraPos", ctx.camera.position);
    m_volumetricFogShader->setVec3("uSunDirection", ctx.skyColors.sunDirection);
    m_volumetricFogShader->setVec3("uMoonDirection", ctx.skyColors.moonDirection);
    m_volumetricFogShader->setVec3("uSunLightColor", ctx.skyColors.sunLightColor);
    m_volumetricFogShader->setVec3("uMoonLightColor", ctx.skyColors.moonLightColor);
    m_volumetricFogShader->setVec3("uSkyAmbientColor", ctx.skyColors.skyAmbientColor);
    m_volumetricFogShader->setVec3("uShadowTintColor", ctx.skyColors.shadowTintColor);
    m_volumetricFogShader->setVec3("uHorizonScatterColor", ctx.skyColors.horizonScatterColor);
    m_volumetricFogShader->setFloat("uSkyIntensity", ctx.skyIntensity);
    m_volumetricFogShader->setFloat("uMoonVisibility", ctx.skyColors.moonVisibility);
    m_volumetricFogShader->setVec3("uDirectIlluminance", ctx.skyIlluminance.directIlluminance);
    m_volumetricFogShader->setVec3("uSkyIlluminance", ctx.skyIlluminance.skyIlluminance);
    m_volumetricFogShader->setVec3("uSunIlluminance", ctx.skyIlluminance.sunIlluminance);
    m_volumetricFogShader->setVec3("uMoonIlluminance", ctx.skyIlluminance.moonIlluminance);
    m_volumetricFogShader->setVec3("uCloudDynamicWeather", ctx.skyIlluminance.cloudDynamicWeather);

    // Atmosphere (inlined)
    m_volumetricFogShader->setFloat("uAerialStrength", ctx.atmosphere.aerialStrength);
    m_volumetricFogShader->setFloat("uHorizonScatterStrength", ctx.atmosphere.horizonScatterStrength);
    m_volumetricFogShader->setFloat("uSunWarmth", ctx.atmosphere.sunWarmth);
    m_volumetricFogShader->setFloat("uSkyCoolness", ctx.atmosphere.skyCoolness);
    m_volumetricFogShader->setFloat("uWeatherWetness", ctx.weather.wetness);
    m_volumetricFogShader->setFloat("uWeatherStorm", ctx.weather.storm);
    m_volumetricFogShader->setFloat("uAerialReduction", ctx.weather.aerialReduction);
    m_volumetricFogShader->setFloat("uLightningFlash", ctx.weather.lightningFlash);
    m_volumetricFogShader->setFloat("uSurfaceWetness", ctx.weather.surfaceWetness);
    m_volumetricFogShader->setFloat("uSkyWetness", ctx.weather.skyWetness);
    m_volumetricFogShader->setFloat("uFogWetness", ctx.weather.fogWetness);
    m_volumetricFogShader->setFloat("uCloudWetness", ctx.weather.cloudWetness);
    m_volumetricFogShader->setFloat("uPrecipitation", ctx.weather.precipitation);
    m_volumetricFogShader->setFloat("uDirectWeatherOcclusion", ctx.atmosphere.directWeatherOcclusion);
    m_volumetricFogShader->setInt("uDirectWeatherOcclusionOverride", ctx.atmosphere.directWeatherOcclusionOverride);

    // Volumetric (inlined from bindVolumetricUniforms)
    m_volumetricFogShader->setInt("uVolumetricLightEnabled", ctx.volumetric.lightEnabled ? 1 : 0);
    m_volumetricFogShader->setInt("uVolumetricFogEnabled", ctx.volumetric.fogEnabled ? 1 : 0);
    m_volumetricFogShader->setFloat("uVolumetricFogStrength", ctx.volumetric.fogStrength);
    m_volumetricFogShader->setFloat("uVolumetricBaseDensity", ctx.volumetric.baseDensity);
    m_volumetricFogShader->setFloat("uVolumetricMaxDistance", ctx.volumetric.maxDistance);
    m_volumetricFogShader->setFloat("uVFogCenterHeight", ctx.volumetric.fogCenterHeight);
    m_volumetricFogShader->setFloat("uVFogHeightSpread", ctx.volumetric.fogHeightSpread);
    m_volumetricFogShader->setFloat("uVFogNoiseScale", ctx.volumetric.fogNoiseScale);
    m_volumetricFogShader->setFloat("uVFogLightStrength", ctx.volumetric.fogLightStrength);
    m_volumetricFogShader->setFloat("uVFogDensityScale", ctx.volumetric.fogDensityScale);
    m_volumetricFogShader->setInt("uVolumetricFogSamples", ctx.volumetric.fogSamples);

    // Cloud (inlined)
    m_volumetricFogShader->setInt("uCloudShadowsEnabled", ctx.cloud.shadowsEnabled ? 1 : 0);
    m_volumetricFogShader->setFloat("uCloudShadowStrength", ctx.cloud.shadowStrength);
    m_volumetricFogShader->setFloat("uCloudShadowScale", ctx.cloud.shadowScale);
    m_volumetricFogShader->setFloat("uCloudShadowSpeed", ctx.cloud.shadowSpeed);
    m_volumetricFogShader->setFloat("uCloudTimeScale", ctx.cloud.timeScale);
    m_volumetricFogShader->setFloat("uCloudCoverage", ctx.cloud.coverage);
    m_volumetricFogShader->setFloat("uCloudDensity", ctx.cloud.density);
    m_volumetricFogShader->setFloat("uCloudHeight", ctx.cloud.height);
    m_volumetricFogShader->setFloat("uCloudThickness", ctx.cloud.thickness);
    m_volumetricFogShader->setFloat("uPlanarCloudCoverage", ctx.cloud.planarCoverage);
    m_volumetricFogShader->setFloat("uPlanarCloudDensity", ctx.cloud.planarDensity);
    m_volumetricFogShader->setFloat("uPlanarCloudAltitude", ctx.cloud.planarAltitude);

    m_volumetricFogShader->setFloat("uCloudWetness", ctx.weather.cloudWetness);
    m_volumetricFogShader->setInt("uShadowsEnabled", settings.shadow.enabled ? 1 : 0);
    m_volumetricFogShader->setFloat("uTime", ctx.shaderTime);
    m_volumetricFogShader->setBool("uNoiseEnabled", m_noiseTexture.isValid());
    m_volumetricFogShader->setInt("uVolumetricSkyRayEnabled", settings.volumetric.skyRayEnabled ? 1 : 0);
    m_volumetricFogShader->setInt("uVolumetricTimeFadeEnabled", settings.volumetric.timeFadeEnabled ? 1 : 0);
    m_volumetricFogShader->setInt("uVolumetricQualityTier", settings.volumetric.qualityTier);

    // Debug mode
    int vfDebugMode = 0;
    if (settings.debug.viewMode >= 46 && settings.debug.viewMode <= 77) {
        vfDebugMode = settings.debug.viewMode - 45;
    }
    m_volumetricFogShader->setInt("uVolumetricDebugMode", vfDebugMode);
    m_volumetricFogShader->setInt("uVolumetricStaticJitter",
        (vfDebugMode > 0 || settings.volumetric.freezeR1) ? 1 : 0);
    m_volumetricFogShader->setInt("uFrameIndex", static_cast<int>(ctx.frameIndex & 0x7fffffffULL));
    m_volumetricFogShader->setFloat("uVolumetricShadowBiasScale", settings.volumetric.shadowBiasScale);

    // Underwater
    m_volumetricFogShader->setInt("uIsEyeInWater", ctx.eyeInWater ? 1 : 0);
    m_volumetricFogShader->setVec3("uWaterAbsorption", glm::vec3(0.4f, 0.14f, 0.08f));
    m_volumetricFogShader->setFloat("uUnderwaterVolumetricLightStrength", ctx.volumetric.underwaterLightStrength);
    m_volumetricFogShader->setInt("uUwVolumetricLightEnabled", settings.volumetric.uwLightEnabled ? 1 : 0);

    // Texture bindings
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.skyCaptureTextureHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(m_noiseTexture));
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.shadowDepthTextureHandle()));
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.shadowColorTextureHandle()));
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_3D, renderer::rhi::gl::textureId(targets.atmosphereLutTextureHandle()));
    // Units 6-11: CSM shadow arrays (6 already bound)
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D_ARRAY,
                  renderer::rhi::gl::textureId(targets.csmShadowDepthTextureHandle()));
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D_ARRAY,
                  renderer::rhi::gl::textureId(targets.csmShadowDepthAllComparisonTextureHandle()));
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D_ARRAY,
                  renderer::rhi::gl::textureId(targets.csmShadowDepthAllTextureHandle()));
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D_ARRAY,
                  renderer::rhi::gl::textureId(targets.csmShadowColor0TextureHandle()));
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D_ARRAY,
                  renderer::rhi::gl::textureId(targets.csmShadowColor1TextureHandle()));

    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_volumetricFogShader);

    glUseProgram(0);
    for (int i = 11; i >= 6; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }
    for (int i = 5; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_3D, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

void VolumetricPass::renderTemporal(const FrameContext& ctx, const RenderSettings& settings,
                                     DeferredRenderTargets& targets) {
    if (m_volumetricTemporalShader == nullptr) return;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureHistoryVolumetricTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.historyVolumetricTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "VolumetricTemporal";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.halfWidth())),
        static_cast<uint32_t>(std::max(1, targets.halfHeight()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_volumetricTemporalShader->use();
    m_volumetricTemporalShader->setInt("uCurrentTex", 0);
    m_volumetricTemporalShader->setInt("uHistoryTex", 1);
    m_volumetricTemporalShader->setInt("uVelocityTex", 2);
    m_volumetricTemporalShader->setInt("uDepthTex", 3);
    m_volumetricTemporalShader->setInt("uHistoryDepthTex", 4);
    m_volumetricTemporalShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, targets.halfWidth())),
                   static_cast<float>(std::max(1, targets.halfHeight()))));
    m_volumetricTemporalShader->setFloat("uHistoryWeight", settings.volumetric.temporalWeight);
    m_volumetricTemporalShader->setFloat("uNearPlane", ctx.camera.nearPlane);
    m_volumetricTemporalShader->setFloat("uFarPlane", ctx.camera.farPlane);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.halfResTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.historyVolumetricTexturePrevHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.velocityTextureHandle()));
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.historyDepthTexturePrevHandle()));

    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_volumetricTemporalShader);

    for (int i = 4; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

void VolumetricPass::composite(const FrameContext& ctx, const RenderSettings& settings,
                                DeferredRenderTargets& targets, bool hasPreviousFrame) {
    if (m_volumetricCompositeShader == nullptr) return;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSceneResolvedTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.sceneResolvedTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "VolumetricComposite";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_volumetricCompositeShader->use();
    m_volumetricCompositeShader->setInt("uSceneTex", 0);
    m_volumetricCompositeShader->setInt("uVolumetricTex", 1);
    m_volumetricCompositeShader->setInt("uDepthTex", 2);
    m_volumetricCompositeShader->setFloat("uNearPlane", ctx.camera.nearPlane);
    m_volumetricCompositeShader->setFloat("uFarPlane", ctx.camera.farPlane);
    m_volumetricCompositeShader->setInt("uIsEyeInWater", ctx.eyeInWater ? 1 : 0);
    m_volumetricCompositeShader->setInt("uFrameIndex", static_cast<int>(ctx.frameIndex & 0x7fffffffULL));
    m_volumetricCompositeShader->setInt("uFreezeBias", settings.volumetric.freezeBias ? 1 : 0);

    const bool underwaterVolumetricActive = ctx.eyeInWater && settings.volumetric.uwLightEnabled;
    const bool volFogCompositeActive = (underwaterVolumetricActive ||
                                        settings.volumetric.lightEnabled ||
                                        (settings.volumetric.fogEnabled &&
                                         settings.volumetric.fogStrength > 0.001f));
    m_volumetricCompositeShader->setInt("uVolumetricFogActive", volFogCompositeActive ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.sceneCompositeTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    if (settings.volumetric.temporalEnabled && hasPreviousFrame && m_volumetricTemporalShader != nullptr) {
        glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.historyVolumetricTextureHandle()));
    } else {
        glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.halfResTextureHandle()));
    }
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));

    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_volumetricCompositeShader);

    for (int i = 2; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}
