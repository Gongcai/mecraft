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
    m_effects.autoExposureMin = std::clamp(m_effects.autoExposureMin, 0.05f, 8.0f);
    m_effects.autoExposureMax = std::clamp(m_effects.autoExposureMax, m_effects.autoExposureMin, 8.0f);
    m_effects.autoExposureSpeed = std::clamp(m_effects.autoExposureSpeed, 0.05f, 12.0f);
    m_effects.autoExposureBias = std::clamp(m_effects.autoExposureBias, -3.0f, 3.0f);
    m_effects.autoExposureDayFactor = std::clamp(m_effects.autoExposureDayFactor, 0.0f, 1.0f);
    m_effects.sunScreenPos.x = std::clamp(m_effects.sunScreenPos.x, -1.0f, 2.0f);
    m_effects.sunScreenPos.y = std::clamp(m_effects.sunScreenPos.y, -1.0f, 2.0f);
    m_effects.sunVisibility = std::clamp(m_effects.sunVisibility, 0.0f, 1.0f);
    m_effects.sunRayStrength = std::clamp(m_effects.sunRayStrength, 0.0f, 1.0f);
    m_effects.tonemapMode = std::clamp(m_effects.tonemapMode, 0, 3);
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
    m_effects.exposure = std::clamp(m_effects.exposure, 0.05f, 8.0f);
    m_effects.gamma = std::clamp(m_effects.gamma, 1.0f, 3.5f);
    m_effects.saturation = std::clamp(m_effects.saturation, 0.0f, 3.0f);
    m_effects.contrast = std::clamp(m_effects.contrast, 0.25f, 3.0f);
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

void PostProcessRenderer::endSceneAndComposite(const Window& window, const float frameTime) {
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

    const float resolvedExposure = updateAutoExposure(frameTime);

    bool hasBloom = false;
    if (m_effects.bloomEnabled && m_bloomExtractShader != nullptr && m_bloomBlurShader != nullptr &&
        m_bloomFbos[0][0] != 0 && m_bloomFbos[0][1] != 0 && m_bloomTex[0][0] != 0 && m_bloomTex[0][1] != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbos[0][0]);
        glViewport(0, 0, m_bloomMipSize[0].x, m_bloomMipSize[0].y);
        glClear(GL_COLOR_BUFFER_BIT);
        m_bloomExtractShader->use();
        m_bloomExtractShader->setInt("uSceneTex", 0);
        m_bloomExtractShader->setFloat("uThreshold", m_effects.bloomThreshold);
        m_bloomExtractShader->setFloat("uIntensity", 1.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);
        glBindVertexArray(m_fullscreenVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        m_bloomBlurShader->use();
        m_bloomBlurShader->setInt("uImage", 0);
        m_bloomBlurShader->setFloat("uWeight", 1.0f);
        for (int mip = 0; mip < kBloomMipCount; ++mip) {
            if (m_bloomFbos[mip][0] == 0 || m_bloomFbos[mip][1] == 0) {
                break;
            }
            if (mip > 0) {
                glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbos[mip][0]);
                glViewport(0, 0, m_bloomMipSize[mip].x, m_bloomMipSize[mip].y);
                glClear(GL_COLOR_BUFFER_BIT);
                m_bloomBlurShader->setVec2("uDirection", glm::vec2(0.0f));
                m_bloomBlurShader->setFloat("uWeight", 0.68f);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, m_bloomTex[mip - 1][0]);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                m_bloomBlurShader->setFloat("uWeight", 1.0f);
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

        const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
        GLint previousBlendSrcRgb = GL_ONE;
        GLint previousBlendDstRgb = GL_ZERO;
        GLint previousBlendSrcAlpha = GL_ONE;
        GLint previousBlendDstAlpha = GL_ZERO;
        glGetIntegerv(GL_BLEND_SRC_RGB, &previousBlendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &previousBlendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &previousBlendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &previousBlendDstAlpha);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        m_bloomBlurShader->setVec2("uDirection", glm::vec2(0.0f));
        for (int mip = kBloomMipCount - 1; mip > 0; --mip) {
            if (m_bloomFbos[mip][0] == 0 || m_bloomFbos[mip - 1][0] == 0) {
                continue;
            }
            const float weight = 0.24f + 0.10f * static_cast<float>(mip);
            glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbos[mip - 1][0]);
            glViewport(0, 0, m_bloomMipSize[mip - 1].x, m_bloomMipSize[mip - 1].y);
            m_bloomBlurShader->setFloat("uWeight", weight);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_bloomTex[mip][0]);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glBlendFuncSeparate(previousBlendSrcRgb, previousBlendDstRgb, previousBlendSrcAlpha, previousBlendDstAlpha);
        if (blendWasEnabled) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
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
    m_postProcessShader->setInt("uNoiseTex", 2);
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

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, hasBloom ? m_bloomTex[0][0] : 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_noiseTexture);

    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

float PostProcessRenderer::updateAutoExposure(const float frameTime) {
    const float manualExposure = std::clamp(m_effects.exposure, 0.05f, 8.0f);
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
    glm::ivec2 sourceSize(m_targetWidth, m_targetHeight);
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
    const float averageLum = std::max(std::exp(averageLogLum), 1e-5f);

    const float dayFactor = m_effects.autoExposureDayFactor;
    const float targetKey = (0.30f + 0.12f * dayFactor) * std::exp2(m_effects.autoExposureBias);
    const float nightAwareMax = std::max(m_effects.autoExposureMin,
                                         std::min(m_effects.autoExposureMax, 1.05f + 0.80f * dayFactor));
    float targetExposure = targetKey / std::pow(averageLum, 0.85f);
    targetExposure = std::clamp(targetExposure, m_effects.autoExposureMin, nightAwareMax);

    if (!m_autoExposureInitialized) {
        m_adaptedExposure = targetExposure;
        m_autoExposureInitialized = true;
    } else {
        const float speed = m_effects.autoExposureSpeed * (targetExposure < m_adaptedExposure ? 1.55f : 1.0f);
        const float alpha = 1.0f - std::exp(-std::max(frameTime, 0.0f) * speed);
        m_adaptedExposure += (targetExposure - m_adaptedExposure) * std::clamp(alpha, 0.0f, 1.0f);
    }

    return std::clamp(manualExposure * m_adaptedExposure, 0.05f, 8.0f);
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
    glTextureStorage2D(m_sceneColorTex, 1, GL_RGBA16F, width, height);
    glTextureParameteri(m_sceneColorTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_sceneColorTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_sceneColorTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_sceneColorTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_sceneFbo, GL_COLOR_ATTACHMENT0, m_sceneColorTex, 0);
    const GLenum sceneDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_sceneFbo, 1, &sceneDrawBuffer);

    glGenRenderbuffers(1, &m_sceneDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_sceneDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, width, height);
    glNamedFramebufferRenderbuffer(m_sceneFbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_sceneDepthRbo);

    const bool complete = glCheckNamedFramebufferStatus(m_sceneFbo, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

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

    glm::ivec2 exposureSize(std::max(1, width / 2), std::max(1, height / 2));
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
    if (m_sceneDepthRbo != 0) {
        glDeleteRenderbuffers(1, &m_sceneDepthRbo);
        m_sceneDepthRbo = 0;
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


