#include "PostProcessRenderer.h"

#include "Shader.h"
#include "../core/Window.h"
#include "../resource/ResourceMgr.h"

#include <algorithm>
#include <glm/vec2.hpp>

PostProcessRenderer::~PostProcessRenderer() {
    shutdown();
}

void PostProcessRenderer::init(ResourceMgr& resourceMgr) {
    m_postProcessShader = resourceMgr.getShader("postprocess");
    m_bloomExtractShader = resourceMgr.getShader("bloom_extract");
    m_bloomBlurShader = resourceMgr.getShader("bloom_blur");
    initFullscreenTriangle();
}

void PostProcessRenderer::shutdown() {
    destroyRenderTargets();
    destroyFullscreenTriangle();
    m_postProcessShader = nullptr;
    m_bloomExtractShader = nullptr;
    m_bloomBlurShader = nullptr;
    m_sceneCaptured = false;
    m_targetWidth = 0;
    m_targetHeight = 0;
}

void PostProcessRenderer::setEffects(const PostProcessEffects& effects) {
    m_effects = effects;
    m_effects.underwaterStrength = std::clamp(m_effects.underwaterStrength, 0.0f, 1.0f);
    m_effects.bloomThreshold = std::clamp(m_effects.bloomThreshold, 0.0f, 4.0f);
    m_effects.bloomStrength = std::clamp(m_effects.bloomStrength, 0.0f, 2.0f);
    m_effects.sunScreenPos.x = std::clamp(m_effects.sunScreenPos.x, -1.0f, 2.0f);
    m_effects.sunScreenPos.y = std::clamp(m_effects.sunScreenPos.y, -1.0f, 2.0f);
    m_effects.sunVisibility = std::clamp(m_effects.sunVisibility, 0.0f, 1.0f);
    m_effects.sunRayStrength = std::clamp(m_effects.sunRayStrength, 0.0f, 1.0f);
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

void PostProcessRenderer::endSceneAndComposite(const Window& window) {
    const int width = std::max(1, window.getWidth());
    const int height = std::max(1, window.getHeight());

    if (!m_sceneCaptured || m_postProcessShader == nullptr || m_fullscreenVao == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    bool hasBloom = false;
    if (m_effects.bloomEnabled && m_bloomExtractShader != nullptr && m_bloomBlurShader != nullptr &&
        m_bloomFbos[0] != 0 && m_bloomFbos[1] != 0 && m_bloomTex[0] != 0 && m_bloomTex[1] != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbos[0]);
        glViewport(0, 0, std::max(1, width / 2), std::max(1, height / 2));
        glClear(GL_COLOR_BUFFER_BIT);
        m_bloomExtractShader->use();
        m_bloomExtractShader->setInt("uSceneTex", 0);
        m_bloomExtractShader->setFloat("uThreshold", m_effects.bloomThreshold);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);
        glBindVertexArray(m_fullscreenVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        bool horizontal = true;
        constexpr int kBlurPasses = 6;
        for (int i = 0; i < kBlurPasses; ++i) {
            const int writeIndex = horizontal ? 1 : 0;
            const int readIndex = horizontal ? 0 : 1;
            glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbos[writeIndex]);
            m_bloomBlurShader->use();
            m_bloomBlurShader->setInt("uImage", 0);
            m_bloomBlurShader->setVec2("uDirection", horizontal ? glm::vec2(1.0f, 0.0f) : glm::vec2(0.0f, 1.0f));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_bloomTex[readIndex]);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            horizontal = !horizontal;
        }
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        hasBloom = true;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);

    m_postProcessShader->use();
    m_postProcessShader->setInt("uSceneTex", 0);
    m_postProcessShader->setInt("uBloomTex", 1);
    m_postProcessShader->setBool("uBloomEnabled", hasBloom);
    m_postProcessShader->setFloat("uBloomStrength", m_effects.bloomStrength);
    m_postProcessShader->setBool("uSunRaysEnabled", m_effects.sunRaysEnabled && hasBloom);
    m_postProcessShader->setVec2("uSunScreenPos", m_effects.sunScreenPos);
    m_postProcessShader->setFloat("uSunVisibility", m_effects.sunVisibility);
    m_postProcessShader->setFloat("uSunRayStrength", m_effects.sunRayStrength);
    m_postProcessShader->setBool("uUnderwaterEnabled", m_effects.underwaterEnabled);
    m_postProcessShader->setVec3("uUnderwaterTint", m_effects.underwaterTint);
    m_postProcessShader->setFloat("uUnderwaterStrength", m_effects.underwaterStrength);
    m_postProcessShader->setFloat("uScreenRollRadians", m_effects.screenRollRadians);
    m_postProcessShader->setFloat("uExposure", m_effects.exposure);
    m_postProcessShader->setFloat("uGamma", m_effects.gamma);
    m_postProcessShader->setFloat("uSaturation", m_effects.saturation);
    m_postProcessShader->setFloat("uContrast", m_effects.contrast);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, hasBloom ? m_bloomTex[0] : 0);

    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
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

    const int bloomWidth = std::max(1, width / 2);
    const int bloomHeight = std::max(1, height / 2);
    glCreateFramebuffers(2, m_bloomFbos);
    glCreateTextures(GL_TEXTURE_2D, 2, m_bloomTex);
    for (int i = 0; i < 2; ++i) {
        glTextureStorage2D(m_bloomTex[i], 1, GL_RGBA16F, bloomWidth, bloomHeight);
        glTextureParameteri(m_bloomTex[i], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(m_bloomTex[i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(m_bloomTex[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_bloomTex[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(m_bloomFbos[i], GL_COLOR_ATTACHMENT0, m_bloomTex[i], 0);
        const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
        glNamedFramebufferDrawBuffers(m_bloomFbos[i], 1, &drawBuffer);
        if (glCheckNamedFramebufferStatus(m_bloomFbos[i], GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            destroyRenderTargets();
            return false;
        }
    }

    m_targetWidth = width;
    m_targetHeight = height;
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
    for (int i = 0; i < 2; ++i) {
        if (m_bloomTex[i] != 0) {
            glDeleteTextures(1, &m_bloomTex[i]);
            m_bloomTex[i] = 0;
        }
        if (m_bloomFbos[i] != 0) {
            glDeleteFramebuffers(1, &m_bloomFbos[i]);
            m_bloomFbos[i] = 0;
        }
    }
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


