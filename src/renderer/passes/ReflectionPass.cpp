#include "ReflectionPass.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../../resource/ResourceMgr.h"

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <algorithm>

void ReflectionPass::init(ResourceMgr& resourceMgr) {
    m_reflectionShader = resourceMgr.getShader("reflection_probe");
    m_reflectionFilterShader = resourceMgr.getShader("reflection_filter");
    m_reflectionTemporalShader = resourceMgr.getShader("reflection_temporal");
    m_noiseTexture = resourceMgr.getTexture2D("shader_noise2d");
    m_rippleNormalTexture = resourceMgr.getTexture2D("shader_ripple_normal");
    m_resourceMgr = &resourceMgr;
}

void ReflectionPass::shutdown() {
    m_reflectionShader = nullptr;
    m_reflectionFilterShader = nullptr;
    m_reflectionTemporalShader = nullptr;
    m_noiseTexture = 0;
    m_rippleNormalTexture = 0;
    m_resourceMgr = nullptr;
}

void ReflectionPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                              DeferredRenderTargets& targets) {
    if (m_reflectionShader == nullptr) return;

    renderReflection(ctx, settings, targets);

    if (settings.reflection.filterEnabled &&
        settings.debug.reflectionDebugMode == 0 &&
        m_reflectionFilterShader != nullptr) {
        renderFilter(ctx, settings.reflection, targets);
    }
    if (settings.reflection.temporalEnabled &&
        settings.debug.reflectionDebugMode == 0 &&
        m_reflectionTemporalShader != nullptr &&
        ctx.hasPreviousFrame) {
        renderTemporal(ctx, settings.reflection, targets);
    }
}

void ReflectionPass::renderReflection(const FrameContext& ctx, const RenderSettings& settings,
                                       DeferredRenderTargets& targets) {
    if (m_reflectionShader == nullptr) return;

    targets.bindReflection();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_reflectionShader->use();
    m_reflectionShader->setInt("uSceneLightingTex", 0);
    m_reflectionShader->setInt("uDepthTex", 1);
    m_reflectionShader->setInt("uNormalAoTex", 2);
    m_reflectionShader->setInt("uMaterialTex", 3);
    m_reflectionShader->setInt("uMaterialAuxTex", 4);
    m_reflectionShader->setInt("uSkyCaptureTex", 5);
    m_reflectionShader->setInt("uAtmosphereLut", 6);
    m_reflectionShader->setInt("uVoxelLightTex", 7);
    m_reflectionShader->setInt("uNoiseTex", 8);
    m_reflectionShader->setInt("uRippleNormalTex", 9);
    m_reflectionShader->setMat4("uViewProj",
        settings.taa.enabled ? ctx.camera.jitteredViewProj : ctx.camera.viewProj);
    m_reflectionShader->setMat4("uInvViewProj",
        settings.taa.enabled ? ctx.camera.jitteredInvViewProj : ctx.camera.invViewProj);
    m_reflectionShader->setVec3("uCameraPos", ctx.camera.position);
    m_reflectionShader->setVec3("uSunDirection", ctx.skyColors.sunDirection);
    m_reflectionShader->setVec3("uMoonDirection", ctx.skyColors.moonDirection);
    m_reflectionShader->setFloat("uSkyIntensity", ctx.skyIntensity);
    m_reflectionShader->setFloat("uMoonVisibility", ctx.skyColors.moonVisibility);
    m_reflectionShader->setFloat("uWeatherWetness", ctx.weather.wetness);
    m_reflectionShader->setFloat("uSurfaceWetness", ctx.weather.surfaceWetness);
    m_reflectionShader->setFloat("uSkyWetness", ctx.weather.skyWetness);
    m_reflectionShader->setFloat("uFogWetness", ctx.weather.fogWetness);
    m_reflectionShader->setFloat("uCloudWetness", ctx.weather.cloudWetness);
    m_reflectionShader->setFloat("uTime", ctx.shaderTime);
    m_reflectionShader->setInt("uReflectionDebugMode", settings.debug.reflectionDebugMode);
    m_reflectionShader->setInt("uRainWetSurfacesEnabled", settings.weather.rainLinesEnabled ? 1 : 0);
    m_reflectionShader->setInt("uRainSurfaceRipplesEnabled", settings.weather.surfaceRipplesEnabled ? 1 : 0);
    m_reflectionShader->setFloat("uNearPlane", ctx.camera.nearPlane);
    m_reflectionShader->setFloat("uFarPlane", ctx.camera.farPlane);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, targets.sceneLightingTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, targets.depthTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, targets.normalAoTexture());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, targets.materialTexture());
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, targets.materialAuxTexture());
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, targets.skyCaptureTexture());
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_3D, targets.atmosphereLutTexture());
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, targets.voxelLightTexture());
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, m_noiseTexture);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, m_rippleNormalTexture);
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_reflectionShader);

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, 0);
    for (int unit = 5; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_3D, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void ReflectionPass::renderFilter(const FrameContext& ctx, const ReflectionSettings& reflection,
                                   DeferredRenderTargets& targets) {
    if (m_reflectionFilterShader == nullptr) return;

    targets.copyReflectionToTemporalScratch();

    targets.bindReflection();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_reflectionFilterShader->use();
    m_reflectionFilterShader->setInt("uReflectionTex", 0);
    m_reflectionFilterShader->setInt("uDepthTex", 1);
    m_reflectionFilterShader->setInt("uNormalAoTex", 2);
    m_reflectionFilterShader->setInt("uMaterialTex", 3);
    m_reflectionFilterShader->setInt("uMaterialAuxTex", 4);
    m_reflectionFilterShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, targets.width())),
                   static_cast<float>(std::max(1, targets.height()))));
    m_reflectionFilterShader->setFloat("uFilterStrength", reflection.filterStrength);
    m_reflectionFilterShader->setFloat("uSurfaceWetness", ctx.weather.surfaceWetness);
    m_reflectionFilterShader->setMat4("uInvViewProj", ctx.camera.invViewProj);
    m_reflectionFilterShader->setVec3("uCameraPos", ctx.camera.position);
    m_reflectionFilterShader->setFloat("uNearPlane", ctx.camera.nearPlane);
    m_reflectionFilterShader->setFloat("uFarPlane", ctx.camera.farPlane);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, targets.reflectionTemporalScratchTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, targets.depthTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, targets.normalAoTexture());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, targets.materialTexture());
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, targets.materialAuxTexture());
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_reflectionFilterShader);

    for (int i = 4; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void ReflectionPass::renderTemporal(const FrameContext& ctx, const ReflectionSettings& reflection,
                                     DeferredRenderTargets& targets) {
    if (m_reflectionTemporalShader == nullptr) return;

    targets.copyReflectionToTemporalScratch();

    targets.bindReflection();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_reflectionTemporalShader->use();
    m_reflectionTemporalShader->setInt("uCurrentTex", 0);
    m_reflectionTemporalShader->setInt("uHistoryTex", 1);
    m_reflectionTemporalShader->setInt("uVelocityTex", 2);
    m_reflectionTemporalShader->setInt("uDepthTex", 3);
    m_reflectionTemporalShader->setInt("uNormalAoTex", 4);
    m_reflectionTemporalShader->setInt("uMaterialTex", 5);
    m_reflectionTemporalShader->setInt("uMaterialAuxTex", 6);
    m_reflectionTemporalShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, targets.width())),
                   static_cast<float>(std::max(1, targets.height()))));
    m_reflectionTemporalShader->setFloat("uHistoryWeight", reflection.historyWeight);
    m_reflectionTemporalShader->setFloat("uNear", ctx.camera.nearPlane);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, targets.reflectionTemporalScratchTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, targets.historyReflectionTexturePrev());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, targets.velocityTexture());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, targets.depthTexture());
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, targets.normalAoTexture());
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, targets.materialTexture());
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, targets.materialAuxTexture());

    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_reflectionTemporalShader);

    for (int i = 6; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
