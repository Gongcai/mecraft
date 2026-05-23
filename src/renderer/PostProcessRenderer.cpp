#include "PostProcessRenderer.h"

#include "Shader.h"
#include "../core/Window.h"
#include "../resource/ResourceMgr.h"

#include <algorithm>
#include <cmath>
#include <glm/vec2.hpp>

PostProcessRenderer::~PostProcessRenderer() {
    shutdown();
}

void PostProcessRenderer::init(ResourceMgr& resourceMgr) {
    m_postProcessShader = resourceMgr.getShader("postprocess");
    m_bloomExtractShader = resourceMgr.getShader("bloom_extract");
    m_bloomBlurShader = resourceMgr.getShader("bloom_blur");
    m_exposureDownsampleShader = resourceMgr.getShader("exposure_downsample");
    m_blitShader = resourceMgr.getShader("blit_texture");
    m_noiseTexture = resourceMgr.getTexture2D("shader_bayer256");
    if (m_noiseTexture == 0) {
        m_noiseTexture = resourceMgr.getTexture2D("shader_noise2d");
    }
    initFullscreenTriangle();
}

void PostProcessRenderer::shutdown() {
    destroyRenderTargets();
    destroyFullscreenTriangle();
    m_postProcessShader = nullptr;
    m_bloomExtractShader = nullptr;
    m_bloomBlurShader = nullptr;
    m_exposureDownsampleShader = nullptr;
    m_blitShader = nullptr;
    m_noiseTexture = 0;
    m_sceneCaptured = false;
    m_targetWidth = 0;
    m_targetHeight = 0;
}

void PostProcessRenderer::setEffects(const PostProcessEffects& effects) {
    m_effects = effects;
    m_effects.underwaterStrength = std::clamp(m_effects.underwaterStrength, 0.0f, 1.0f);
    m_effects.bloomThreshold = std::clamp(m_effects.bloomThreshold, 0.0f, 4.0f);
    m_effects.bloomStrength = std::clamp(m_effects.bloomStrength, 0.0f, 2.0f);
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
    m_effects.kappaGradingStrength = std::clamp(m_effects.kappaGradingStrength, 0.0f, 1.0f);
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

void PostProcessRenderer::beginScene(const Window& window) {
    m_sceneCaptured = false;

    const int width = window.getWidth();
    const int height = window.getHeight();
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

void PostProcessRenderer::endSceneAndComposite(const Window& window, const float frameTime,
                                                GLuint gbufDepthTex,
                                                GLuint weatherMaskTex) {
    const int width = std::max(1, window.getWidth());
    const int height = std::max(1, window.getHeight());

    if (!m_sceneCaptured || m_postProcessShader == nullptr || m_fullscreenVao == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);
    glGenerateMipmap(GL_TEXTURE_2D);

    const float resolvedExposure = updateAutoExposure(frameTime);

    bool hasBloom = false;
    if (m_effects.bloomEnabled && m_bloomExtractShader != nullptr && m_bloomBlurShader != nullptr &&
        m_bloomFbos[0][0] != 0 && m_bloomFbos[0][1] != 0 && m_bloomTex[0][0] != 0 && m_bloomTex[0][1] != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);

        m_bloomExtractShader->use();
        m_bloomExtractShader->setInt("uSceneTex", 0);
        m_bloomExtractShader->setFloat("uExposure", resolvedExposure);
        glBindVertexArray(m_fullscreenVao);
        for (int mip = 0; mip < kBloomMipCount; ++mip) {
            if (m_bloomFbos[mip][0] == 0) {
                break;
            }
            glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbos[mip][0]);
            glViewport(0, 0, m_bloomMipSize[mip].x, m_bloomMipSize[mip].y);
            glClear(GL_COLOR_BUFFER_BIT);
            m_bloomExtractShader->setInt("uSourceLod", mip);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        m_bloomBlurShader->use();
        m_bloomBlurShader->setInt("uImage", 0);
        m_bloomBlurShader->setFloat("uWeight", 1.0f);
        for (int mip = 0; mip < kBloomMipCount; ++mip) {
            if (m_bloomFbos[mip][0] == 0 || m_bloomFbos[mip][1] == 0) {
                break;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbos[mip][1]);
            glViewport(0, 0, m_bloomMipSize[mip].x, m_bloomMipSize[mip].y);
            glClear(GL_COLOR_BUFFER_BIT);
            m_bloomBlurShader->use();
            m_bloomBlurShader->setInt("uImage", 0);
            m_bloomBlurShader->setVec2("uDirection", glm::vec2(1.0f, 0.0f));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_bloomTex[mip][0]);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbos[mip][0]);
            glViewport(0, 0, m_bloomMipSize[mip].x, m_bloomMipSize[mip].y);
            glClear(GL_COLOR_BUFFER_BIT);
            m_bloomBlurShader->setVec2("uDirection", glm::vec2(0.0f, 1.0f));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_bloomTex[mip][1]);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        m_bloomBlurShader->setFloat("uWeight", 1.0f);

        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        hasBloom = true;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);

    m_postProcessShader->use();
    m_postProcessShader->setInt("uSceneTex", 0);
    m_postProcessShader->setInt("uBloomTex", 1);
    m_postProcessShader->setInt("uBloomMip0", 1);
    m_postProcessShader->setInt("uBloomMip1", 2);
    m_postProcessShader->setInt("uBloomMip2", 3);
    m_postProcessShader->setInt("uBloomMip3", 4);
    m_postProcessShader->setInt("uBloomMip4", 5);
    m_postProcessShader->setInt("uBloomMip5", 6);
    m_postProcessShader->setInt("uBloomMip6", 7);
    m_postProcessShader->setInt("uNoiseTex", 8);
    m_postProcessShader->setBool("uBloomEnabled", hasBloom);
    m_postProcessShader->setFloat("uBloomStrength", m_effects.bloomStrength);
    m_postProcessShader->setBool("uSunRaysEnabled", m_effects.sunRaysEnabled && hasBloom);
    m_postProcessShader->setVec2("uSunScreenPos", m_effects.sunScreenPos);
    m_postProcessShader->setFloat("uSunVisibility", m_effects.sunVisibility);
    m_postProcessShader->setFloat("uSunRayStrength", m_effects.sunRayStrength);
    m_postProcessShader->setBool("uShaderpackGradingEnabled", m_effects.shaderpackGradingEnabled);
    m_postProcessShader->setInt("uTonemapMode", m_effects.tonemapMode);
    m_postProcessShader->setFloat("uColorTemperature", m_effects.colorTemperature);
    m_postProcessShader->setFloat("uVibrance", m_effects.vibrance);
    m_postProcessShader->setFloat("uKappaGradingStrength", m_effects.kappaGradingStrength);
    m_postProcessShader->setFloat("uHighlightCompression", m_effects.highlightCompression);
    m_postProcessShader->setFloat("uFilmEmulationStrength", m_effects.filmEmulationStrength);
    m_postProcessShader->setFloat("uRedModifierStrength", m_effects.redModifierStrength);
    m_postProcessShader->setVec3("uColorLuma", m_effects.colorLuma);
    m_postProcessShader->setFloat("uSplitToneStrength", m_effects.splitToneStrength);
    m_postProcessShader->setFloat("uVignetteStrength", m_effects.vignetteStrength);
    m_postProcessShader->setFloat("uSharpenStrength", m_effects.sharpenStrength);
    const float noiseDitherStrength = (m_effects.shaderpackGradingEnabled && m_noiseTexture != 0)
        ? m_effects.noiseDitherStrength
        : 0.0f;
    m_postProcessShader->setFloat("uNoiseDitherStrength", noiseDitherStrength);
    m_postProcessShader->setBool("uUnderwaterEnabled", m_effects.underwaterEnabled);
    m_postProcessShader->setVec3("uUnderwaterTint", m_effects.underwaterTint);
    m_postProcessShader->setFloat("uUnderwaterStrength", m_effects.underwaterStrength);
    m_postProcessShader->setFloat("uScreenRollRadians", m_effects.screenRollRadians);
    m_postProcessShader->setFloat("uExposure", resolvedExposure);
    m_postProcessShader->setFloat("uGamma", m_effects.gamma);
    m_postProcessShader->setFloat("uSaturation", m_effects.saturation);
    m_postProcessShader->setFloat("uContrast", m_effects.contrast);
    m_postProcessShader->setBool("uPurkinjeShiftEnabled", m_effects.purkinjeShiftEnabled);
    m_postProcessShader->setBool("uBloomyFogEnabled", m_effects.bloomyFogEnabled);
    m_postProcessShader->setFloat("uWeatherWetness", m_effects.weatherWetness);
    m_postProcessShader->setFloat("uWeatherStorm", m_effects.weatherStorm);
    m_postProcessShader->setFloat("uSnowStrength", m_effects.snowStrength);
    m_postProcessShader->setFloat("uSkyWetness", m_effects.skyWetness);
    m_postProcessShader->setFloat("uFogWetness", m_effects.fogWetness);
    m_postProcessShader->setFloat("uCloudWetness", m_effects.cloudWetness);
    m_postProcessShader->setFloat("uCameraRainVisibility", m_effects.cameraRainVisibility);
    m_postProcessShader->setFloat("uWeatherExposureBias", m_effects.weatherExposureBias);
    m_postProcessShader->setFloat("uWeatherPostRainFog", m_effects.weatherPostRainFog);
    m_postProcessShader->setInt("uDepthTex", 9);
    m_postProcessShader->setInt("uSceneDepthTex", 11);
    m_postProcessShader->setInt("uPostprocessDebugMode", m_effects.postprocessDebugMode);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);

    static_cast<void>(weatherMaskTex);
    for (int mip = 0; mip < kBloomMipCount; ++mip) {
        glActiveTexture(GL_TEXTURE1 + mip);
        glBindTexture(GL_TEXTURE_2D, hasBloom ? m_bloomTex[mip][0] : 0);
    }
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, m_noiseTexture);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, gbufDepthTex);
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, m_sceneDepthTex);

    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE11);
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

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void PostProcessRenderer::blitSceneToBackbuffer(const Window& window) {
    const int width = std::max(1, window.getWidth());
    const int height = std::max(1, window.getHeight());

    if (!m_sceneCaptured || m_blitShader == nullptr || m_fullscreenVao == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT);

    m_blitShader->use();
    m_blitShader->setInt("uInputTex", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);

    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

float PostProcessRenderer::updateAutoExposure(const float frameTime) {
    const float manualExposure = 0.8f / std::max(m_effects.exposure, 0.0001f);
    if (!m_effects.autoExposureEnabled || m_exposureDownsampleShader == nullptr ||
        m_exposureMipCount <= 0 || m_sceneColorTex == 0 || m_fullscreenVao == 0) {
        m_autoExposureInitialized = false;
        m_adaptedExposure = 1.0f;
        return manualExposure;
    }

    m_exposureDownsampleShader->use();
    m_exposureDownsampleShader->setInt("uInputTex", 0);
    glBindVertexArray(m_fullscreenVao);

    GLuint sourceTex = m_sceneColorTex;
    const int exposureLod = std::min(kAutoExposureLod,
        std::max(0, static_cast<int>(std::floor(std::log2(static_cast<float>(
            std::max(m_targetWidth, m_targetHeight)))))));
    glm::ivec2 sourceSize(std::max(1, m_targetWidth >> exposureLod),
                          std::max(1, m_targetHeight >> exposureLod));
    bool sourceIsScene = true;
    int finalMip = 0;
    for (int mip = 0; mip < m_exposureMipCount; ++mip) {
        if (m_exposureFbos[mip] == 0 || m_exposureTex[mip] == 0) {
            break;
        }
        finalMip = mip;
        glBindFramebuffer(GL_FRAMEBUFFER, m_exposureFbos[mip]);
        glViewport(0, 0, m_exposureMipSize[mip].x, m_exposureMipSize[mip].y);
        glClear(GL_COLOR_BUFFER_BIT);
        m_exposureDownsampleShader->setBool("uSourceIsScene", sourceIsScene);
        m_exposureDownsampleShader->setVec2("uSourceSize", glm::vec2(sourceSize));
        m_exposureDownsampleShader->setInt("uSourceLod", sourceIsScene ? exposureLod : 0);
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

    float exposureData[2] = {0.0f, 0.0f};
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_exposureFbos[finalMip]);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, 1, 1, GL_RG, GL_FLOAT, exposureData);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);

    const float weightedLogLum = exposureData[0];
    const float weightSum = std::max(exposureData[1], 1e-4f);
    const float averageLogLum = weightedLogLum / weightSum;
    // DerivativeMain/program/Post/Temporal.vert::CalculateAverageExposure:
    // exposure = fastExp(weightedLogLuminance * 0.75). Do not apply a Mecraft
    // luminance floor here; the shaderpack relies on very low night luminance to
    // drive the sigmoid exposure response smoothly.
    const float averageLum = std::exp(averageLogLum * 0.75f);
    m_lastAverageLum = averageLum;

    // DerivativeMain sigmoid response curve (Temporal.vert lines 42-57)
    const float bias = m_effects.autoExposureBias;
    const float K = 19.0f;
    const float calibration = std::exp2(bias) * K * 1e-2f;  // 0.19 when bias=0
    const float a = K * 1e-2f * 18.0f;   // 3.42
    const float b = a - K * 1e-2f * 0.04f; // 3.4124
    float targetExposure = calibration / (a - b * std::exp(-averageLum / b));
    m_lastTargetExposure = targetExposure;

    if (!m_autoExposureInitialized) {
        m_adaptedExposure = targetExposure;
        m_autoExposureInitialized = true;
    } else {
        // DerivativeMain/program/Post/Temporal.vert:
        // speed = targetExposure < prevExposure ? 1.5 : 1.0
        const float speed = m_effects.autoExposureSpeed * (targetExposure < m_adaptedExposure ? 1.5f : 1.0f);
        const float alpha = 1.0f - std::exp(-std::max(frameTime, 0.0f) * speed);
        m_adaptedExposure += (targetExposure - m_adaptedExposure) * std::clamp(alpha, 0.0f, 1.0f);
    }

    return m_adaptedExposure;
}

bool PostProcessRenderer::ensureRenderTargets(const int width, const int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (m_sceneFbo != 0 && m_targetWidth == width && m_targetHeight == height) {
        return true;
    }

    destroyRenderTargets();

    glCreateFramebuffers(1, &m_sceneFbo);

    glCreateTextures(GL_TEXTURE_2D, 1, &m_sceneColorTex);
    const int sceneMipLevels = std::max(
        1,
        static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(width, height))))) + 1);
    glTextureStorage2D(m_sceneColorTex, sceneMipLevels, GL_RGBA16F, width, height);
    // Keep normal scene sampling on base level. Bloom uses explicit textureLod()
    // after glGenerateMipmap(), matching DerivativeMain's LOD-driven downsample.
    glTextureParameteri(m_sceneColorTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_sceneColorTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_sceneColorTex, GL_TEXTURE_MAX_LEVEL, sceneMipLevels - 1);
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

    m_targetWidth = width;
    m_targetHeight = height;
    m_autoExposureInitialized = false;
    return true;
}

void PostProcessRenderer::destroyRenderTargets() {
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
    m_exposureMipCount = 0;
    m_autoExposureInitialized = false;
}

void PostProcessRenderer::initFullscreenTriangle() {
    if (m_fullscreenVao != 0) {
        return;
    }
    glGenVertexArrays(1, &m_fullscreenVao);
}

void PostProcessRenderer::destroyFullscreenTriangle() {
    if (m_fullscreenVao != 0) {
        glDeleteVertexArrays(1, &m_fullscreenVao);
        m_fullscreenVao = 0;
    }
}
