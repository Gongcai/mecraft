#include "PostProcessPass.h"
#include "../../Diagnostics.h"
#include "../core/Shader.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../../resource/ResourceMgr.h"
#include "engine/platform/Window.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <cassert>
#include <iostream>

namespace {
constexpr GLint kExposureDownsampleSourceIsSceneLocation = 0;
constexpr GLint kExposureDownsampleSourceSizeLocation = 1;
constexpr GLint kExposureDownsampleSourceLodLocation = 2;
constexpr GLint kExposureResolveFrameTimeLocation = 0;
constexpr GLint kExposureResolveSpeedLocation = 1;
constexpr GLint kExposureResolveBiasLocation = 2;
constexpr GLint kExposureResolveManualLocation = 3;
constexpr GLint kExposureResolveInitializedLocation = 4;
constexpr GLint kExposureResolveReusePreviousTargetLocation = 5;
constexpr GLint kBloomExtractSourceLodLocation = 0;
constexpr GLint kBloomBlurDirectionLocation = 0;
constexpr GLint kBloomBlurWeightLocation = 1;

RhiTextureHandle registerPostProcessTexture(const uint32_t texture,
                                            const RhiTextureFormat format,
                                            const int width,
                                            const int height,
                                            const RhiTextureUsageFlags usage) {
    return renderer::rhi::gl::registerTexture({
        texture,
        RhiTextureDimension::Texture2D,
        format,
        static_cast<uint32_t>(std::max(1, width)),
        static_cast<uint32_t>(std::max(1, height)),
        1u,
        1u,
        1u,
        usage,
        false
    });
}

bool blitPostProcessTextureToSwapchain(RhiDevice& rhiDevice,
                                       const RhiTextureViewHandle swapchainColorView,
                                       const RhiTextureHandle source,
                                       const int width,
                                       const int height) {
    if (!source.isValid() || !renderer::rhi::gl::isTextureRegistered(source) ||
        !swapchainColorView.isValid() ||
        !rhiDevice.resizeSwapchain(static_cast<uint32_t>(std::max(1, width)),
                                   static_cast<uint32_t>(std::max(1, height)))) {
        return false;
    }

    RhiTextureBlit blit;
    blit.src = source;
    blit.dstView = swapchainColorView;

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.blitTexture(blit);
    rhiDevice.submitFrame(commandList);
    return true;
}

void beginPostProcessColorOutput(RhiCommandList& commandList,
                                 const char* debugName,
                                 const RhiTextureViewHandle view,
                                 const int width,
                                 const int height,
                                 const bool clearColor) {
    RhiColorAttachment colorAttachment;
    colorAttachment.view = view;
    colorAttachment.loadOp = clearColor ? RhiLoadOp::Clear : RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = debugName;
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, width)),
        static_cast<uint32_t>(std::max(1, height))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    commandList.beginRendering(renderingInfo);
}
}

PostProcessPass::~PostProcessPass() {
    shutdown();
}

void PostProcessPass::init(ResourceMgr& resourceMgr) {
    m_postProcessShader = resourceMgr.getShader("postprocess");
    m_bloomExtractShader = resourceMgr.getShader("bloom_extract");
    m_bloomBlurShader = resourceMgr.getShader("bloom_blur");
    m_exposureDownsampleShader = resourceMgr.getShader("exposure_downsample");
    m_exposureResolveShader = resourceMgr.getShader("exposure_resolve");
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_bayer256");
    glCreateBuffers(1, &m_compositeParamsBuffer);
    glNamedBufferStorage(m_compositeParamsBuffer,
                         static_cast<GLsizeiptr>(sizeof(PostProcessCompositeParams)),
                         nullptr,
                         GL_DYNAMIC_STORAGE_BIT);
    initFullscreenTriangle();
}

void PostProcessPass::shutdown() {
    destroyRenderTargets();
    destroyFullscreenTriangle();
    if (m_compositeParamsBuffer != 0) {
        glDeleteBuffers(1, &m_compositeParamsBuffer);
        m_compositeParamsBuffer = 0;
    }
    m_postProcessShader = nullptr;
    m_bloomExtractShader = nullptr;
    m_bloomBlurShader = nullptr;
    m_exposureDownsampleShader = nullptr;
    m_exposureResolveShader = nullptr;
    m_noiseTexture = {};
    m_sceneCaptured = false;
    m_renderCompositeToTexture = false;
    m_targetWidth = 0;
    m_targetHeight = 0;
    m_autoExposureSampleAccumulator = 0.0;
}

void PostProcessPass::setFrameEffects(const PostProcessEffects& effects) {
    m_effects = effects;
    m_effects.underwaterStrength = std::clamp(m_effects.underwaterStrength, 0.0f, 1.0f);
    m_effects.bloomThreshold = std::clamp(m_effects.bloomThreshold, 0.0f, 4.0f);
    m_effects.bloomStrength = std::clamp(m_effects.bloomStrength, 0.0f, 20.0f);
    m_effects.bloomMipCount = std::clamp(m_effects.bloomMipCount, 1, kBloomMipCount);
    m_effects.autoExposureMin = std::clamp(m_effects.autoExposureMin, 0.001f, 64.0f);
    m_effects.autoExposureMax = std::clamp(m_effects.autoExposureMax, m_effects.autoExposureMin, 64.0f);
    m_effects.autoExposureSpeed = std::clamp(m_effects.autoExposureSpeed, 0.05f, 12.0f);
    m_effects.autoExposureBias = std::clamp(m_effects.autoExposureBias, -3.0f, 3.0f);
    m_effects.autoExposureDayFactor = std::clamp(m_effects.autoExposureDayFactor, 0.0f, 1.0f);
    m_effects.sunScreenPos.x = std::clamp(m_effects.sunScreenPos.x, -1.0f, 2.0f);
    m_effects.sunScreenPos.y = std::clamp(m_effects.sunScreenPos.y, -1.0f, 2.0f);
    m_effects.sunVisibility = std::clamp(m_effects.sunVisibility, 0.0f, 1.0f);
    m_effects.sunRayStrength = std::clamp(m_effects.sunRayStrength, 0.0f, 1.0f);
    m_effects.tonemapMode = std::clamp(m_effects.tonemapMode, 0, 5);
    m_effects.colorTemperature = std::clamp(m_effects.colorTemperature, 0.0f, 2.0f);
    m_effects.vibrance = std::clamp(m_effects.vibrance, -1.0f, 1.0f);
    m_effects.highlightCompression = std::clamp(m_effects.highlightCompression, 0.0f, 1.5f);
    m_effects.filmEmulationStrength = std::clamp(m_effects.filmEmulationStrength, 0.0f, 1.0f);
    m_effects.redModifierStrength = std::clamp(m_effects.redModifierStrength, 0.0f, 1.0f);
    m_effects.colorLuma.x = std::clamp(m_effects.colorLuma.x, 0.5f, 1.5f);
    m_effects.colorLuma.y = std::clamp(m_effects.colorLuma.y, 0.5f, 1.5f);
    m_effects.colorLuma.z = std::clamp(m_effects.colorLuma.z, 0.5f, 1.5f);
    m_effects.splitToneStrength = std::clamp(m_effects.splitToneStrength, 0.0f, 1.0f);
    m_effects.vignetteStrength = std::clamp(m_effects.vignetteStrength, 0.0f, 0.5f);
    m_effects.noiseDitherStrength = std::clamp(m_effects.noiseDitherStrength, 0.0f, 0.08f);
    m_effects.sharpenStrength = std::clamp(m_effects.sharpenStrength, 0.0f, 1.0f);
    m_effects.exposure = std::clamp(m_effects.exposure, 0.1f, 50.0f);
    m_effects.gamma = std::clamp(m_effects.gamma, 1.0f, 3.5f);
    m_effects.saturation = std::clamp(m_effects.saturation, 0.0f, 3.0f);
    m_effects.contrast = std::clamp(m_effects.contrast, 0.25f, 3.0f);
    m_effects.weatherWetness = std::clamp(m_effects.weatherWetness, 0.0f, 1.0f);
    m_effects.weatherStorm = std::clamp(m_effects.weatherStorm, 0.0f, 1.0f);
    m_effects.skyWetness = std::clamp(m_effects.skyWetness, 0.0f, 1.0f);
    m_effects.fogWetness = std::clamp(m_effects.fogWetness, 0.0f, 1.0f);
    m_effects.cloudWetness = std::clamp(m_effects.cloudWetness, 0.0f, 1.0f);
}

bool PostProcessPass::beginSceneCapture(const Window& window) {
    return beginSceneCapture(window.getWidth(), window.getHeight());
}

bool PostProcessPass::beginSceneCapture(const int requestedWidth, const int requestedHeight) {
    m_sceneCaptured = false;

    const int width = requestedWidth;
    const int height = requestedHeight;
    if (m_postProcessShader == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    if (!ensureRenderTargets(width, height)) {
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
    glViewport(0, 0, width, height);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    m_sceneCaptured = true;
    return true;
}

void PostProcessPass::compositeToBackbuffer(RhiDevice& rhiDevice,
                                            const RhiTextureViewHandle swapchainColorView,
                                            const Window& window,
                                            const float frameTime,
                                            uint32_t gbufDepthTex,
                                            uint32_t weatherMaskTex) {
    const int width = std::max(1, window.getWidth());
    const int height = std::max(1, window.getHeight());

    if (!m_sceneCaptured || m_postProcessShader == nullptr || m_fullscreenVao == 0) {
        RhiCommandList& commandList = rhiDevice.beginFrame();
        bindBackbufferOutput(commandList, swapchainColorView, width, height, false);
        commandList.endRendering();
        rhiDevice.submitFrame(commandList);
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    const float resolvedExposure = updateAutoExposure(rhiDevice, frameTime);
    static_cast<void>(resolvedExposure);

    const bool hasBloom = renderBloom(rhiDevice, m_effects.bloomMipCount);

    static_cast<void>(weatherMaskTex);

    RhiCommandList& commandList = rhiDevice.beginFrame();
    bindBackbufferOutput(commandList, swapchainColorView, width, height, false);

    renderComposite(gbufDepthTex, weatherMaskTex, hasBloom);

    commandList.endRendering();
    rhiDevice.submitFrame(commandList);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

uint32_t PostProcessPass::compositeToTexture(RhiDevice& rhiDevice,
                                             const Window& window,
                                             const float frameTime,
                                             uint32_t gbufDepthTex,
                                             uint32_t weatherMaskTex) {
    static_cast<void>(window);
    const int width = std::max(1, m_targetWidth);
    const int height = std::max(1, m_targetHeight);

    if (!m_sceneCaptured || m_postProcessShader == nullptr || m_fullscreenVao == 0 ||
        !ensureCompositeTarget(rhiDevice, width, height)) {
        return 0;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    const float resolvedExposure = updateAutoExposure(rhiDevice, frameTime);
    static_cast<void>(resolvedExposure);

    const bool hasBloom = renderBloom(rhiDevice, m_effects.bloomMipCount);

    RhiCommandList& commandList = rhiDevice.beginFrame();
    bindCompositeOutput(commandList, width, height);
    renderComposite(gbufDepthTex, weatherMaskTex, hasBloom);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    return m_compositeTex;
}

void PostProcessPass::blitSceneCaptureToBackbuffer(RhiDevice& rhiDevice,
                                                   const RhiTextureViewHandle swapchainColorView,
                                                   const Window& window) {
    const int width = std::max(1, window.getWidth());
    const int height = std::max(1, window.getHeight());

    if (!m_sceneCaptured) {
        return;
    }
    if (!m_sceneColorHandle.isValid()) {
        MECRAFT_LOG_STREAM(std::cerr << "[PostProcessPass] Missing scene capture RHI texture\n");
        return;
    }

    if (!blitPostProcessTextureToSwapchain(rhiDevice, swapchainColorView, m_sceneColorHandle, width, height)) {
        MECRAFT_LOG_STREAM(std::cerr << "[PostProcessPass] Failed to blit scene capture through RHI\n");
    }
}

void PostProcessPass::blitCompositeToBackbuffer(RhiDevice& rhiDevice,
                                                const RhiTextureViewHandle swapchainColorView,
                                                const Window& window) {
    const int width = std::max(1, window.getWidth());
    const int height = std::max(1, window.getHeight());

    if (!m_compositeHandle.isValid()) {
        MECRAFT_LOG_STREAM(std::cerr << "[PostProcessPass] Missing composite RHI texture\n");
        return;
    }

    if (!blitPostProcessTextureToSwapchain(rhiDevice, swapchainColorView, m_compositeHandle, width, height)) {
        MECRAFT_LOG_STREAM(std::cerr << "[PostProcessPass] Failed to blit composite target through RHI\n");
    }
}

// --- Internal ---

float PostProcessPass::updateAutoExposure(RhiDevice& rhiDevice, const float frameTime) {
    const float manualExposure = 0.8f / std::max(m_effects.exposure, 0.0001f);
    if (!m_effects.autoExposureEnabled || m_exposureDownsampleShader == nullptr ||
        m_exposureResolveShader == nullptr || m_exposureMipCount <= 0 ||
        m_sceneColorTex == 0 || m_fullscreenVao == 0 ||
        m_exposureStateTex[0] == 0 || m_exposureStateTex[1] == 0 ||
        !ensureExposureTargetViews(rhiDevice)) {
        m_autoExposureInitialized = false;
        m_adaptedExposure = manualExposure;
        m_autoExposureSampleAccumulator = 0.0;
        return manualExposure;
    }

    if (!m_autoExposureInitialized) {
        m_adaptedExposure = manualExposure;
    }

    const float elapsedFrameTime = std::max(frameTime, 0.0f);

    m_autoExposureSampleAccumulator += elapsedFrameTime;
    const bool shouldSampleExposure =
        !m_autoExposureInitialized ||
        m_autoExposureSampleAccumulator >= kAutoExposureSampleIntervalSeconds;

    if (!m_autoExposureInitialized) {
        initializeExposureState(rhiDevice, manualExposure);
    }

    glBindVertexArray(m_fullscreenVao);
    RhiCommandList& commandList = rhiDevice.beginFrame();

    int finalMip = 0;
    if (shouldSampleExposure) {
        m_exposureDownsampleShader->use();

        GLuint sourceTex = m_sceneColorTex;
        const int exposureLod = std::min(kAutoExposureLod,
            std::max(0, static_cast<int>(std::floor(std::log2(static_cast<float>(
                std::max(m_targetWidth, m_targetHeight)))))));
        glm::ivec2 sourceSize(std::max(1, m_targetWidth >> exposureLod),
                              std::max(1, m_targetHeight >> exposureLod));
        bool sourceIsScene = true;
        for (int mip = 0; mip < m_exposureMipCount; ++mip) {
            if (m_exposureFbos[mip] == 0 || m_exposureTex[mip] == 0) {
                break;
            }
            finalMip = mip;
            beginPostProcessColorOutput(commandList, "ExposureDownsample", m_exposureView[mip],
                                        m_exposureMipSize[mip].x, m_exposureMipSize[mip].y, true);
            glUniform1i(kExposureDownsampleSourceIsSceneLocation, sourceIsScene ? 1 : 0);
            glUniform2f(kExposureDownsampleSourceSizeLocation,
                        static_cast<float>(sourceSize.x),
                        static_cast<float>(sourceSize.y));
            glUniform1i(kExposureDownsampleSourceLodLocation, sourceIsScene ? exposureLod : 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sourceTex);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            commandList.endRendering();

            sourceTex = m_exposureTex[mip];
            sourceSize = m_exposureMipSize[mip];
            sourceIsScene = false;
            if (m_exposureMipSize[mip].x == 1 && m_exposureMipSize[mip].y == 1) {
                break;
            }
        }
    }

    const int writeIndex = 1 - m_exposureStateReadIndex;

    m_exposureResolveShader->use();
    glUniform1f(kExposureResolveFrameTimeLocation, elapsedFrameTime);
    glUniform1f(kExposureResolveSpeedLocation, m_effects.autoExposureSpeed);
    glUniform1f(kExposureResolveBiasLocation, m_effects.autoExposureBias);
    glUniform1f(kExposureResolveManualLocation, manualExposure);
    glUniform1i(kExposureResolveInitializedLocation, m_autoExposureInitialized ? 1 : 0);
    glUniform1i(kExposureResolveReusePreviousTargetLocation, shouldSampleExposure ? 0 : 1);

    beginPostProcessColorOutput(commandList, "ExposureResolve", m_exposureStateView[writeIndex], 1, 1, false);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_exposureTex[finalMip]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_exposureStateTex[m_exposureStateReadIndex]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);

    m_exposureStateReadIndex = writeIndex;
    m_autoExposureInitialized = true;
    if (shouldSampleExposure) {
        m_autoExposureSampleAccumulator = 0.0;
    }

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);

    return m_adaptedExposure;
}

void PostProcessPass::initializeExposureState(RhiDevice& rhiDevice, const float manualExposure) {
    const float initialState[4] = {
        std::max(manualExposure, 0.001f),
        m_lastAverageLum,
        m_lastTargetExposure,
        1.0f
    };
    if (!ensureExposureTargetViews(rhiDevice)) {
        return;
    }

    RhiCommandList& commandList = rhiDevice.beginFrame();
    for (const RhiTextureViewHandle view : m_exposureStateView) {
        RhiColorAttachment colorAttachment;
        colorAttachment.view = view;
        colorAttachment.loadOp = RhiLoadOp::Clear;
        colorAttachment.storeOp = RhiStoreOp::Store;
        colorAttachment.clearColor[0] = initialState[0];
        colorAttachment.clearColor[1] = initialState[1];
        colorAttachment.clearColor[2] = initialState[2];
        colorAttachment.clearColor[3] = initialState[3];

        RhiRenderingInfo renderingInfo;
        renderingInfo.debugName = "ExposureStateInit";
        renderingInfo.renderArea = {0, 0, 1u, 1u};
        renderingInfo.colorAttachments = &colorAttachment;
        renderingInfo.colorAttachmentCount = 1u;
        commandList.beginRendering(renderingInfo);
        commandList.endRendering();
    }
    rhiDevice.submitFrame(commandList);
    m_exposureStateReadIndex = 0;
}

bool PostProcessPass::renderBloom(RhiDevice& rhiDevice, const int maxMipCount) {
    bool hasBloom = false;
    if (m_effects.bloomEnabled && m_bloomExtractShader != nullptr && m_bloomBlurShader != nullptr &&
        m_effects.bloomStrength > 0.001f &&
        m_bloomFbos[0][0] != 0 && m_bloomFbos[0][1] != 0 && m_bloomTex[0][0] != 0 && m_bloomTex[0][1] != 0 &&
        ensureBloomTargetViews(rhiDevice)) {
        const int mipCount = std::clamp(maxMipCount, 1, kBloomMipCount);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);

        RhiCommandList& commandList = rhiDevice.beginFrame();
        m_bloomExtractShader->use();
        glBindVertexArray(m_fullscreenVao);
        for (int mip = 0; mip < mipCount; ++mip) {
            if (m_bloomFbos[mip][0] == 0) {
                break;
            }
            beginPostProcessColorOutput(commandList, "BloomExtract", m_bloomView[mip][0],
                                        m_bloomMipSize[mip].x, m_bloomMipSize[mip].y, true);
            glUniform1i(kBloomExtractSourceLodLocation, mip + 1);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            commandList.endRendering();
        }

        m_bloomBlurShader->use();
        glUniform1f(kBloomBlurWeightLocation, 1.0f);
        for (int mip = 0; mip < mipCount; ++mip) {
            if (m_bloomFbos[mip][0] == 0 || m_bloomFbos[mip][1] == 0) {
                break;
            }

            beginPostProcessColorOutput(commandList, "BloomBlurHorizontal", m_bloomView[mip][1],
                                        m_bloomMipSize[mip].x, m_bloomMipSize[mip].y, true);
            m_bloomBlurShader->use();
            glUniform2f(kBloomBlurDirectionLocation, 1.0f, 0.0f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_bloomTex[mip][0]);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            commandList.endRendering();

            beginPostProcessColorOutput(commandList, "BloomBlurVertical", m_bloomView[mip][0],
                                        m_bloomMipSize[mip].x, m_bloomMipSize[mip].y, true);
            glUniform2f(kBloomBlurDirectionLocation, 0.0f, 1.0f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_bloomTex[mip][1]);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            commandList.endRendering();
        }
        rhiDevice.submitFrame(commandList);

        glUniform1f(kBloomBlurWeightLocation, 1.0f);

        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        hasBloom = true;
    }
    return hasBloom;
}

void PostProcessPass::renderComposite(uint32_t gbufDepthTex, uint32_t weatherMaskTex, const bool bloomReady) {
    m_postProcessShader->use();
    const bool useAutoExposureTexture =
        m_effects.autoExposureEnabled &&
        m_autoExposureInitialized &&
        m_exposureStateTex[m_exposureStateReadIndex] != 0;
    const bool hasBloom = bloomReady && m_effects.bloomEnabled && m_effects.bloomStrength > 0.001f && m_bloomFbos[0][0] != 0;
    updateCompositeParams(useAutoExposureTexture, hasBloom);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);

    static_cast<void>(weatherMaskTex);
    for (int mip = 0; mip < kBloomMipCount; ++mip) {
        glActiveTexture(GL_TEXTURE1 + mip);
        glBindTexture(GL_TEXTURE_2D, m_bloomTex[mip][0]);
    }
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(m_noiseTexture));
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, gbufDepthTex);
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, useAutoExposureTexture ? m_exposureStateTex[m_exposureStateReadIndex] : 0);
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, m_sceneDepthTex);

    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    // Cleanup texture bindings
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, 0);
    for (int mip = kBloomMipCount - 1; mip >= 0; --mip) {
        glActiveTexture(GL_TEXTURE1 + mip);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PostProcessPass::updateCompositeParams(const bool useAutoExposureTexture, const bool hasBloom) {
    assert(m_compositeParamsBuffer != 0);
    const bool sunRaysEnabled = m_effects.sunRaysEnabled && hasBloom;
    const float noiseDitherStrength = (m_effects.shaderpackGradingEnabled && m_noiseTexture.isValid())
        ? m_effects.noiseDitherStrength
        : 0.0f;

    const PostProcessCompositeParams params{
        glm::ivec4(hasBloom ? 1 : 0,
                   useAutoExposureTexture ? 1 : 0,
                   sunRaysEnabled ? 1 : 0,
                   m_effects.shaderpackGradingEnabled ? 1 : 0),
        glm::ivec4(std::clamp(m_effects.bloomMipCount, 1, kBloomMipCount),
                   m_effects.tonemapMode,
                   m_effects.postprocessDebugMode,
                   0),
        glm::ivec4(m_effects.underwaterEnabled ? 1 : 0,
                   m_effects.purkinjeShiftEnabled ? 1 : 0,
                   m_effects.bloomyFogEnabled ? 1 : 0,
                   0),
        glm::vec4(m_effects.bloomStrength,
                  m_adaptedExposure,
                  m_effects.gamma,
                  m_effects.screenRollRadians),
        glm::vec4(m_effects.sunScreenPos,
                  m_effects.sunVisibility,
                  m_effects.sunRayStrength),
        glm::vec4(m_effects.colorTemperature,
                  m_effects.vibrance,
                  m_effects.splitToneStrength,
                  m_effects.vignetteStrength),
        glm::vec4(noiseDitherStrength,
                  m_effects.sharpenStrength,
                  m_effects.saturation,
                  m_effects.contrast),
        glm::vec4(m_effects.underwaterTint,
                  m_effects.underwaterStrength),
        glm::vec4(m_effects.weatherWetness,
                  m_effects.weatherStorm,
                  m_effects.snowStrength,
                  m_effects.skyWetness),
        glm::vec4(m_effects.fogWetness,
                  m_effects.cloudWetness,
                  m_effects.cameraRainVisibility,
                  m_effects.weatherExposureBias),
        glm::vec4(m_effects.weatherPostRainFog,
                  m_effects.gameTime,
                  0.0f,
                  0.0f)
    };

    glNamedBufferSubData(m_compositeParamsBuffer,
                         0,
                         static_cast<GLsizeiptr>(sizeof(params)),
                         &params);
    glBindBufferBase(GL_UNIFORM_BUFFER, kCompositeParamsBinding, m_compositeParamsBuffer);
}

bool PostProcessPass::ensureRenderTargets(const int width, const int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (m_sceneFbo != 0 && m_targetWidth == width && m_targetHeight == height) {
        return true;
    }

    destroyRenderTargets();

    glCreateFramebuffers(1, &m_sceneFbo);

    glCreateTextures(GL_TEXTURE_2D, 1, &m_sceneColorTex);
    glTextureStorage2D(m_sceneColorTex, 1, GL_RGBA16F, width, height);
    glTextureParameteri(m_sceneColorTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_sceneColorTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_sceneColorTex, GL_TEXTURE_MAX_LEVEL, 0);
    glTextureParameteri(m_sceneColorTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_sceneColorTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_sceneFbo, GL_COLOR_ATTACHMENT0, m_sceneColorTex, 0);
    const GLenum sceneDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_sceneFbo, 1, &sceneDrawBuffer);

    glCreateTextures(GL_TEXTURE_2D, 1, &m_sceneDepthTex);
    glTextureStorage2D(m_sceneDepthTex, 1, GL_DEPTH_COMPONENT32F, width, height);
    glTextureParameteri(m_sceneDepthTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_sceneDepthTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_sceneDepthTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_sceneDepthTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_sceneFbo, GL_DEPTH_ATTACHMENT, m_sceneDepthTex, 0);

    const bool complete = glCheckNamedFramebufferStatus(m_sceneFbo, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    if (!complete) {
        destroyRenderTargets();
        return false;
    }

    m_sceneColorHandle = registerPostProcessTexture(
        m_sceneColorTex,
        RhiTextureFormat::Rgba16Float,
        width,
        height,
        rhiFlag(RhiTextureUsage::Sampled) |
            rhiFlag(RhiTextureUsage::ColorAttachment) |
            rhiFlag(RhiTextureUsage::TransferSrc) |
            rhiFlag(RhiTextureUsage::TransferDst));
    m_sceneDepthHandle = registerPostProcessTexture(
        m_sceneDepthTex,
        RhiTextureFormat::Depth32Float,
        width,
        height,
        rhiFlag(RhiTextureUsage::Sampled) |
            rhiFlag(RhiTextureUsage::DepthStencilAttachment) |
            rhiFlag(RhiTextureUsage::TransferSrc) |
            rhiFlag(RhiTextureUsage::TransferDst));
    if (!m_sceneColorHandle.isValid() || !m_sceneDepthHandle.isValid()) {
        destroyRenderTargets();
        return false;
    }

    for (int mip = 0; mip < kBloomMipCount; ++mip) {
        const int divisor = 1 << (mip + 1);
        m_bloomMipSize[mip] = glm::ivec2(std::max(1, width / divisor), std::max(1, height / divisor));
        for (int ping = 0; ping < 2; ++ping) {
            glCreateFramebuffers(1, &m_bloomFbos[mip][ping]);
            glCreateTextures(GL_TEXTURE_2D, 1, &m_bloomTex[mip][ping]);
            glTextureStorage2D(m_bloomTex[mip][ping], 1, GL_RGBA16F, m_bloomMipSize[mip].x, m_bloomMipSize[mip].y);
            glTextureParameteri(m_bloomTex[mip][ping], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(m_bloomTex[mip][ping], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(m_bloomTex[mip][ping], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(m_bloomTex[mip][ping], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glNamedFramebufferTexture(m_bloomFbos[mip][ping], GL_COLOR_ATTACHMENT0, m_bloomTex[mip][ping], 0);
            const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
            glNamedFramebufferDrawBuffers(m_bloomFbos[mip][ping], 1, &drawBuffer);
            if (glCheckNamedFramebufferStatus(m_bloomFbos[mip][ping], GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                destroyRenderTargets();
                return false;
            }
            m_bloomHandle[mip][ping] = registerPostProcessTexture(
                m_bloomTex[mip][ping],
                RhiTextureFormat::Rgba16Float,
                m_bloomMipSize[mip].x,
                m_bloomMipSize[mip].y,
                rhiFlag(RhiTextureUsage::Sampled) |
                    rhiFlag(RhiTextureUsage::ColorAttachment));
            if (!m_bloomHandle[mip][ping].isValid()) {
                destroyRenderTargets();
                return false;
            }
        }
    }

    const int exposureBaseLod = std::min(kAutoExposureLod,
        std::max(0, static_cast<int>(std::floor(std::log2(static_cast<float>(
            std::max(width, height)))))));
    const glm::ivec2 exposureBaseSize(std::max(1, width >> exposureBaseLod),
                                      std::max(1, height >> exposureBaseLod));
    glm::ivec2 exposureSize(std::max(1, exposureBaseSize.x / 2),
                            std::max(1, exposureBaseSize.y / 2));
    m_exposureMipCount = 0;
    for (int mip = 0; mip < kExposureMipCount; ++mip) {
        m_exposureMipSize[mip] = exposureSize;
        glCreateFramebuffers(1, &m_exposureFbos[mip]);
        glCreateTextures(GL_TEXTURE_2D, 1, &m_exposureTex[mip]);
        glTextureStorage2D(m_exposureTex[mip], 1, GL_RG16F, exposureSize.x, exposureSize.y);
        glTextureParameteri(m_exposureTex[mip], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_exposureTex[mip], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(m_exposureTex[mip], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_exposureTex[mip], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(m_exposureFbos[mip], GL_COLOR_ATTACHMENT0, m_exposureTex[mip], 0);
        const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
        glNamedFramebufferDrawBuffers(m_exposureFbos[mip], 1, &drawBuffer);
        if (glCheckNamedFramebufferStatus(m_exposureFbos[mip], GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            destroyRenderTargets();
            return false;
        }
        m_exposureHandle[mip] = registerPostProcessTexture(
            m_exposureTex[mip],
            RhiTextureFormat::Rg16Float,
            exposureSize.x,
            exposureSize.y,
            rhiFlag(RhiTextureUsage::Sampled) |
                rhiFlag(RhiTextureUsage::ColorAttachment));
        if (!m_exposureHandle[mip].isValid()) {
            destroyRenderTargets();
            return false;
        }
        ++m_exposureMipCount;
        if (exposureSize.x == 1 && exposureSize.y == 1) {
            break;
        }
        exposureSize = glm::ivec2(std::max(1, exposureSize.x / 2), std::max(1, exposureSize.y / 2));
    }

    for (int i = 0; i < 2; ++i) {
        glCreateFramebuffers(1, &m_exposureStateFbos[i]);
        glCreateTextures(GL_TEXTURE_2D, 1, &m_exposureStateTex[i]);
        glTextureStorage2D(m_exposureStateTex[i], 1, GL_RGBA16F, 1, 1);
        glTextureParameteri(m_exposureStateTex[i], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_exposureStateTex[i], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(m_exposureStateTex[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_exposureStateTex[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(m_exposureStateFbos[i], GL_COLOR_ATTACHMENT0, m_exposureStateTex[i], 0);
        const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
        glNamedFramebufferDrawBuffers(m_exposureStateFbos[i], 1, &drawBuffer);
        if (glCheckNamedFramebufferStatus(m_exposureStateFbos[i], GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            destroyRenderTargets();
            return false;
        }
        m_exposureStateHandle[i] = registerPostProcessTexture(
            m_exposureStateTex[i],
            RhiTextureFormat::Rgba16Float,
            1,
            1,
            rhiFlag(RhiTextureUsage::Sampled) |
                rhiFlag(RhiTextureUsage::ColorAttachment));
        if (!m_exposureStateHandle[i].isValid()) {
            destroyRenderTargets();
            return false;
        }
    }

    m_targetWidth = width;
    m_targetHeight = height;
    m_autoExposureInitialized = false;
    m_autoExposureSampleAccumulator = 0.0;
    return true;
}

bool PostProcessPass::ensureSceneCaptureViews(RhiDevice& rhiDevice) {
    if (m_sceneColorView.isValid() && m_sceneDepthView.isValid() &&
        m_sceneCaptureViewDevice == &rhiDevice) {
        return true;
    }

    if (m_sceneCaptureViewDevice != nullptr) {
        if (m_sceneColorView.isValid()) {
            m_sceneCaptureViewDevice->destroyTextureView(m_sceneColorView);
        }
        if (m_sceneDepthView.isValid()) {
            m_sceneCaptureViewDevice->destroyTextureView(m_sceneDepthView);
        }
    }
    m_sceneColorView = {};
    m_sceneDepthView = {};
    m_sceneCaptureViewDevice = nullptr;

    if (!m_sceneColorHandle.isValid() || !m_sceneDepthHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc colorDesc;
    colorDesc.texture = m_sceneColorHandle;
    colorDesc.viewType = RhiTextureViewType::Texture2D;
    colorDesc.format = RhiTextureFormat::Rgba16Float;
    colorDesc.baseMip = 0;
    colorDesc.mipCount = 1;
    colorDesc.baseLayer = 0;
    colorDesc.layerCount = 1;
    m_sceneColorView = rhiDevice.createTextureView(colorDesc);
    if (!m_sceneColorView.isValid()) {
        return false;
    }

    RhiTextureViewDesc depthDesc;
    depthDesc.texture = m_sceneDepthHandle;
    depthDesc.viewType = RhiTextureViewType::Texture2D;
    depthDesc.format = RhiTextureFormat::Depth32Float;
    depthDesc.baseMip = 0;
    depthDesc.mipCount = 1;
    depthDesc.baseLayer = 0;
    depthDesc.layerCount = 1;
    m_sceneDepthView = rhiDevice.createTextureView(depthDesc);
    if (!m_sceneDepthView.isValid()) {
        rhiDevice.destroyTextureView(m_sceneColorView);
        m_sceneColorView = {};
        return false;
    }

    m_sceneCaptureViewDevice = &rhiDevice;
    return true;
}

bool PostProcessPass::ensureCompositeTarget(RhiDevice& rhiDevice, const int width, const int height) {
    const int targetWidth = std::max(1, width);
    const int targetHeight = std::max(1, height);
    if (m_compositeTex != 0 && m_compositeHandle.isValid() && m_compositeView.isValid() &&
        m_compositeViewDevice == &rhiDevice &&
        m_targetWidth == targetWidth && m_targetHeight == targetHeight) {
        return true;
    }
    if (m_compositeViewDevice != nullptr && m_compositeView.isValid()) {
        m_compositeViewDevice->destroyTextureView(m_compositeView);
    }
    m_compositeView = {};
    m_compositeViewDevice = nullptr;
    renderer::rhi::gl::unregisterTextureAndReset(m_compositeHandle);
    if (m_compositeTex != 0) {
        glDeleteTextures(1, &m_compositeTex);
        m_compositeTex = 0;
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &m_compositeTex);
    glTextureStorage2D(m_compositeTex, 1, GL_RGBA8, targetWidth, targetHeight);
    glTextureParameteri(m_compositeTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_compositeTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_compositeTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_compositeTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_compositeHandle = registerPostProcessTexture(
        m_compositeTex,
        RhiTextureFormat::Rgba8Unorm,
        targetWidth,
        targetHeight,
        rhiFlag(RhiTextureUsage::Sampled) |
            rhiFlag(RhiTextureUsage::ColorAttachment) |
            rhiFlag(RhiTextureUsage::TransferSrc));
    if (!m_compositeHandle.isValid()) {
        if (m_compositeTex != 0) {
            glDeleteTextures(1, &m_compositeTex);
            m_compositeTex = 0;
        }
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = m_compositeHandle;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = RhiTextureFormat::Rgba8Unorm;
    viewDesc.baseMip = 0;
    viewDesc.mipCount = 1;
    viewDesc.baseLayer = 0;
    viewDesc.layerCount = 1;
    m_compositeView = rhiDevice.createTextureView(viewDesc);
    if (!m_compositeView.isValid()) {
        renderer::rhi::gl::unregisterTextureAndReset(m_compositeHandle);
        if (m_compositeTex != 0) {
            glDeleteTextures(1, &m_compositeTex);
            m_compositeTex = 0;
        }
        return false;
    }
    m_compositeViewDevice = &rhiDevice;
    return true;
}

bool PostProcessPass::ensureBloomTargetViews(RhiDevice& rhiDevice) {
    if (m_bloomViewDevice != nullptr && m_bloomViewDevice != &rhiDevice) {
        for (int mip = 0; mip < kBloomMipCount; ++mip) {
            for (int ping = 0; ping < 2; ++ping) {
                if (m_bloomView[mip][ping].isValid()) {
                    m_bloomViewDevice->destroyTextureView(m_bloomView[mip][ping]);
                    m_bloomView[mip][ping] = {};
                }
            }
        }
        m_bloomViewDevice = nullptr;
    }

    m_bloomViewDevice = &rhiDevice;
    for (int mip = 0; mip < kBloomMipCount; ++mip) {
        for (int ping = 0; ping < 2; ++ping) {
            if (!m_bloomHandle[mip][ping].isValid()) {
                return false;
            }
            if (m_bloomView[mip][ping].isValid()) {
                continue;
            }

            RhiTextureViewDesc viewDesc;
            viewDesc.texture = m_bloomHandle[mip][ping];
            viewDesc.viewType = RhiTextureViewType::Texture2D;
            viewDesc.format = RhiTextureFormat::Rgba16Float;
            viewDesc.baseMip = 0;
            viewDesc.mipCount = 1;
            viewDesc.baseLayer = 0;
            viewDesc.layerCount = 1;

            m_bloomView[mip][ping] = rhiDevice.createTextureView(viewDesc);
            if (!m_bloomView[mip][ping].isValid()) {
                return false;
            }
        }
    }

    m_bloomViewDevice = &rhiDevice;
    return true;
}

bool PostProcessPass::ensureExposureTargetViews(RhiDevice& rhiDevice) {
    if (m_exposureViewDevice != nullptr && m_exposureViewDevice != &rhiDevice) {
        for (int mip = 0; mip < kExposureMipCount; ++mip) {
            if (m_exposureView[mip].isValid()) {
                m_exposureViewDevice->destroyTextureView(m_exposureView[mip]);
                m_exposureView[mip] = {};
            }
        }
        for (RhiTextureViewHandle& view : m_exposureStateView) {
            if (view.isValid()) {
                m_exposureViewDevice->destroyTextureView(view);
                view = {};
            }
        }
        m_exposureViewDevice = nullptr;
    }

    m_exposureViewDevice = &rhiDevice;
    for (int mip = 0; mip < m_exposureMipCount; ++mip) {
        if (!m_exposureHandle[mip].isValid()) {
            return false;
        }
        if (m_exposureView[mip].isValid()) {
            continue;
        }

        RhiTextureViewDesc viewDesc;
        viewDesc.texture = m_exposureHandle[mip];
        viewDesc.viewType = RhiTextureViewType::Texture2D;
        viewDesc.format = RhiTextureFormat::Rg16Float;
        viewDesc.baseMip = 0;
        viewDesc.mipCount = 1;
        viewDesc.baseLayer = 0;
        viewDesc.layerCount = 1;
        m_exposureView[mip] = rhiDevice.createTextureView(viewDesc);
        if (!m_exposureView[mip].isValid()) {
            return false;
        }
    }

    for (int i = 0; i < 2; ++i) {
        if (!m_exposureStateHandle[i].isValid()) {
            return false;
        }
        if (m_exposureStateView[i].isValid()) {
            continue;
        }

        RhiTextureViewDesc viewDesc;
        viewDesc.texture = m_exposureStateHandle[i];
        viewDesc.viewType = RhiTextureViewType::Texture2D;
        viewDesc.format = RhiTextureFormat::Rgba16Float;
        viewDesc.baseMip = 0;
        viewDesc.mipCount = 1;
        viewDesc.baseLayer = 0;
        viewDesc.layerCount = 1;
        m_exposureStateView[i] = rhiDevice.createTextureView(viewDesc);
        if (!m_exposureStateView[i].isValid()) {
            return false;
        }
    }

    return true;
}

void PostProcessPass::bindCompositeOutput(RhiCommandList& commandList, const int width, const int height) {
    m_renderCompositeToTexture = true;
    RhiColorAttachment colorAttachment;
    colorAttachment.view = m_compositeView;
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "PostProcessCompositeTexture";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, width)),
        static_cast<uint32_t>(std::max(1, height))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    commandList.beginRendering(renderingInfo);
}

void PostProcessPass::bindBackbufferOutput(RhiCommandList& commandList,
                                           const RhiTextureViewHandle swapchainColorView,
                                           const int width,
                                           const int height,
                                           const bool clearColor) {
    m_renderCompositeToTexture = false;
    RhiColorAttachment colorAttachment;
    colorAttachment.view = swapchainColorView;
    colorAttachment.loadOp = clearColor ? RhiLoadOp::Clear : RhiLoadOp::Load;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "PostProcessBackbuffer";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, width)),
        static_cast<uint32_t>(std::max(1, height))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1;
    commandList.beginRendering(renderingInfo);
}

void PostProcessPass::destroyRenderTargets() {
    if (m_sceneCaptureViewDevice != nullptr) {
        if (m_sceneColorView.isValid()) {
            m_sceneCaptureViewDevice->destroyTextureView(m_sceneColorView);
        }
        if (m_sceneDepthView.isValid()) {
            m_sceneCaptureViewDevice->destroyTextureView(m_sceneDepthView);
        }
    }
    m_sceneColorView = {};
    m_sceneDepthView = {};
    m_sceneCaptureViewDevice = nullptr;
    if (m_compositeViewDevice != nullptr && m_compositeView.isValid()) {
        m_compositeViewDevice->destroyTextureView(m_compositeView);
    }
    m_compositeView = {};
    m_compositeViewDevice = nullptr;
    if (m_bloomViewDevice != nullptr) {
        for (int mip = 0; mip < kBloomMipCount; ++mip) {
            for (int ping = 0; ping < 2; ++ping) {
                if (m_bloomView[mip][ping].isValid()) {
                    m_bloomViewDevice->destroyTextureView(m_bloomView[mip][ping]);
                }
                m_bloomView[mip][ping] = {};
            }
        }
    }
    m_bloomViewDevice = nullptr;
    if (m_exposureViewDevice != nullptr) {
        for (int mip = 0; mip < kExposureMipCount; ++mip) {
            if (m_exposureView[mip].isValid()) {
                m_exposureViewDevice->destroyTextureView(m_exposureView[mip]);
            }
            m_exposureView[mip] = {};
        }
        for (RhiTextureViewHandle& view : m_exposureStateView) {
            if (view.isValid()) {
                m_exposureViewDevice->destroyTextureView(view);
            }
            view = {};
        }
    }
    m_exposureViewDevice = nullptr;
    renderer::rhi::gl::unregisterTextureAndReset(m_compositeHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_sceneColorHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_sceneDepthHandle);
    if (m_compositeTex != 0) {
        glDeleteTextures(1, &m_compositeTex);
        m_compositeTex = 0;
    }
    if (m_sceneDepthTex != 0) {
        glDeleteTextures(1, &m_sceneDepthTex);
        m_sceneDepthTex = 0;
    }
    if (m_sceneColorTex != 0) {
        glDeleteTextures(1, &m_sceneColorTex);
        m_sceneColorTex = 0;
    }
    if (m_sceneFbo != 0) {
        glDeleteFramebuffers(1, &m_sceneFbo);
        m_sceneFbo = 0;
    }
    for (int mip = 0; mip < kBloomMipCount; ++mip) {
        m_bloomMipSize[mip] = glm::ivec2(0);
        for (int ping = 0; ping < 2; ++ping) {
            renderer::rhi::gl::unregisterTextureAndReset(m_bloomHandle[mip][ping]);
            if (m_bloomTex[mip][ping] != 0) {
                glDeleteTextures(1, &m_bloomTex[mip][ping]);
                m_bloomTex[mip][ping] = 0;
            }
            if (m_bloomFbos[mip][ping] != 0) {
                glDeleteFramebuffers(1, &m_bloomFbos[mip][ping]);
                m_bloomFbos[mip][ping] = 0;
            }
        }
    }
    for (int mip = 0; mip < kExposureMipCount; ++mip) {
        m_exposureMipSize[mip] = glm::ivec2(0);
        renderer::rhi::gl::unregisterTextureAndReset(m_exposureHandle[mip]);
        if (m_exposureTex[mip] != 0) {
            glDeleteTextures(1, &m_exposureTex[mip]);
            m_exposureTex[mip] = 0;
        }
        if (m_exposureFbos[mip] != 0) {
            glDeleteFramebuffers(1, &m_exposureFbos[mip]);
            m_exposureFbos[mip] = 0;
        }
    }
    for (int i = 0; i < 2; ++i) {
        renderer::rhi::gl::unregisterTextureAndReset(m_exposureStateHandle[i]);
        if (m_exposureStateTex[i] != 0) {
            glDeleteTextures(1, &m_exposureStateTex[i]);
            m_exposureStateTex[i] = 0;
        }
        if (m_exposureStateFbos[i] != 0) {
            glDeleteFramebuffers(1, &m_exposureStateFbos[i]);
            m_exposureStateFbos[i] = 0;
        }
    }
    m_exposureMipCount = 0;
    m_exposureStateReadIndex = 0;
    m_autoExposureInitialized = false;
    m_autoExposureSampleAccumulator = 0.0;
}

void PostProcessPass::initFullscreenTriangle() {
    if (m_fullscreenVao != 0) {
        return;
    }
    glGenVertexArrays(1, &m_fullscreenVao);
}

void PostProcessPass::destroyFullscreenTriangle() {
    if (m_fullscreenVao != 0) {
        glDeleteVertexArrays(1, &m_fullscreenVao);
        m_fullscreenVao = 0;
    }
}
