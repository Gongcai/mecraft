#include "DeferredLightingPass.h"
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

void DeferredLightingPass::init(ResourceMgr& resourceMgr) {
    m_deferredLightingShader = resourceMgr.getShader("deferred_lighting");
    m_resourceMgr = &resourceMgr;
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
    m_rippleNormalTexture = resourceMgr.getTexture2DHandle("shader_ripple_normal");
}

void DeferredLightingPass::shutdown() {
    m_deferredLightingShader = nullptr;
    m_resourceMgr = nullptr;
    m_shadowRenderer = nullptr;
    m_noiseTexture = {};
    m_rippleNormalTexture = {};
}

void DeferredLightingPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                                    DeferredRenderTargets& targets) {
    if (m_deferredLightingShader == nullptr) return;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSceneLightingTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    const bool clearForDebug = settings.debug.deferredLightDebugMode > 0;
    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.sceneLightingTextureViewHandle();
    colorAttachment.loadOp = clearForDebug ? RhiLoadOp::Clear : RhiLoadOp::Load;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "DeferredLighting";
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

    // Pre-bind CSM shadow array on unit 15 before shader->use()
    glActiveTexture(GL_TEXTURE15);
    glBindTexture(GL_TEXTURE_2D_ARRAY,
                  renderer::rhi::gl::textureId(targets.csmShadowDepthComparisonTextureHandle()));

    m_deferredLightingShader->use();

    // Sampler assignments
    m_deferredLightingShader->setInt("uAlbedoTex", 0);
    m_deferredLightingShader->setInt("uNormalAoTex", 1);
    m_deferredLightingShader->setInt("uVoxelLightTex", 2);
    m_deferredLightingShader->setInt("uMaterialTex", 3);
    m_deferredLightingShader->setInt("uMaterialAuxTex", 4);
    m_deferredLightingShader->setInt("uDepthTex", 5);
    m_deferredLightingShader->setInt("uLightmapDay", 6);
    m_deferredLightingShader->setInt("uLightmapNight", 7);
    m_deferredLightingShader->setInt("uShadowMapRaw", 8);
    m_deferredLightingShader->setInt("uSsaoTex", 9);
    m_deferredLightingShader->setInt("uSkyCaptureTex", 10);
    m_deferredLightingShader->setInt("uNoiseTex", 11);
    m_deferredLightingShader->setInt("uShadowColorTex", 12);
    m_deferredLightingShader->setInt("uShadowNormalTex", 13);
    m_deferredLightingShader->setInt("uAtmosphereLut", 14);
    m_deferredLightingShader->setInt("uCsmShadowMap", 15);
    m_deferredLightingShader->setInt("uCsmShadowDepthRaw", 16);
    m_deferredLightingShader->setInt("uCsmShadowDepthAll", 17);
    m_deferredLightingShader->setInt("uCsmShadowDepthAllRaw", 18);
    m_deferredLightingShader->setInt("uCsmShadowColor0", 19);
    m_deferredLightingShader->setInt("uCsmShadowColor1", 20);
    m_deferredLightingShader->setInt("uRippleNormalTex", 21);

    // Noise
    m_deferredLightingShader->setBool("uNoiseEnabled", m_noiseTexture.isValid());

    // Camera / TAA
    m_deferredLightingShader->setMat4("uViewProj",
        settings.taa.enabled ? ctx.camera.jitteredViewProj : ctx.camera.viewProj);
    m_deferredLightingShader->setMat4("uInvViewProj",
        settings.taa.enabled ? ctx.camera.jitteredInvViewProj : ctx.camera.invViewProj);
    m_deferredLightingShader->setMat4("uProjection", ctx.camera.projection);

    // Shadow uniforms via ShadowRenderer
    if (m_shadowRenderer) {
        const shadow::ShadowRenderer::BiasSettings bias{
            settings.shadow.constantBias,
            settings.shadow.slopeBias,
            settings.shadow.normalOffset
        };
        m_shadowRenderer->bindShadowUniforms(*m_deferredLightingShader, ctx.moonShadowActive, bias);
    }

    // Sky lighting (inlined from bindSkyLightingUniforms)
    m_deferredLightingShader->setVec3("uCameraPos", ctx.camera.position);
    m_deferredLightingShader->setVec3("uSunDirection", ctx.skyColors.sunDirection);
    m_deferredLightingShader->setVec3("uMoonDirection", ctx.skyColors.moonDirection);
    m_deferredLightingShader->setVec3("uSunLightColor", ctx.skyColors.sunLightColor);
    m_deferredLightingShader->setVec3("uMoonLightColor", ctx.skyColors.moonLightColor);
    m_deferredLightingShader->setVec3("uSkyAmbientColor", ctx.skyColors.skyAmbientColor);
    m_deferredLightingShader->setVec3("uShadowTintColor", ctx.skyColors.shadowTintColor);
    m_deferredLightingShader->setVec3("uHorizonScatterColor", ctx.skyColors.horizonScatterColor);
    m_deferredLightingShader->setFloat("uSkyIntensity", ctx.skyIntensity);
    m_deferredLightingShader->setFloat("uMoonVisibility", ctx.skyColors.moonVisibility);
    m_deferredLightingShader->setVec3("uDirectIlluminance", ctx.skyIlluminance.directIlluminance);
    m_deferredLightingShader->setVec3("uSkyIlluminance", ctx.skyIlluminance.skyIlluminance);
    m_deferredLightingShader->setVec3("uSunIlluminance", ctx.skyIlluminance.sunIlluminance);
    m_deferredLightingShader->setVec3("uMoonIlluminance", ctx.skyIlluminance.moonIlluminance);
    m_deferredLightingShader->setVec3("uCloudDynamicWeather", ctx.skyIlluminance.cloudDynamicWeather);
    m_deferredLightingShader->setInt("uHeldBlockLightValue", m_heldBlockLightValue);
    m_deferredLightingShader->setInt("uHeldBlockLightValue2", 0);

    // Aerial perspective
    m_deferredLightingShader->setInt("uAerialPerspectiveEnabled", settings.postProcess.aerialPerspectiveEnabled ? 1 : 0);

    // Volumetric
    m_deferredLightingShader->setInt("uVolumetricLightEnabled", ctx.volumetric.lightEnabled ? 1 : 0);
    const bool volFogActive = (ctx.volumetric.lightEnabled ||
                               (ctx.volumetric.fogEnabled &&
                                ctx.volumetric.fogDensityScale > 0.001f)) &&
                              settings.volumetric.fogEnabled;
    m_deferredLightingShader->setInt("uVolumetricFogActive", volFogActive ? 1 : 0);

    // Debug
    m_deferredLightingShader->setInt("uDeferredDebugMode", settings.debug.deferredLightDebugMode);

    // Weather / rain
    m_deferredLightingShader->setInt("uRainWetSurfacesEnabled", settings.weather.rainLinesEnabled ? 1 : 0);
    m_deferredLightingShader->setInt("uRainSurfaceRipplesEnabled", settings.weather.surfaceRipplesEnabled ? 1 : 0);

    // Lighting parameters
    m_deferredLightingShader->setFloat("uShadowTintStrength", settings.postProcess.shadowTintStrength);
    m_deferredLightingShader->setFloat("uDirectSunStrength", settings.postProcess.directSunStrength);
    m_deferredLightingShader->setFloat("uSkyAmbientStrength", settings.postProcess.skyAmbientStrength);
    m_deferredLightingShader->setFloat("uWeatherSkylightScale", settings.weather.skylightScale);
    m_deferredLightingShader->setFloat("uMinimumAmbient", settings.postProcess.minimumAmbient);
    m_deferredLightingShader->setFloat("uShadowMinLight", settings.postProcess.shadowMinLight);
    m_deferredLightingShader->setFloat("uShadowContrast", settings.postProcess.shadowContrast);
    m_deferredLightingShader->setFloat("uBlockLightStrength", settings.postProcess.blockLightStrength);
    m_deferredLightingShader->setFloat("uFakeBounceStrength", settings.postProcess.fakeBounceStrength);
    m_deferredLightingShader->setFloat("uAlbedoDesaturation", settings.postProcess.albedoDesaturation);
    m_deferredLightingShader->setFloat("uShadowDesaturation", settings.postProcess.shadowDesaturation);
    m_deferredLightingShader->setInt("uDerivativeStrictMode", settings.debug.derivativeStrictMode ? 1 : 0);

    // Atmosphere (inlined from bindAtmosphereUniforms)
    m_deferredLightingShader->setFloat("uAerialStrength", ctx.atmosphere.aerialStrength);
    m_deferredLightingShader->setFloat("uHorizonScatterStrength", ctx.atmosphere.horizonScatterStrength);
    m_deferredLightingShader->setFloat("uSunWarmth", ctx.atmosphere.sunWarmth);
    m_deferredLightingShader->setFloat("uSkyCoolness", ctx.atmosphere.skyCoolness);
    m_deferredLightingShader->setFloat("uWeatherWetness", ctx.weather.wetness);
    m_deferredLightingShader->setFloat("uWeatherStorm", ctx.weather.storm);
    m_deferredLightingShader->setFloat("uAerialReduction", ctx.weather.aerialReduction);
    m_deferredLightingShader->setFloat("uLightningFlash", ctx.weather.lightningFlash);
    m_deferredLightingShader->setFloat("uSurfaceWetness", ctx.weather.surfaceWetness);
    m_deferredLightingShader->setFloat("uSkyWetness", ctx.weather.skyWetness);
    m_deferredLightingShader->setFloat("uFogWetness", ctx.weather.fogWetness);
    m_deferredLightingShader->setFloat("uCloudWetness", ctx.weather.cloudWetness);
    m_deferredLightingShader->setFloat("uPrecipitation", ctx.weather.precipitation);
    m_deferredLightingShader->setFloat("uDirectWeatherOcclusion", ctx.atmosphere.directWeatherOcclusion);
    m_deferredLightingShader->setInt("uDirectWeatherOcclusionOverride", ctx.atmosphere.directWeatherOcclusionOverride);

    // Shadow settings
    m_deferredLightingShader->setInt("uShadowsEnabled", settings.shadow.enabled ? 1 : 0);
    m_deferredLightingShader->setInt("uSoftShadowsEnabled", settings.shadow.softShadowsEnabled ? 1 : 0);
    m_deferredLightingShader->setInt("uPcssShadowsEnabled", settings.shadow.pcssShadowsEnabled ? 1 : 0);
    m_deferredLightingShader->setInt("uContactShadowsEnabled", settings.shadow.contactShadowsEnabled ? 1 : 0);

    // Cloud (inlined from bindCloudUniforms)
    m_deferredLightingShader->setInt("uCloudShadowsEnabled", ctx.cloud.shadowsEnabled ? 1 : 0);
    m_deferredLightingShader->setFloat("uCloudShadowStrength", ctx.cloud.shadowStrength);
    m_deferredLightingShader->setFloat("uCloudShadowScale", ctx.cloud.shadowScale);
    m_deferredLightingShader->setFloat("uCloudShadowSpeed", ctx.cloud.shadowSpeed);
    m_deferredLightingShader->setFloat("uCloudTimeScale", ctx.cloud.timeScale);
    m_deferredLightingShader->setFloat("uCloudCoverage", ctx.cloud.coverage);
    m_deferredLightingShader->setFloat("uCloudDensity", ctx.cloud.density);
    m_deferredLightingShader->setFloat("uCloudHeight", ctx.cloud.height);
    m_deferredLightingShader->setFloat("uCloudThickness", ctx.cloud.thickness);
    m_deferredLightingShader->setFloat("uPlanarCloudCoverage", ctx.cloud.planarCoverage);
    m_deferredLightingShader->setFloat("uPlanarCloudDensity", ctx.cloud.planarDensity);
    m_deferredLightingShader->setFloat("uPlanarCloudAltitude", ctx.cloud.planarAltitude);
    m_deferredLightingShader->setFloat("uCloudWetness", ctx.weather.cloudWetness);

    // Shadow detail
    m_deferredLightingShader->setFloat("uShadowSoftness", settings.shadow.softness);
    m_deferredLightingShader->setFloat("uShadowPcssStrength", settings.shadow.pcssStrength);
    m_deferredLightingShader->setFloat("uShadowNormalOffset", settings.shadow.normalOffset);
    m_deferredLightingShader->setFloat("uContactShadowStrength", settings.shadow.contactShadowStrength);

    // Timing / state
    m_deferredLightingShader->setFloat("uTime", ctx.shaderTime);
    m_deferredLightingShader->setInt("uSsaoEnabled", settings.ssao.enabled ? 1 : 0);
    m_deferredLightingShader->setInt("uIsEyeInWater", ctx.eyeInWater ? 1 : 0);

    // Fog (inlined from bindFogUniforms)
    m_deferredLightingShader->setInt("uFogEnabled", ctx.fog.enabled ? 1 : 0);
    m_deferredLightingShader->setInt("uFogMode", 0); // Linear fog mode
    m_deferredLightingShader->setVec3("uFogColor", ctx.fog.color);
    m_deferredLightingShader->setFloat("uFogStart", ctx.fog.startDistance);
    m_deferredLightingShader->setFloat("uFogEnd", ctx.fog.endDistance);
    m_deferredLightingShader->setFloat("uFogDensity", ctx.fog.density);

    // === Texture bindings ===
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.albedoTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.normalAoTextureHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.voxelLightTextureHandle()));
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.materialTextureHandle()));
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.materialAuxTextureHandle()));
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    const uint32_t lightmapDayId = renderer::rhi::gl::textureId(
        m_resourceMgr ? m_resourceMgr->getLightmapDay() : RhiTextureHandle{});
    const uint32_t lightmapNightId = renderer::rhi::gl::textureId(
        m_resourceMgr ? m_resourceMgr->getLightmapNight() : RhiTextureHandle{});
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, lightmapDayId);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, lightmapNightId);
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.shadowDepthTextureHandle()));
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, settings.ssao.temporalEnabled
        ? renderer::rhi::gl::textureId(targets.ssaoTemporalTextureHandle())
        : renderer::rhi::gl::textureId(targets.ssaoFilteredTextureHandle()));
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.skyCaptureTextureHandle()));
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(m_noiseTexture));
    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.shadowColorTextureHandle()));
    glActiveTexture(GL_TEXTURE13);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.shadowNormalTextureHandle()));
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(GL_TEXTURE_3D, renderer::rhi::gl::textureId(targets.atmosphereLutTextureHandle()));
    // Units 15-20: CSM shadow arrays (15 already bound before shader->use())
    glActiveTexture(GL_TEXTURE16);
    glBindTexture(GL_TEXTURE_2D_ARRAY,
                  renderer::rhi::gl::textureId(targets.csmShadowDepthTextureHandle()));
    glActiveTexture(GL_TEXTURE17);
    glBindTexture(GL_TEXTURE_2D_ARRAY,
                  renderer::rhi::gl::textureId(targets.csmShadowDepthAllComparisonTextureHandle()));
    glActiveTexture(GL_TEXTURE18);
    glBindTexture(GL_TEXTURE_2D_ARRAY,
                  renderer::rhi::gl::textureId(targets.csmShadowDepthAllTextureHandle()));
    glActiveTexture(GL_TEXTURE19);
    glBindTexture(GL_TEXTURE_2D_ARRAY,
                  renderer::rhi::gl::textureId(targets.csmShadowColor0TextureHandle()));
    glActiveTexture(GL_TEXTURE20);
    glBindTexture(GL_TEXTURE_2D_ARRAY,
                  renderer::rhi::gl::textureId(targets.csmShadowColor1TextureHandle()));
    glActiveTexture(GL_TEXTURE21);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(m_rippleNormalTexture));

    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_deferredLightingShader);

    // Cleanup: unbind units 21 down to 15 (arrays), then 14 down to 0
    glActiveTexture(GL_TEXTURE21);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    for (int i = 20; i >= 15; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }
    for (int i = 14; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(GL_TEXTURE_3D, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}
