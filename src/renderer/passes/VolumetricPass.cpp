#include "VolumetricPass.h"
#include "../core/RenderScene.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../../resource/ResourceMgr.h"
#include "../shadow/ShadowRenderer.h"

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <optional>

namespace {
[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

template <size_t Count>
[[nodiscard]] bool sameTextureViews(const std::array<RhiTextureViewHandle, Count>& lhs,
                                    const std::array<RhiTextureViewHandle, Count>& rhs) {
    for (size_t i = 0u; i < lhs.size(); ++i) {
        if (!sameTextureView(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}
} // namespace

void VolumetricPass::init(ResourceMgr& resourceMgr) {
    m_volumetricFogShader = resourceMgr.getShader("volumetric_fog");
    m_resourceMgr = &resourceMgr;
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void VolumetricPass::shutdown() {
    destroyTemporalRhiResources();
    destroyCompositeRhiResources();
    m_volumetricFogShader = nullptr;
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
        if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
            return;
        }
        targets.copyHistoryVolumetricToHalfRes(*ctx.shared->rhiDevice);
    }

    // Temporal resolve (optional)
    if (renderCurrentFog && settings.volumetric.temporalEnabled && hasPreviousFrame &&
        hasTemporalShader()) {
        renderTemporal(ctx, settings, targets);
    }

    // Composite (always runs to ensure correct transmittance)
    composite(ctx, settings, targets, hasPreviousFrame);
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
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!targets.ensureHistoryVolumetricTextureViews(rhiDevice) ||
        !targets.ensureHistoryDepthTextureViews(rhiDevice) ||
        !targets.ensureHalfResTextureView(rhiDevice) ||
        !targets.ensureVelocityTextureView(rhiDevice) ||
        !targets.ensureGBufferTextureViews(rhiDevice)) {
        return;
    }

    const std::array<RhiTextureViewHandle, 5> views = {
        targets.halfResTextureViewHandle(),
        targets.historyVolumetricTexturePrevViewHandle(),
        targets.velocityTextureViewHandle(),
        targets.depthTextureViewHandle(),
        targets.historyDepthTexturePrevViewHandle()
    };
    if (!ensureTemporalRhiPipeline(rhiDevice) ||
        !ensureTemporalBindGroup(rhiDevice, views)) {
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

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);
    const glm::vec4 pushConstants[2] = {
        glm::vec4(static_cast<float>(std::max(1, targets.halfWidth())),
                  static_cast<float>(std::max(1, targets.halfHeight())),
                  settings.volumetric.temporalWeight,
                  ctx.camera.nearPlane),
        glm::vec4(ctx.camera.farPlane, 0.0f, 0.0f, 0.0f)
    };
    commandList.setGraphicsPipeline(m_temporalPipeline);
    commandList.setBindGroup(0u, m_temporalBindGroup);
    commandList.pushConstants(pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

void VolumetricPass::composite(const FrameContext& ctx, const RenderSettings& settings,
                                DeferredRenderTargets& targets, bool hasPreviousFrame) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const bool useTemporalVolumetric = settings.volumetric.temporalEnabled &&
                                       hasPreviousFrame &&
                                       hasTemporalShader();
    if (!targets.ensureSceneResolvedTextureView(rhiDevice) ||
        !targets.ensureSceneCompositeTextureView(rhiDevice) ||
        !targets.ensureGBufferTextureViews(rhiDevice) ||
        !(useTemporalVolumetric
              ? targets.ensureHistoryVolumetricTextureView(rhiDevice)
              : targets.ensureHalfResTextureView(rhiDevice))) {
        return;
    }

    const std::array<RhiTextureViewHandle, 3> views = {
        targets.sceneCompositeTextureViewHandle(),
        useTemporalVolumetric
            ? targets.historyVolumetricTextureViewHandle()
            : targets.halfResTextureViewHandle(),
        targets.depthTextureViewHandle()
    };
    if (!ensureCompositeRhiPipeline(rhiDevice) ||
        !ensureCompositeBindGroup(rhiDevice, views)) {
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

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    const bool underwaterVolumetricActive = ctx.eyeInWater && settings.volumetric.uwLightEnabled;
    const bool volFogCompositeActive = (underwaterVolumetricActive ||
                                        settings.volumetric.lightEnabled ||
                                        (settings.volumetric.fogEnabled &&
                                         settings.volumetric.fogStrength > 0.001f));
    struct CompositePushConstants {
        glm::vec4 depthParams;
        glm::ivec4 flags;
    };
    const CompositePushConstants pushConstants{
        glm::vec4(ctx.camera.nearPlane, ctx.camera.farPlane, 0.0f, 0.0f),
        glm::ivec4(static_cast<int>(ctx.frameIndex & 0x7fffffffULL),
                   settings.volumetric.freezeBias ? 1 : 0,
                   ctx.eyeInWater ? 1 : 0,
                   volFogCompositeActive ? 1 : 0)
    };
    commandList.setGraphicsPipeline(m_compositePipeline);
    commandList.setBindGroup(0u, m_compositeBindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

bool VolumetricPass::ensureCompositeRhiPipeline(RhiDevice& rhiDevice) {
    if (m_compositeRhiDevice != nullptr && m_compositeRhiDevice != &rhiDevice) {
        destroyCompositeRhiResources();
    }
    if (m_compositePipeline.isValid()) {
        return true;
    }
    m_compositeRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/volumetric_composite.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "VolumetricComposite.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_compositeVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "VolumetricComposite.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_compositeFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_compositeVertexShader.isValid() || !m_compositeFragmentShader.isValid()) {
        destroyCompositeRhiResources();
        return false;
    }

    RhiSamplerDesc nearestSamplerDesc;
    nearestSamplerDesc.minFilter = RhiFilter::Nearest;
    nearestSamplerDesc.magFilter = RhiFilter::Nearest;
    nearestSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    nearestSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_compositeNearestSampler = rhiDevice.createSampler(nearestSamplerDesc);

    RhiSamplerDesc linearSamplerDesc;
    linearSamplerDesc.minFilter = RhiFilter::Linear;
    linearSamplerDesc.magFilter = RhiFilter::Linear;
    linearSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    linearSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_compositeLinearSampler = rhiDevice.createSampler(linearSamplerDesc);
    if (!m_compositeNearestSampler.isValid() || !m_compositeLinearSampler.isValid()) {
        destroyCompositeRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "VolumetricComposite.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    m_compositeBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_compositeBindGroupLayout.isValid()) {
        destroyCompositeRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "VolumetricComposite.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_compositeBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4) + sizeof(glm::ivec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_compositePipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_compositePipelineLayout.isValid()) {
        destroyCompositeRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "VolumetricComposite.Pipeline";
    pipelineDesc.vertexShader = m_compositeVertexShader;
    pipelineDesc.fragmentShader = m_compositeFragmentShader;
    pipelineDesc.layout = m_compositePipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    m_compositePipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_compositePipeline.isValid()) {
        destroyCompositeRhiResources();
        return false;
    }

    return true;
}

bool VolumetricPass::ensureCompositeBindGroup(
    RhiDevice& rhiDevice,
    const std::array<RhiTextureViewHandle, 3>& views) {
    if (!ensureCompositeRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_compositeBindGroup.isValid() && sameTextureViews(m_compositeBoundViews, views)) {
        return true;
    }

    destroyCompositeBindGroup();

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_compositeBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler =
            binding == 2u ? m_compositeNearestSampler : m_compositeLinearSampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_compositeBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_compositeBindGroup.isValid()) {
        m_compositeBoundViews = {};
        return false;
    }

    m_compositeBoundViews = views;
    return true;
}

void VolumetricPass::destroyCompositeBindGroup() {
    if (m_compositeRhiDevice != nullptr && m_compositeBindGroup.isValid()) {
        m_compositeRhiDevice->destroyBindGroup(m_compositeBindGroup);
    }
    m_compositeBindGroup = {};
    m_compositeBoundViews = {};
}

void VolumetricPass::destroyCompositeRhiResources() {
    destroyCompositeBindGroup();
    if (m_compositeRhiDevice != nullptr) {
        if (m_compositePipeline.isValid()) {
            m_compositeRhiDevice->destroyPipeline(m_compositePipeline);
        }
        if (m_compositeVertexShader.isValid()) {
            m_compositeRhiDevice->destroyShader(m_compositeVertexShader);
        }
        if (m_compositeFragmentShader.isValid()) {
            m_compositeRhiDevice->destroyShader(m_compositeFragmentShader);
        }
        if (m_compositePipelineLayout.isValid()) {
            m_compositeRhiDevice->destroyPipelineLayout(m_compositePipelineLayout);
        }
        if (m_compositeBindGroupLayout.isValid()) {
            m_compositeRhiDevice->destroyBindGroupLayout(m_compositeBindGroupLayout);
        }
        if (m_compositeNearestSampler.isValid()) {
            m_compositeRhiDevice->destroySampler(m_compositeNearestSampler);
        }
        if (m_compositeLinearSampler.isValid()) {
            m_compositeRhiDevice->destroySampler(m_compositeLinearSampler);
        }
    }

    m_compositePipeline = {};
    m_compositeVertexShader = {};
    m_compositeFragmentShader = {};
    m_compositePipelineLayout = {};
    m_compositeBindGroupLayout = {};
    m_compositeNearestSampler = {};
    m_compositeLinearSampler = {};
    m_compositeRhiDevice = nullptr;
}

bool VolumetricPass::ensureTemporalRhiPipeline(RhiDevice& rhiDevice) {
    if (m_temporalRhiDevice != nullptr && m_temporalRhiDevice != &rhiDevice) {
        destroyTemporalRhiResources();
    }
    if (m_temporalPipeline.isValid()) {
        return true;
    }
    m_temporalRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/volumetric_temporal.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "VolumetricTemporal.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_temporalVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "VolumetricTemporal.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_temporalFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_temporalVertexShader.isValid() || !m_temporalFragmentShader.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiSamplerDesc nearestSamplerDesc;
    nearestSamplerDesc.minFilter = RhiFilter::Nearest;
    nearestSamplerDesc.magFilter = RhiFilter::Nearest;
    nearestSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    nearestSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_temporalNearestSampler = rhiDevice.createSampler(nearestSamplerDesc);

    RhiSamplerDesc linearSamplerDesc;
    linearSamplerDesc.minFilter = RhiFilter::Linear;
    linearSamplerDesc.magFilter = RhiFilter::Linear;
    linearSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    linearSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_temporalLinearSampler = rhiDevice.createSampler(linearSamplerDesc);
    if (!m_temporalNearestSampler.isValid() || !m_temporalLinearSampler.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "VolumetricTemporal.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 5u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    m_temporalBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_temporalBindGroupLayout.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "VolumetricTemporal.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_temporalBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4) * 2u);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_temporalPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_temporalPipelineLayout.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "VolumetricTemporal.Pipeline";
    pipelineDesc.vertexShader = m_temporalVertexShader;
    pipelineDesc.fragmentShader = m_temporalFragmentShader;
    pipelineDesc.layout = m_temporalPipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    m_temporalPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_temporalPipeline.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    return true;
}

bool VolumetricPass::ensureTemporalBindGroup(
    RhiDevice& rhiDevice,
    const std::array<RhiTextureViewHandle, 5>& views) {
    if (!ensureTemporalRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_temporalBindGroup.isValid() && sameTextureViews(m_temporalBoundViews, views)) {
        return true;
    }

    destroyTemporalBindGroup();

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_temporalBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler =
            binding == 1u ? m_temporalLinearSampler : m_temporalNearestSampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_temporalBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_temporalBindGroup.isValid()) {
        m_temporalBoundViews = {};
        return false;
    }

    m_temporalBoundViews = views;
    return true;
}

void VolumetricPass::destroyTemporalBindGroup() {
    if (m_temporalRhiDevice != nullptr && m_temporalBindGroup.isValid()) {
        m_temporalRhiDevice->destroyBindGroup(m_temporalBindGroup);
    }
    m_temporalBindGroup = {};
    m_temporalBoundViews = {};
}

void VolumetricPass::destroyTemporalRhiResources() {
    destroyTemporalBindGroup();
    if (m_temporalRhiDevice != nullptr) {
        if (m_temporalPipeline.isValid()) {
            m_temporalRhiDevice->destroyPipeline(m_temporalPipeline);
        }
        if (m_temporalVertexShader.isValid()) {
            m_temporalRhiDevice->destroyShader(m_temporalVertexShader);
        }
        if (m_temporalFragmentShader.isValid()) {
            m_temporalRhiDevice->destroyShader(m_temporalFragmentShader);
        }
        if (m_temporalPipelineLayout.isValid()) {
            m_temporalRhiDevice->destroyPipelineLayout(m_temporalPipelineLayout);
        }
        if (m_temporalBindGroupLayout.isValid()) {
            m_temporalRhiDevice->destroyBindGroupLayout(m_temporalBindGroupLayout);
        }
        if (m_temporalNearestSampler.isValid()) {
            m_temporalRhiDevice->destroySampler(m_temporalNearestSampler);
        }
        if (m_temporalLinearSampler.isValid()) {
            m_temporalRhiDevice->destroySampler(m_temporalLinearSampler);
        }
    }

    m_temporalPipeline = {};
    m_temporalVertexShader = {};
    m_temporalFragmentShader = {};
    m_temporalPipelineLayout = {};
    m_temporalBindGroupLayout = {};
    m_temporalNearestSampler = {};
    m_temporalLinearSampler = {};
    m_temporalRhiDevice = nullptr;
}
