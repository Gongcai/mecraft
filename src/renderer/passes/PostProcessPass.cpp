#include "PostProcessPass.h"
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
    m_blitShader = resourceMgr.getShader("blit_texture");
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
    m_blitShader = nullptr;
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

void PostProcessPass::beginSceneCapture(const Window& window) {
    beginSceneCapture(window.getWidth(), window.getHeight());
}

void PostProcessPass::beginSceneCapture(const int requestedWidth, const int requestedHeight) {
    m_sceneCaptured = false;

    const int width = requestedWidth;
    const int height = requestedHeight;
    if (m_postProcessShader == nullptr || width <= 0 || height <= 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, std::max(1, width), std::max(1, height));
        return;
    }

    if (!ensureRenderTargets(width, height)) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
    glViewport(0, 0, width, height);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    m_sceneCaptured = true;
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

    const float resolvedExposure = updateAutoExposure(frameTime);
    static_cast<void>(resolvedExposure);

    const bool hasBloom = renderBloom(m_effects.bloomMipCount);

    static_cast<void>(weatherMaskTex);

    RhiCommandList& commandList = rhiDevice.beginFrame();
    bindBackbufferOutput(commandList, swapchainColorView, width, height, false);

    renderComposite(gbufDepthTex, weatherMaskTex, hasBloom);

    commandList.endRendering();
    rhiDevice.submitFrame(commandList);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

uint32_t PostProcessPass::compositeToTexture(const Window& window, const float frameTime,
                                             uint32_t gbufDepthTex,
                                             uint32_t weatherMaskTex) {
    static_cast<void>(window);
    const int width = std::max(1, m_targetWidth);
    const int height = std::max(1, m_targetHeight);

    if (!m_sceneCaptured || m_postProcessShader == nullptr || m_fullscreenVao == 0 ||
        !ensureCompositeTarget(width, height)) {
        return 0;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    const float resolvedExposure = updateAutoExposure(frameTime);
    static_cast<void>(resolvedExposure);

    const bool hasBloom = renderBloom(m_effects.bloomMipCount);

    bindCompositeOutput(width, height);
    renderComposite(gbufDepthTex, weatherMaskTex, hasBloom);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    return m_compositeTex;
}

void PostProcessPass::blitSceneCaptureToBackbuffer(RhiDevice& rhiDevice,
                                                   const RhiTextureViewHandle swapchainColorView,
                                                   const Window& window) {
    const int width = std::max(1, window.getWidth());
    const int height = std::max(1, window.getHeight());

    if (!m_sceneCaptured || m_blitShader == nullptr || m_fullscreenVao == 0) {
        RhiCommandList& commandList = rhiDevice.beginFrame();
        bindBackbufferOutput(commandList, swapchainColorView, width, height, false);
        commandList.endRendering();
        rhiDevice.submitFrame(commandList);
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    RhiCommandList& commandList = rhiDevice.beginFrame();
    bindBackbufferOutput(commandList, swapchainColorView, width, height, true);

    m_blitShader->use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);

    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    commandList.endRendering();
    rhiDevice.submitFrame(commandList);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void PostProcessPass::blitTextureToBackbuffer(RhiDevice& rhiDevice,
                                              const RhiTextureViewHandle swapchainColorView,
                                              const uint32_t texture,
                                              const Window& window) {
    const int width = std::max(1, window.getWidth());
    const int height = std::max(1, window.getHeight());

    if (texture == 0 || m_blitShader == nullptr || m_fullscreenVao == 0) {
        RhiCommandList& commandList = rhiDevice.beginFrame();
        bindBackbufferOutput(commandList, swapchainColorView, width, height, false);
        commandList.endRendering();
        rhiDevice.submitFrame(commandList);
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    RhiCommandList& commandList = rhiDevice.beginFrame();
    bindBackbufferOutput(commandList, swapchainColorView, width, height, true);

    m_blitShader->use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    commandList.endRendering();
    rhiDevice.submitFrame(commandList);

    glBindTexture(GL_TEXTURE_2D, 0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

// --- Internal ---

float PostProcessPass::updateAutoExposure(const float frameTime) {
    const float manualExposure = 0.8f / std::max(m_effects.exposure, 0.0001f);
    if (!m_effects.autoExposureEnabled || m_exposureDownsampleShader == nullptr ||
        m_exposureResolveShader == nullptr || m_exposureMipCount <= 0 ||
        m_sceneColorTex == 0 || m_fullscreenVao == 0 ||
        m_exposureStateTex[0] == 0 || m_exposureStateTex[1] == 0) {
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
        initializeExposureState(manualExposure);
    }

    glBindVertexArray(m_fullscreenVao);

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
            glBindFramebuffer(GL_FRAMEBUFFER, m_exposureFbos[mip]);
            glViewport(0, 0, m_exposureMipSize[mip].x, m_exposureMipSize[mip].y);
            glClear(GL_COLOR_BUFFER_BIT);
            glUniform1i(kExposureDownsampleSourceIsSceneLocation, sourceIsScene ? 1 : 0);
            glUniform2f(kExposureDownsampleSourceSizeLocation,
                        static_cast<float>(sourceSize.x),
                        static_cast<float>(sourceSize.y));
            glUniform1i(kExposureDownsampleSourceLodLocation, sourceIsScene ? exposureLod : 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sourceTex);
            glDrawArrays(GL_TRIANGLES, 0, 3);

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

    glBindFramebuffer(GL_FRAMEBUFFER, m_exposureStateFbos[writeIndex]);
    glViewport(0, 0, 1, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_exposureTex[finalMip]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_exposureStateTex[m_exposureStateReadIndex]);
    glDrawArrays(GL_TRIANGLES, 0, 3);

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

void PostProcessPass::initializeExposureState(const float manualExposure) {
    const float initialState[4] = {
        std::max(manualExposure, 0.001f),
        m_lastAverageLum,
        m_lastTargetExposure,
        1.0f
    };
    for (GLuint texture : m_exposureStateTex) {
        glClearTexImage(texture, 0, GL_RGBA, GL_FLOAT, initialState);
    }
    m_exposureStateReadIndex = 0;
}

bool PostProcessPass::renderBloom(const int maxMipCount) {
    bool hasBloom = false;
    if (m_effects.bloomEnabled && m_bloomExtractShader != nullptr && m_bloomBlurShader != nullptr &&
        m_effects.bloomStrength > 0.001f &&
        m_bloomFbos[0][0] != 0 && m_bloomFbos[0][1] != 0 && m_bloomTex[0][0] != 0 && m_bloomTex[0][1] != 0) {
        const int mipCount = std::clamp(maxMipCount, 1, kBloomMipCount);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);

        m_bloomExtractShader->use();
        glBindVertexArray(m_fullscreenVao);
        for (int mip = 0; mip < mipCount; ++mip) {
            if (m_bloomFbos[mip][0] == 0) {
                break;
            }
            glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbos[mip][0]);
            glViewport(0, 0, m_bloomMipSize[mip].x, m_bloomMipSize[mip].y);
            glClear(GL_COLOR_BUFFER_BIT);
            glUniform1i(kBloomExtractSourceLodLocation, mip + 1);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        m_bloomBlurShader->use();
        glUniform1f(kBloomBlurWeightLocation, 1.0f);
        for (int mip = 0; mip < mipCount; ++mip) {
            if (m_bloomFbos[mip][0] == 0 || m_bloomFbos[mip][1] == 0) {
                break;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbos[mip][1]);
            glViewport(0, 0, m_bloomMipSize[mip].x, m_bloomMipSize[mip].y);
            glClear(GL_COLOR_BUFFER_BIT);
            m_bloomBlurShader->use();
            glUniform2f(kBloomBlurDirectionLocation, 1.0f, 0.0f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_bloomTex[mip][0]);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbos[mip][0]);
            glViewport(0, 0, m_bloomMipSize[mip].x, m_bloomMipSize[mip].y);
            glClear(GL_COLOR_BUFFER_BIT);
            glUniform2f(kBloomBlurDirectionLocation, 0.0f, 1.0f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_bloomTex[mip][1]);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

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
    }

    m_targetWidth = width;
    m_targetHeight = height;
    m_autoExposureInitialized = false;
    m_autoExposureSampleAccumulator = 0.0;
    return true;
}

bool PostProcessPass::ensureCompositeTarget(const int width, const int height) {
    const int targetWidth = std::max(1, width);
    const int targetHeight = std::max(1, height);
    if (m_compositeFbo != 0 && m_compositeTex != 0 &&
        m_targetWidth == targetWidth && m_targetHeight == targetHeight) {
        return true;
    }
    if (m_compositeTex != 0) {
        glDeleteTextures(1, &m_compositeTex);
        m_compositeTex = 0;
    }
    if (m_compositeFbo != 0) {
        glDeleteFramebuffers(1, &m_compositeFbo);
        m_compositeFbo = 0;
    }

    glCreateFramebuffers(1, &m_compositeFbo);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_compositeTex);
    glTextureStorage2D(m_compositeTex, 1, GL_RGBA8, targetWidth, targetHeight);
    glTextureParameteri(m_compositeTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_compositeTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_compositeTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_compositeTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_compositeFbo, GL_COLOR_ATTACHMENT0, m_compositeTex, 0);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_compositeFbo, 1, &drawBuffer);
    if (glCheckNamedFramebufferStatus(m_compositeFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        if (m_compositeTex != 0) {
            glDeleteTextures(1, &m_compositeTex);
            m_compositeTex = 0;
        }
        if (m_compositeFbo != 0) {
            glDeleteFramebuffers(1, &m_compositeFbo);
            m_compositeFbo = 0;
        }
        return false;
    }
    return true;
}

void PostProcessPass::bindCompositeOutput(const int width, const int height) {
    m_renderCompositeToTexture = true;
    glBindFramebuffer(GL_FRAMEBUFFER, m_compositeFbo);
    glViewport(0, 0, std::max(1, width), std::max(1, height));
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
    if (m_compositeTex != 0) {
        glDeleteTextures(1, &m_compositeTex);
        m_compositeTex = 0;
    }
    if (m_compositeFbo != 0) {
        glDeleteFramebuffers(1, &m_compositeFbo);
        m_compositeFbo = 0;
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
